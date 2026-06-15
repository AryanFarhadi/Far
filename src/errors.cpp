#include "errors.h"

#include "error.h"

namespace far {

static const ErrConstructorInfo kErrConstructors[] = {
    {"Ok", true, false, 1},   {"ok", true, false, 1},
    {"Err", false, false, 1}, {"err", false, false, 1},
    {"Some", false, true, 1}, {"some", false, true, 1},
    {"None", false, false, 0}, {"none", false, false, 0},
};

static const ErrMethodInfo kOptionMethods[] = {
    {ErrMethodId::IsSome, "is_some", 0}, {ErrMethodId::IsNone, "is_none", 0},
    {ErrMethodId::Unwrap, "unwrap", 0},  {ErrMethodId::UnwrapOr, "unwrap_or", 1},
};

static const ErrMethodInfo kResultMethods[] = {
    {ErrMethodId::IsOk, "is_ok", 0},    {ErrMethodId::IsErr, "is_err", 0},
    {ErrMethodId::Unwrap, "unwrap", 0},  {ErrMethodId::UnwrapOr, "unwrap_or", 1},
    {ErrMethodId::Ok, "ok", 0},          {ErrMethodId::Err, "err", 0},
};

const ErrConstructorInfo* lookupErrConstructor(const std::string& name) {
  for (const auto& c : kErrConstructors) {
    if (name == c.name)
      return &c;
  }
  return nullptr;
}

const ErrMethodInfo* lookupOptionMethod(const std::string& name) {
  for (const auto& m : kOptionMethods) {
    if (name == m.name)
      return &m;
  }
  return nullptr;
}

const ErrMethodInfo* lookupResultMethod(const std::string& name) {
  for (const auto& m : kResultMethods) {
    if (name == m.name)
      return &m;
  }
  return nullptr;
}

TypeDesc errMethodRetType(ErrMethodId id, const TypeDesc& recv, const TypeDesc& arg) {
  (void)arg;
  switch (id) {
    case ErrMethodId::IsSome:
    case ErrMethodId::IsNone:
    case ErrMethodId::IsOk:
    case ErrMethodId::IsErr:
      return TypeDesc::prim(FarTypeId::Bool);
    case ErrMethodId::Unwrap:
      if (isOptionDesc(recv) && !recv.args.empty())
        return recv.args[0];
      if (isResultDesc(recv) && !recv.args.empty())
        return recv.args[0];
      return TypeDesc::prim(FarTypeId::I64);
    case ErrMethodId::UnwrapOr:
      if (isOptionDesc(recv) && !recv.args.empty())
        return recv.args[0];
      if (isResultDesc(recv) && !recv.args.empty())
        return recv.args[0];
      return TypeDesc::prim(FarTypeId::I64);
    case ErrMethodId::Ok:
      if (isResultDesc(recv) && !recv.args.empty())
        return recv.args[0];
      return TypeDesc::prim(FarTypeId::I64);
    case ErrMethodId::Err:
      if (isResultDesc(recv) && recv.args.size() > 1)
        return recv.args[1];
      return TypeDesc::prim(FarTypeId::I64);
    default:
      return TypeDesc::prim(FarTypeId::I64);
  }
}

void declareErrorRuntime(std::ostringstream& out) {
  out << "declare i32 @far_try_enter()\n";
  out << "declare void @far_try_success()\n";
  out << "declare void @far_throw(i64, i64)\n";
  out << "declare void @far_store_caught(i64, i64)\n";
  out << "declare i64 @far_caught_tag()\n";
  out << "declare i64 @far_caught_value()\n";
  out << "declare i32 @far_caught_matches(i64)\n";
  out << "declare void @far_panic(i64)\n";
  out << "declare void @far_assert(i64, i64)\n";
  out << "declare void @far_stack_trace()\n";
  out << "declare i64 @far_option_some(i64)\n";
  out << "declare i64 @far_option_none()\n";
  out << "declare i64 @far_option_is_some(i64)\n";
  out << "declare i64 @far_option_unwrap(i64)\n";
  out << "declare i64 @far_option_unwrap_or(i64, i64)\n";
  out << "declare i64 @far_result_ok(i64)\n";
  out << "declare i64 @far_result_err(i64)\n";
  out << "declare i64 @far_result_is_ok(i64)\n";
  out << "declare i64 @far_result_is_err(i64)\n";
  out << "declare i64 @far_result_unwrap(i64)\n";
  out << "declare i64 @far_result_unwrap_or(i64, i64)\n";
  out << "declare i64 @far_result_ok_val(i64)\n";
  out << "declare i64 @far_result_err_val(i64)\n";
  out << "declare i64 @far_i64_add_checked(i64, i64)\n";
  out << "declare i64 @far_i64_sub_checked(i64, i64)\n";
  out << "declare i64 @far_i64_mul_checked(i64, i64)\n";
  out << "declare i64 @far_i64_div_checked(i64, i64)\n";
  out << "declare i64 @far_i64_mod_checked(i64, i64)\n";
  out << "declare i64 @far_i64_neg_checked(i64)\n";
  out << "declare i64 @far_u64_div_checked(i64, i64)\n";
  out << "declare i64 @far_u64_mod_checked(i64, i64)\n";
  out << "declare i64 @far_i64_shl_checked(i64, i64)\n";
  out << "declare i64 @far_i64_shr_checked(i64, i64)\n";
  out << "declare i64 @far_u64_shr_checked(i64, i64)\n";
  out << "declare double @far_f64_div_checked(double, double)\n";
  out << "declare double @far_f64_rem_checked(double, double)\n";
}

}  // namespace far
