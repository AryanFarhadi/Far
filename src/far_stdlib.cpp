#include "far_stdlib.h"

#include "error.h"
#include "type_desc.h"

#include <unordered_map>

namespace far {

namespace {

#define S StdlibTy::Str
#define I StdlibTy::I64
#define D StdlibTy::F64
#define B StdlibTy::Bool
#define V StdlibTy::Void

#define F0(n, rt, r) \
  { n, rt, r, 0, {} }
#define F1(n, rt, r, a0) \
  { n, rt, r, 1, { a0 } }
#define F2(n, rt, r, a0, a1) \
  { n, rt, r, 2, { a0, a1 } }
#define F3(n, rt, r, a0, a1, a2) \
  { n, rt, r, 3, { a0, a1, a2 } }

static const StdlibFn kStdlib[] = {
    // random
    F1("rand_seed", "far_rand_seed", V, I),
    F0("rand_i64", "far_rand_i64", I),
    F0("rand_f64", "far_rand_f64", D),
    F2("rand_range", "far_rand_range", I, I, I),

    // time
    F0("now_ms", "far_now_ms", I),
    F0("now_sec", "far_now_sec", I),
    F0("clock_ticks", "far_clock_ticks", I),

    // date
    F1("date_year", "far_date_year", I, I),
    F1("date_month", "far_date_month", I, I),
    F1("date_day", "far_date_day", I, I),
    F1("date_hour", "far_date_hour", I, I),
    F1("date_minute", "far_date_minute", I, I),
    F1("date_second", "far_date_second", I, I),

    // filesystem
    F1("fs_read", "far_fs_read", S, S),
    F2("fs_write", "far_fs_write", I, S, S),
    F1("fs_exists", "far_fs_exists", B, S),
    F1("fs_is_file", "far_fs_is_file", B, S),
    F1("fs_is_dir", "far_fs_is_dir", B, S),
    F1("fs_mkdir", "far_fs_mkdir", I, S),
    F1("fs_remove", "far_fs_remove", I, S),

    // networking
    F2("net_connect", "far_net_connect", I, S, I),
    F2("net_send", "far_net_send", I, I, S),
    F2("net_recv", "far_net_recv", S, I, I),
    F1("net_close", "far_net_close", I, I),

    // json
    F1("json_escape", "far_json_escape", S, S),
    F1("json_stringify_i64", "far_json_stringify_i64", S, I),
    F1("json_stringify_str", "far_json_stringify_str", S, S),
    F2("json_get_i64", "far_json_get_i64", I, S, S),
    F2("json_get_str", "far_json_get_str", S, S, S),

    // xml
    F1("xml_escape", "far_xml_escape", S, S),
    F2("xml_tag", "far_xml_tag", S, S, S),
    F2("xml_get_attr", "far_xml_get_attr", S, S, S),

    // yaml
    F2("yaml_get", "far_yaml_get", S, S, S),

    // csv
    F1("csv_count", "far_csv_count", I, S),
    F2("csv_field", "far_csv_field", S, S, I),

    // logging
    F1("log_info", "far_log_info", V, S),
    F1("log_warn", "far_log_warn", V, S),
    F1("log_error", "far_log_error", V, S),
    F1("log_debug", "far_log_debug", V, S),

    // regex
    F2("regex_match", "far_regex_match", B, S, S),
    F2("regex_find", "far_regex_find", I, S, S),

    // compression
    F1("compress_rle", "far_compress_rle", S, S),
    F1("decompress_rle", "far_decompress_rle", S, S),

    // encryption
    F2("xor_crypt", "far_xor_crypt", S, S, S),

    // hashing
    F1("hash_fnv", "far_hash_fnv", I, S),
    F1("hash_crc32", "far_hash_crc32", I, S),
    F1("hash_md5_hex", "far_hash_md5_hex", S, S),

    // process
    F1("proc_run", "far_proc_run", I, S),
    F0("proc_pid", "far_proc_pid", I),

    // environment
    F1("env_get", "far_env_get", S, S),
    F2("env_set", "far_env_set", I, S, S),
    F1("env_has", "far_env_has", B, S),

    // cli args
    F0("args_count", "far_args_count", I),
    F1("args_get", "far_args_get", S, I),

    // benchmarking
    F0("bench_now", "far_bench_now", I),
    F1("bench_elapsed_ms", "far_bench_elapsed_ms", I, I),
};

#undef F0
#undef F1
#undef F2
#undef F3
#undef S
#undef I
#undef D
#undef B
#undef V

static FarTypeId stdlibToFar(StdlibTy ty) {
  switch (ty) {
    case StdlibTy::Str:
      return FarTypeId::String;
    case StdlibTy::F64:
      return FarTypeId::F64;
    case StdlibTy::Bool:
      return FarTypeId::Bool;
    case StdlibTy::Void:
    case StdlibTy::I64:
    default:
      return FarTypeId::I64;
  }
}

}  // namespace

const StdlibFn* lookupStdlib(const std::string& name) {
  static const std::unordered_map<std::string, const StdlibFn*> map = [] {
    std::unordered_map<std::string, const StdlibFn*> m;
    for (const auto& f : kStdlib)
      m[f.name] = &f;
    return m;
  }();
  auto it = map.find(name);
  return it == map.end() ? nullptr : it->second;
}

std::string stdlibArgLlvm(StdlibTy ty) {
  if (ty == StdlibTy::Str)
    return "i8*";
  if (ty == StdlibTy::F64)
    return "double";
  return "i64";
}

std::string stdlibRetLlvm(StdlibTy ty) {
  if (ty == StdlibTy::Str)
    return "i8*";
  if (ty == StdlibTy::F64)
    return "double";
  return "i64";
}

void declareStdlibRuntime(std::ostringstream& out) {
  for (const auto& f : kStdlib) {
    std::ostringstream sig;
    sig << (f.ret == StdlibTy::Void ? "void" : stdlibRetLlvm(f.ret)) << " @" << f.rt_name << "(";
    for (int i = 0; i < f.nargs; ++i) {
      if (i > 0)
        sig << ", ";
      sig << stdlibArgLlvm(f.args[i]);
    }
    sig << ")";
    out << "declare " << sig.str() << "\n";
  }
}

TypeDesc stdlibRetType(const StdlibFn* fn) {
  if (fn->ret == StdlibTy::Bool)
    return TypeDesc::prim(FarTypeId::I64);
  return TypeDesc::prim(stdlibToFar(fn->ret));
}

bool checkStdlibArgs(const StdlibFn* fn, const std::vector<CallArg>& args,
                     const std::function<FarTypeId(Expr&)>& typecheck_expr) {
  if (static_cast<int>(args.size()) != fn->nargs) {
    throw FarError(std::string(fn->name) + "() expects " + std::to_string(fn->nargs) + " argument(s)");
  }
  for (int i = 0; i < fn->nargs; ++i) {
    FarTypeId at = typecheck_expr(*args[static_cast<size_t>(i)].value);
    FarTypeId expected = stdlibToFar(fn->args[i]);
    if (canAssign(at, expected))
      continue;
    if (expected == FarTypeId::F64 && (isIntegerType(at) || at == FarTypeId::F32))
      continue;
    throw FarError(std::string(fn->name) + "() argument " + std::to_string(i + 1) + " type mismatch");
  }
  return true;
}

}  // namespace far
