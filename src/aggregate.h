#pragma once

#include "types.h"

#include <cstdint>
#include <string>
#include <vector>

namespace far {

enum class AggregateKind { Vec, Point, Rect, IVec, Mat, Quat, Color, Color32, Transform, Bounds };

enum class AggMethodId {
  Length,
  Length2,
  Dot,
  Distance,
  Distance2,
  Normalize,
  Min,
  Max,
  Clamp,
  ApproxEq,
  Cross,
  DistanceTo,
  Translate,
  Width,
  Height,
  Center,
  Contains,
  Intersects,
  Expand,
  Area,
  Transpose,
  Determinant,
  MatMul,
  QuatMul,
  ToColor,
  BoundsSize,
};

struct AggregateMeta {
  FarTypeId id;
  const char* name;
  const char* llvm_name;
  AggregateKind kind;
  FarTypeId scalar;
  int nfields;
  const char* fields[16];
  const char* print_rt;  // runtime print suffix, e.g. "Vec2" -> far_print_Vec2
};

struct ConstructorInfo {
  const char* name;
  FarTypeId ret;
  int nargs;
};

struct MethodInfo {
  AggMethodId id;
  const char* name;
  int nargs;  // excluding receiver
  FarTypeId ret;
};

const AggregateMeta* aggregateMeta(FarTypeId id);
bool isAggregateType(FarTypeId id);
AggregateKind aggregateKind(FarTypeId id);
FarTypeId aggregateScalar(FarTypeId id);
int aggregateDim(FarTypeId id);
FarTypeId vecTypeForDim(FarTypeId scalar, int dim);
FarTypeId pointTypeForScalar(FarTypeId scalar);
FarTypeId rectTypeForScalar(FarTypeId scalar);
bool isVecFamily(FarTypeId id);
bool isPointFamily(FarTypeId id);
bool isRectFamily(FarTypeId id);
bool isIVecFamily(FarTypeId id);
bool isMatFamily(FarTypeId id);
bool isQuatFamily(FarTypeId id);
bool isColorFamily(FarTypeId id);
bool isBoundsFamily(FarTypeId id);
bool sameScalarFamily(FarTypeId a, FarTypeId b);
FarTypeId matTypeForDim(FarTypeId scalar, int dim);
int aggregateMatDim(FarTypeId id);
FarTypeId quatTypeForScalar(FarTypeId scalar);

const ConstructorInfo* lookupConstructor(const std::string& name);
int lookupFieldIndex(FarTypeId type, const std::string& field);
const MethodInfo* lookupMethod(FarTypeId type, const std::string& name);

FarTypeId checkAggregateBinOp(const std::string& op, FarTypeId lt, FarTypeId rt);
FarTypeId checkUnaryAggregateOp(const std::string& op, FarTypeId ty);

void declareAggregateTypes(std::ostringstream& out);
void declareAggregateRuntime(std::ostringstream& out);

}  // namespace far
