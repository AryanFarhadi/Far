#include "types.h"

#include "aggregate.h"
#include "error.h"

#include <climits>
#include <limits>
#include <unordered_map>

namespace far {

static const FarTypeInfo kTypes[] = {
    {FarTypeId::I8, "i8", "i8", true, false, true, 8, -128, 127, 0, 0},
    {FarTypeId::I16, "i16", "i16", true, false, true, 16, -32768, 32767, 0, 0},
    {FarTypeId::I32, "i32", "i32", true, false, true, 32, INT32_MIN, INT32_MAX, 0, 0},
    {FarTypeId::I64, "i64", "i64", true, false, true, 64, INT64_MIN, INT64_MAX, 0, 0},
    {FarTypeId::I128, "i128", "i128", true, false, true, 128, 0, 0, 0, 0},
    {FarTypeId::U8, "u8", "i8", false, false, true, 8, 0, 255, 0, 0},
    {FarTypeId::U16, "u16", "i16", false, false, true, 16, 0, 65535, 0, 0},
    {FarTypeId::U32, "u32", "i32", false, false, true, 32, 0, UINT32_MAX, 0, 0},
    {FarTypeId::U64, "u64", "i64", false, false, true, 64, 0, static_cast<int64_t>(UINT64_MAX), 0, 0},
    {FarTypeId::U128, "u128", "i128", false, false, true, 128, 0, 0, 0, 0},
    {FarTypeId::F16, "f16", "half", true, true, false, 16, 0, 0,
     -65504.0, 65504.0},
    {FarTypeId::F32, "f32", "float", true, true, false, 32, 0, 0,
     -std::numeric_limits<float>::max(), std::numeric_limits<float>::max()},
    {FarTypeId::F64, "f64", "double", true, true, false, 64, 0, 0,
     -std::numeric_limits<double>::max(), std::numeric_limits<double>::max()},
    {FarTypeId::F128, "f128", "fp128", true, true, false, 128, 0, 0, 0, 0},
    {FarTypeId::Bool, "bool", "i1", false, false, false, 1, 0, 1, 0, 0},
    {FarTypeId::Char, "char", "i16", false, false, false, 16, 0, 65535, 0, 0},
    {FarTypeId::String, "string", "i8*", false, false, false, 64, 0, 0, 0, 0},
    {FarTypeId::RawString, "raw_string", "i8*", false, false, false, 64, 0, 0, 0, 0},
    {FarTypeId::Ptr, "ptr", "i64", false, false, false, 64, 0, 0, 0, 0},
    {FarTypeId::Ref, "ref", "i64", false, false, false, 64, 0, 0, 0, 0},
    {FarTypeId::Any, "any", "i64", false, false, false, 64, 0, 0, 0, 0},
    {FarTypeId::Void, "void", "void", false, false, false, 0, 0, 0, 0, 0},
    {FarTypeId::Arr, "arr", "i64", false, false, false, 64, 0, 0, 0, 0},
    {FarTypeId::FVec2, "fvec2", "%FarFVec2", true, false, false, 64, 0, 0, 0, 0},
    {FarTypeId::FVec3, "fvec3", "%FarFVec3", true, false, false, 96, 0, 0, 0, 0},
    {FarTypeId::FVec4, "fvec4", "%FarFVec4", true, false, false, 128, 0, 0, 0, 0},
    {FarTypeId::DVec2, "dvec2", "%FarDVec2", true, false, false, 128, 0, 0, 0, 0},
    {FarTypeId::DVec3, "dvec3", "%FarDVec3", true, false, false, 192, 0, 0, 0, 0},
    {FarTypeId::DVec4, "dvec4", "%FarDVec4", true, false, false, 256, 0, 0, 0, 0},
    {FarTypeId::FPoint, "fpoint", "%FarFPoint", true, false, false, 64, 0, 0, 0, 0},
    {FarTypeId::DPoint, "dpoint", "%FarDPoint", true, false, false, 128, 0, 0, 0, 0},
    {FarTypeId::FRect, "frect", "%FarFRect", true, false, false, 128, 0, 0, 0, 0},
    {FarTypeId::DRect, "drect", "%FarDRect", true, false, false, 256, 0, 0, 0, 0},
    {FarTypeId::IVec2, "ivec2", "%FarIVec2", true, false, false, 64, 0, 0, 0, 0},
    {FarTypeId::IVec3, "ivec3", "%FarIVec3", true, false, false, 96, 0, 0, 0, 0},
    {FarTypeId::IVec4, "ivec4", "%FarIVec4", true, false, false, 128, 0, 0, 0, 0},
    {FarTypeId::Mat2, "mat2", "%FarMat2", true, false, false, 128, 0, 0, 0, 0},
    {FarTypeId::Mat3, "mat3", "%FarMat3", true, false, false, 288, 0, 0, 0, 0},
    {FarTypeId::Mat4, "mat4", "%FarMat4", true, false, false, 512, 0, 0, 0, 0},
    {FarTypeId::DMat2, "dmat2", "%FarDMat2", true, false, false, 256, 0, 0, 0, 0},
    {FarTypeId::DMat3, "dmat3", "%FarDMat3", true, false, false, 576, 0, 0, 0, 0},
    {FarTypeId::DMat4, "dmat4", "%FarDMat4", true, false, false, 1024, 0, 0, 0, 0},
    {FarTypeId::Quat, "quat", "%FarQuat", true, false, false, 128, 0, 0, 0, 0},
    {FarTypeId::DQuat, "dquat", "%FarDQuat", true, false, false, 256, 0, 0, 0, 0},
    {FarTypeId::Color, "color", "%FarColor", true, false, false, 128, 0, 0, 0, 0},
    {FarTypeId::Color32, "color32", "%FarColor32", false, false, false, 32, 0, 0, 0, 0},
    {FarTypeId::Transform, "transform", "%FarTransform", true, false, false, 320, 0, 0, 0, 0},
    {FarTypeId::Bounds, "bounds", "%FarBounds", true, false, false, 192, 0, 0, 0, 0},
};

static const std::unordered_map<std::string, FarTypeId> kTypeAliases = {
    // C#-style signed integers
    {"sbyte", FarTypeId::I8},
    {"short", FarTypeId::I16},
    {"int", FarTypeId::I32},
    {"long", FarTypeId::I64},
    {"longlong", FarTypeId::I128},
    {"int128", FarTypeId::I128},
    // C#-style unsigned integers
    {"byte", FarTypeId::U8},
    {"ushort", FarTypeId::U16},
    {"uint", FarTypeId::U32},
    {"ulong", FarTypeId::U64},
    {"ulonglong", FarTypeId::U128},
    {"uint128", FarTypeId::U128},
    // C#-style floats
    {"half", FarTypeId::F16},
    {"float", FarTypeId::F32},
    {"double", FarTypeId::F64},
    {"quad", FarTypeId::F128},
    {"float128", FarTypeId::F128},
    // strings / pointers
    {"str", FarTypeId::String},
    {"pointer", FarTypeId::Ptr},
    // vectors & geometry (short names)
    {"vec2", FarTypeId::FVec2},
    {"vec3", FarTypeId::FVec3},
    {"vec4", FarTypeId::FVec4},
    {"point", FarTypeId::FPoint},
    {"rect", FarTypeId::FRect},
    {"quaternion", FarTypeId::Quat},
};

const FarTypeInfo& typeInfo(FarTypeId id) {
  return kTypes[static_cast<size_t>(id)];
}

const FarTypeInfo* lookupType(const std::string& name) {
  for (const auto& t : kTypes) {
    if (name == t.name)
      return &t;
  }
  auto alias = kTypeAliases.find(name);
  if (alias != kTypeAliases.end())
    return &typeInfo(alias->second);
  return nullptr;
}

bool isTypeName(const std::string& name) {
  return lookupType(name) != nullptr;
}

FarTypeId parseTypeName(const std::string& name) {
  const FarTypeInfo* t = lookupType(name);
  if (!t)
    throw FarError("unknown type '" + name + "'");
  return t->id;
}

bool isNumericType(FarTypeId id) {
  return isIntegerType(id) || isFloatType(id) || id == FarTypeId::Bool || id == FarTypeId::Char;
}

bool isIntegerType(FarTypeId id) {
  return typeInfo(id).is_integer;
}

bool isFloatType(FarTypeId id) {
  return typeInfo(id).is_float;
}

bool isStringType(FarTypeId id) {
  return id == FarTypeId::String || id == FarTypeId::RawString;
}

bool isVoidType(FarTypeId id) {
  return id == FarTypeId::Void;
}

bool canAssign(FarTypeId from, FarTypeId to) {
  if (from == to)
    return true;
  if (isStringType(from) && isStringType(to))
    return true;
  if (isIntegerType(from) && isIntegerType(to))
    return true;
  if (to == FarTypeId::F64 && (isFloatType(from) || isIntegerType(from)))
    return true;
  if (to == FarTypeId::F32 && (from == FarTypeId::F32 || from == FarTypeId::F16 || isIntegerType(from)))
    return true;
  if (to == FarTypeId::F16 && (from == FarTypeId::F16 || isIntegerType(from)))
    return true;
  if (to == FarTypeId::Bool && isIntegerType(from))
    return true;
  if (to == FarTypeId::Char && (isIntegerType(from) || from == FarTypeId::Char))
    return true;
  if (from == FarTypeId::Char && (isIntegerType(to) || to == FarTypeId::Char))
    return true;
  if ((from == FarTypeId::Ptr || from == FarTypeId::Ref) && (to == FarTypeId::Ptr || to == FarTypeId::Ref))
    return true;
  if (to == FarTypeId::Any)
    return true;
  if (isAggregateType(from) && isAggregateType(to) && from == to)
    return true;
  if (isPointFamily(from) && isVecFamily(to) && aggregateScalar(from) == aggregateScalar(to))
    return true;
  if (isVecFamily(from) && isPointFamily(to) && aggregateScalar(from) == aggregateScalar(to))
    return true;
  if (isRectFamily(to) && isVecFamily(from) && aggregateScalar(from) == FarTypeId::F32 && aggregateDim(from) == 2)
    return true;
  return false;
}

}  // namespace far
