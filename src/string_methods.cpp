#include "string_methods.h"

namespace far {

static const StrMethodInfo kStrMethods[] = {
    {StrMethodId::Trim, "trim", 0},
    {StrMethodId::ToLower, "tolower", 0},
    {StrMethodId::ToUpper, "toupper", 0},
    {StrMethodId::Split, "split", 1},
    {StrMethodId::Contains, "contains", 1},
    {StrMethodId::StartsWith, "starts_with", 1},
    {StrMethodId::EndsWith, "ends_with", 1},
};

const StrMethodInfo* lookupStrMethod(const std::string& name) {
  for (const auto& m : kStrMethods) {
    if (name == m.name)
      return &m;
  }
  return nullptr;
}

TypeDesc strMethodRetType(StrMethodId id) {
  switch (id) {
    case StrMethodId::Contains:
    case StrMethodId::StartsWith:
    case StrMethodId::EndsWith:
      return TypeDesc::prim(FarTypeId::I64);
    case StrMethodId::Trim:
    case StrMethodId::ToLower:
    case StrMethodId::ToUpper:
      return TypeDesc::prim(FarTypeId::String);
    case StrMethodId::Split:
      return TypeDesc::list(TypeDesc::prim(FarTypeId::String));
    default:
      return TypeDesc::prim(FarTypeId::I64);
  }
}

void declareStringRuntime(std::ostringstream& out) {
  out << "declare i8* @far_str_trim(i8*)\n";
  out << "declare i8* @far_str_tolower(i8*)\n";
  out << "declare i8* @far_str_toupper(i8*)\n";
  out << "declare i64 @far_str_split(i8*, i8*)\n";
  out << "declare i64 @far_str_contains(i8*, i8*)\n";
  out << "declare i64 @far_str_starts_with(i8*, i8*)\n";
  out << "declare i64 @far_str_ends_with(i8*, i8*)\n";
}

}  // namespace far
