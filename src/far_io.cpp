#include "far_io.h"

#include "error.h"
#include "type_desc.h"

#include <unordered_map>

namespace far {

namespace {

#define S IoTy::Str
#define I IoTy::I64
#define D IoTy::F64
#define B IoTy::Bool
#define V IoTy::Void

#define I0(n, rt, r) \
  { n, rt, r, 0, {} }
#define I1(n, rt, r, a0) \
  { n, rt, r, 1, { a0 } }

static const struct {
  const char* alias;
  const char* target;
} kConsoleAliases[] = {
    {"input", "io_read_line"},
    {"input_prompt", "io_read_line_prompt"},
    {"read_line", "io_read_line"},
    {"read_line_prompt", "io_read_line_prompt"},
    {"read_char", "io_read_char"},
    {"read_i64", "io_read_i64"},
    {"read_f64", "io_read_f64"},
    {"has_input", "io_has_input"},
    {"confirm", "io_confirm"},
    {"write", "io_write"},
    {"writeln", "io_writeln"},
    {"write_text", "io_write"},
    {"write_line", "io_writeln"},
    {"write_err", "io_write_err"},
    {"write_errln", "io_write_errln"},
    {"write_err_line", "io_write_errln"},
    {"flush_out", "io_flush_stdout"},
    {"flush_err", "io_flush_stderr"},
    {"clear", "io_clear"},
    {"clear_screen", "io_clear"},
    {"is_tty", "io_is_tty"},
    {"columns", "io_columns"},
    {"beep", "io_beep"},
};

static const IoFn kIo[] = {
    I0("io_read_line", "far_io_read_line", S),
    I1("io_read_line_prompt", "far_io_read_line_prompt", S, S),
    I0("io_read_char", "far_io_read_char", I),
    I0("io_read_i64", "far_io_read_i64", I),
    I0("io_read_f64", "far_io_read_f64", D),
    I0("io_has_input", "far_io_has_input", B),
    I1("io_confirm", "far_io_confirm", B, S),

    I1("io_write", "far_io_write", V, S),
    I1("io_writeln", "far_io_writeln", V, S),
    I1("io_write_err", "far_io_write_err", V, S),
    I1("io_write_errln", "far_io_write_errln", V, S),
    I0("io_flush_stdout", "far_io_flush_stdout", V),
    I0("io_flush_stderr", "far_io_flush_stderr", V),

    I0("io_clear", "far_io_clear", V),
    I0("io_is_tty", "far_io_is_tty", B),
    I0("io_columns", "far_io_columns", I),
    I0("io_beep", "far_io_beep", V),
};

#undef I0
#undef I1
#undef S
#undef I
#undef D
#undef B
#undef V

static FarTypeId ioToFar(IoTy ty) {
  switch (ty) {
    case IoTy::F64:
      return FarTypeId::F64;
    case IoTy::Str:
      return FarTypeId::String;
    case IoTy::Bool:
    case IoTy::Void:
    case IoTy::I64:
    default:
      return FarTypeId::I64;
  }
}

}  // namespace

const IoFn* lookupIo(const std::string& name) {
  static const std::unordered_map<std::string, const IoFn*> map = [] {
    std::unordered_map<std::string, const IoFn*> m;
    for (const auto& f : kIo)
      m[f.name] = &f;
    for (const auto& a : kConsoleAliases) {
      auto it = m.find(a.target);
      if (it != m.end())
        m[a.alias] = it->second;
    }
    return m;
  }();
  auto it = map.find(name);
  return it == map.end() ? nullptr : it->second;
}

const IoFn* resolveIoCall(const std::string& name, int nargs) {
  if (name == "input") {
    if (nargs == 0)
      return lookupIo("io_read_line");
    if (nargs == 1)
      return lookupIo("io_read_line_prompt");
    return nullptr;
  }
  const IoFn* fn = lookupIo(name);
  if (!fn || fn->nargs != nargs)
    return nullptr;
  return fn;
}

std::string ioArgLlvm(IoTy ty) {
  if (ty == IoTy::F64)
    return "double";
  if (ty == IoTy::Str)
    return "i8*";
  return "i64";
}

std::string ioRetLlvm(IoTy ty) {
  if (ty == IoTy::F64)
    return "double";
  if (ty == IoTy::Str)
    return "i8*";
  return "i64";
}

void declareIoRuntime(std::ostringstream& out) {
  for (const auto& f : kIo) {
    std::ostringstream sig;
    sig << (f.ret == IoTy::Void ? "void" : ioRetLlvm(f.ret)) << " @" << f.rt_name << "(";
    for (int i = 0; i < f.nargs; ++i) {
      if (i > 0)
        sig << ", ";
      sig << ioArgLlvm(f.args[i]);
    }
    sig << ")";
    out << "declare " << sig.str() << "\n";
  }
}

TypeDesc ioRetType(const IoFn* fn) {
  if (fn->ret == IoTy::Bool)
    return TypeDesc::prim(FarTypeId::I64);
  if (fn->ret == IoTy::Void)
    return TypeDesc::prim(FarTypeId::I64);
  return TypeDesc::prim(ioToFar(fn->ret));
}

bool checkIoArgs(const IoFn* fn, const std::string& display, const std::vector<CallArg>& args,
                 const std::function<TypeDesc(Expr&)>& typecheck_expr) {
  if (static_cast<int>(args.size()) != fn->nargs) {
    throw FarError(display + "() expects " + std::to_string(fn->nargs) + " argument(s)");
  }
  for (int i = 0; i < fn->nargs; ++i) {
    TypeDesc td = typecheck_expr(*args[static_cast<size_t>(i)].value);
    if (!isPrimitiveDesc(td))
      throw FarError(display + "() argument " + std::to_string(i + 1) + " type mismatch");
    FarTypeId at = td.primitive;
    FarTypeId want = ioToFar(fn->args[i]);
    if (canAssign(at, want))
      continue;
    if (want == FarTypeId::F64 && (isIntegerType(at) || at == FarTypeId::F32))
      continue;
    throw FarError(display + "() argument " + std::to_string(i + 1) + " type mismatch");
  }
  return true;
}

}  // namespace far
