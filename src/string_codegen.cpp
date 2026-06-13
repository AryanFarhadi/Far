#include "string_codegen.h"

#include "error.h"
#include "string_methods.h"

namespace far {

static std::string strPtrFromI64(StrCodegenCtx ctx, const std::string& val) {
  std::string ptr = ctx.fresh("sp");
  ctx.out << "  %" << ptr << " = inttoptr i64 " << val << " to i8*\n";
  return "%" + ptr;
}

std::string emitStrMethod(StrCodegenCtx ctx, const MethodCall& call, const std::string& recv_val) {
  const StrMethodInfo* mi = lookupStrMethod(call.method);
  if (!mi)
    throw FarError("unknown string method '" + call.method + "'");
  std::string recv_ptr = strPtrFromI64(ctx, recv_val);
  std::string tmp = ctx.fresh("sm");
  switch (mi->id) {
    case StrMethodId::Trim:
    case StrMethodId::ToLower:
    case StrMethodId::ToUpper: {
      const char* rt = mi->id == StrMethodId::Trim   ? "far_str_trim"
                       : mi->id == StrMethodId::ToLower ? "far_str_tolower"
                                                         : "far_str_toupper";
      std::string out_ptr = ctx.fresh("sout");
      ctx.out << "  %" << out_ptr << " = call i8* @" << rt << "(i8* " << recv_ptr << ")\n";
      ctx.out << "  %" << tmp << " = ptrtoint i8* %" << out_ptr << " to i64\n";
      break;
    }
    case StrMethodId::Split:
    case StrMethodId::Contains:
    case StrMethodId::StartsWith:
    case StrMethodId::EndsWith: {
      std::string arg = ctx.emit_expr(*call.args[0]);
      std::string arg_ptr = strPtrFromI64(ctx, arg);
      const char* rt = mi->id == StrMethodId::Split         ? "far_str_split"
                       : mi->id == StrMethodId::Contains    ? "far_str_contains"
                       : mi->id == StrMethodId::StartsWith  ? "far_str_starts_with"
                                                            : "far_str_ends_with";
      ctx.out << "  %" << tmp << " = call i64 @" << rt << "(i8* " << recv_ptr << ", i8* " << arg_ptr
              << ")\n";
      break;
    }
    default:
      throw FarError("unsupported string method");
  }
  return "%" + tmp;
}

}  // namespace far
