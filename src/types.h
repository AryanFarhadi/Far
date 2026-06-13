#pragma once

#include <cstdint>
#include <string>

namespace far {

enum class FarTypeId {
  I8,
  I16,
  I32,
  I64,
  I128,
  U8,
  U16,
  U32,
  U64,
  U128,
  F16,
  F32,
  F64,
  F128,
  Bool,
  Char,
  String,
  RawString,
  Ptr,
  Ref,
  Any,
  Void,
  Arr,
  FVec2,
  FVec3,
  FVec4,
  DVec2,
  DVec3,
  DVec4,
  FPoint,
  DPoint,
  FRect,
  DRect,
  IVec2,
  IVec3,
  IVec4,
  Mat2,
  Mat3,
  Mat4,
  DMat2,
  DMat3,
  DMat4,
  Quat,
  DQuat,
  Color,
  Color32,
  Transform,
  Bounds,
};

struct FarTypeInfo {
  FarTypeId id;
  const char* name;
  const char* llvm;
  bool is_signed;
  bool is_float;
  bool is_integer;
  int bits;
  int64_t min_i;
  int64_t max_i;
  double min_f;
  double max_f;
};

const FarTypeInfo& typeInfo(FarTypeId id);
const FarTypeInfo* lookupType(const std::string& name);
bool isTypeName(const std::string& name);
FarTypeId parseTypeName(const std::string& name);
bool isNumericType(FarTypeId id);
bool isIntegerType(FarTypeId id);
bool isFloatType(FarTypeId id);
bool isStringType(FarTypeId id);
bool isVoidType(FarTypeId id);
bool canAssign(FarTypeId from, FarTypeId to);

}  // namespace far
