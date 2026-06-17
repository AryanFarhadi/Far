#include "errors_codegen.h"

#include "error.h"

namespace far {

std::string emitErrConstruct(ErrCodegenCtx ctx, const ErrConstructorInfo& ctor,
                             const std::vector<std::string>& arg_vals) {
  std::string tmp = ctx.fresh("opt");
  if (ctor.is_some) {
    std::string v = arg_vals.empty() ? "0" : arg_vals[0];
    ctx.out << "  %" << tmp << " = call i64 @far_option_some(i64 " << v << ")\n";
    return "%" + tmp;
  }
  if (ctor.name[0] == 'N' || ctor.name[0] == 'n') {
    ctx.out << "  %" << tmp << " = call i64 @far_option_none()\n";
    return "%" + tmp;
  }
  if (ctor.is_ok) {
    std::string v = arg_vals.empty() ? "0" : arg_vals[0];
    ctx.out << "  %" << tmp << " = call i64 @far_result_ok(i64 " << v << ")\n";
    return "%" + tmp;
  }
  std::string v = arg_vals.empty() ? "0" : arg_vals[0];
  ctx.out << "  %" << tmp << " = call i64 @far_result_err(i64 " << v << ")\n";
  return "%" + tmp;
}

std::string emitErrMethod(ErrCodegenCtx ctx, const MethodCall& call, const TypeDesc& recv_ty,
                          const std::string& recv_val) {
  const ErrMethodInfo* mi = isOptionDesc(recv_ty) ? lookupOptionMethod(call.method)
                                                  : lookupResultMethod(call.method);
  if (!mi)
    throw FarError("unknown method '" + call.method + "'");
  std::string tmp = ctx.fresh("em");
  switch (mi->id) {
    case ErrMethodId::IsSome:
      ctx.out << "  %" << tmp << " = call i64 @far_option_is_some(i64 " << recv_val << ")\n";
      break;
    case ErrMethodId::IsNone: {
      std::string t2 = ctx.fresh("em");
      ctx.out << "  %" << t2 << " = call i64 @far_option_is_some(i64 " << recv_val << ")\n";
      ctx.out << "  %" << tmp << " = xor i64 %" << t2 << ", 1\n";
      break;
    }
    case ErrMethodId::IsOk:
      ctx.out << "  %" << tmp << " = call i64 @far_result_is_ok(i64 " << recv_val << ")\n";
      break;
    case ErrMethodId::IsErr: {
      std::string t2 = ctx.fresh("em");
      ctx.out << "  %" << t2 << " = call i64 @far_result_is_ok(i64 " << recv_val << ")\n";
      ctx.out << "  %" << tmp << " = xor i64 %" << t2 << ", 1\n";
      break;
    }
    case ErrMethodId::Unwrap:
      if (isOptionDesc(recv_ty))
        ctx.out << "  %" << tmp << " = call i64 @far_option_unwrap(i64 " << recv_val << ")\n";
      else
        ctx.out << "  %" << tmp << " = call i64 @far_result_unwrap(i64 " << recv_val << ")\n";
      break;
    case ErrMethodId::UnwrapOr: {
      std::string alt = ctx.emit_expr(*call.args[0]);
      if (isOptionDesc(recv_ty))
        ctx.out << "  %" << tmp << " = call i64 @far_option_unwrap_or(i64 " << recv_val << ", i64 "
                << alt << ")\n";
      else
        ctx.out << "  %" << tmp << " = call i64 @far_result_unwrap_or(i64 " << recv_val << ", i64 "
                << alt << ")\n";
      break;
    }
    case ErrMethodId::Ok:
      ctx.out << "  %" << tmp << " = call i64 @far_result_ok_val(i64 " << recv_val << ")\n";
      break;
    case ErrMethodId::Err:
      ctx.out << "  %" << tmp << " = call i64 @far_result_err_val(i64 " << recv_val << ")\n";
      break;
    default:
      throw FarError("unsupported error method");
  }
  return "%" + tmp;
}

void emitThrow(ErrCodegenCtx ctx, int64_t type_tag, const std::string& value) {
  ctx.out << "  call void @far_throw(i64 " << type_tag << ", i64 " << value << ")\n";
}

void emitTryEnter(ErrCodegenCtx ctx, const std::string& body_label) {
  ctx.out << "  call void @far_try_push()\n";
  ctx.out << "  br label %" << body_label << "\n";
}

void emitTrySuccess(ErrCodegenCtx ctx) { ctx.out << "  call void @far_try_success()\n"; }

std::string emitCaughtValue(ErrCodegenCtx ctx) {
  std::string tmp = ctx.fresh("caught");
  ctx.out << "  %" << tmp << " = call i64 @far_caught_value()\n";
  return "%" + tmp;
}

std::string emitCaughtTag(ErrCodegenCtx ctx) {
  std::string tmp = ctx.fresh("ctag");
  ctx.out << "  %" << tmp << " = call i64 @far_caught_tag()\n";
  return "%" + tmp;
}

std::string emitCaughtMatches(ErrCodegenCtx ctx, int64_t expected_tag) {
  std::string tag = emitCaughtTagGlobal(ctx);
  std::string cmp = ctx.fresh("tagcmp");
  ctx.out << "  %" << cmp << " = icmp eq i64 " << tag << ", " << expected_tag << "\n";
  return "%" + cmp;
}

std::string emitCaughtValueGlobal(ErrCodegenCtx ctx) {
  std::string tmp = ctx.fresh("caught");
  ctx.out << "  %" << tmp << " = call i64 @far_caught_value()\n";
  return "%" + tmp;
}

std::string emitCaughtTagGlobal(ErrCodegenCtx ctx) {
  std::string tmp = ctx.fresh("ctag");
  ctx.out << "  %" << tmp << " = call i64 @far_caught_tag()\n";
  return "%" + tmp;
}

void emitCaughtBind(ErrCodegenCtx ctx, const std::string& catch_var) {
  std::string tmp = ctx.fresh("caught");
  ctx.out << "  %" << tmp << " = call i64 @far_caught_value()\n";
  ctx.out << "  ; bind " << catch_var << " = %" << tmp << "\n";
}

}  // namespace far
