#include "aggregate.h"

#include "error.h"

#include <sstream>
#include <unordered_map>

namespace far {

static const AggregateMeta kAggregates[] = {
    {FarTypeId::FVec2, "fvec2", "%FarFVec2", AggregateKind::Vec, FarTypeId::F32, 2, {"x", "y"}, "FVec2"},
    {FarTypeId::FVec3, "fvec3", "%FarFVec3", AggregateKind::Vec, FarTypeId::F32, 3, {"x", "y", "z"}, "FVec3"},
    {FarTypeId::FVec4, "fvec4", "%FarFVec4", AggregateKind::Vec, FarTypeId::F32, 4, {"x", "y", "z", "w"}, "FVec4"},
    {FarTypeId::DVec2, "dvec2", "%FarDVec2", AggregateKind::Vec, FarTypeId::F64, 2, {"x", "y"}, "DVec2"},
    {FarTypeId::DVec3, "dvec3", "%FarDVec3", AggregateKind::Vec, FarTypeId::F64, 3, {"x", "y", "z"}, "DVec3"},
    {FarTypeId::DVec4, "dvec4", "%FarDVec4", AggregateKind::Vec, FarTypeId::F64, 4, {"x", "y", "z", "w"}, "DVec4"},
    {FarTypeId::FPoint, "fpoint", "%FarFPoint", AggregateKind::Point, FarTypeId::F32, 2, {"x", "y"}, "FPoint"},
    {FarTypeId::DPoint, "dpoint", "%FarDPoint", AggregateKind::Point, FarTypeId::F64, 2, {"x", "y"}, "DPoint"},
    {FarTypeId::FRect, "frect", "%FarFRect", AggregateKind::Rect, FarTypeId::F32, 4,
     {"xmin", "ymin", "xmax", "ymax"}, "FRect"},
    {FarTypeId::DRect, "drect", "%FarDRect", AggregateKind::Rect, FarTypeId::F64, 4,
     {"xmin", "ymin", "xmax", "ymax"}, "DRect"},
    {FarTypeId::IVec2, "ivec2", "%FarIVec2", AggregateKind::IVec, FarTypeId::I32, 2, {"x", "y"}, "IVec2"},
    {FarTypeId::IVec3, "ivec3", "%FarIVec3", AggregateKind::IVec, FarTypeId::I32, 3, {"x", "y", "z"}, "IVec3"},
    {FarTypeId::IVec4, "ivec4", "%FarIVec4", AggregateKind::IVec, FarTypeId::I32, 4, {"x", "y", "z", "w"}, "IVec4"},
    {FarTypeId::Mat2, "mat2", "%FarMat2", AggregateKind::Mat, FarTypeId::F32, 4, {"m00", "m01", "m10", "m11"},
     "Mat2"},
    {FarTypeId::Mat3, "mat3", "%FarMat3", AggregateKind::Mat, FarTypeId::F32, 9,
     {"m00", "m01", "m02", "m10", "m11", "m12", "m20", "m21", "m22"}, "Mat3"},
    {FarTypeId::Mat4, "mat4", "%FarMat4", AggregateKind::Mat, FarTypeId::F32, 16,
     {"m00", "m01", "m02", "m03", "m10", "m11", "m12", "m13", "m20", "m21", "m22", "m23", "m30", "m31", "m32", "m33"},
     "Mat4"},
    {FarTypeId::DMat2, "dmat2", "%FarDMat2", AggregateKind::Mat, FarTypeId::F64, 4, {"m00", "m01", "m10", "m11"},
     "DMat2"},
    {FarTypeId::DMat3, "dmat3", "%FarDMat3", AggregateKind::Mat, FarTypeId::F64, 9,
     {"m00", "m01", "m02", "m10", "m11", "m12", "m20", "m21", "m22"}, "DMat3"},
    {FarTypeId::DMat4, "dmat4", "%FarDMat4", AggregateKind::Mat, FarTypeId::F64, 16,
     {"m00", "m01", "m02", "m03", "m10", "m11", "m12", "m13", "m20", "m21", "m22", "m23", "m30", "m31", "m32", "m33"},
     "DMat4"},
    {FarTypeId::Quat, "quat", "%FarQuat", AggregateKind::Quat, FarTypeId::F32, 4, {"x", "y", "z", "w"}, "Quat"},
    {FarTypeId::DQuat, "dquat", "%FarDQuat", AggregateKind::Quat, FarTypeId::F64, 4, {"x", "y", "z", "w"}, "DQuat"},
    {FarTypeId::Color, "color", "%FarColor", AggregateKind::Color, FarTypeId::F32, 4, {"r", "g", "b", "a"}, "Color"},
    {FarTypeId::Color32, "color32", "%FarColor32", AggregateKind::Color32, FarTypeId::U8, 4, {"r", "g", "b", "a"},
     "Color32"},
    {FarTypeId::Transform, "transform", "%FarTransform", AggregateKind::Transform, FarTypeId::F32, 10,
     {"px", "py", "pz", "qx", "qy", "qz", "qw", "sx", "sy", "sz"}, "Transform"},
    {FarTypeId::Bounds, "bounds", "%FarBounds", AggregateKind::Bounds, FarTypeId::F32, 6,
     {"min_x", "min_y", "min_z", "max_x", "max_y", "max_z"}, "Bounds"},
};

static const ConstructorInfo kConstructors[] = {
    {"fvec2", FarTypeId::FVec2, 2},   {"vec2", FarTypeId::FVec2, 2},
    {"fvec3", FarTypeId::FVec3, 3},   {"vec3", FarTypeId::FVec3, 3},
    {"fvec4", FarTypeId::FVec4, 4},   {"vec4", FarTypeId::FVec4, 4},
    {"dvec2", FarTypeId::DVec2, 2},   {"dvec3", FarTypeId::DVec3, 3},
    {"dvec4", FarTypeId::DVec4, 4},
    {"fpoint", FarTypeId::FPoint, 2}, {"dpoint", FarTypeId::DPoint, 2},
    {"frect", FarTypeId::FRect, 4},   {"rect", FarTypeId::FRect, 4},
    {"drect", FarTypeId::DRect, 4},
    {"ivec2", FarTypeId::IVec2, 2},  {"ivec3", FarTypeId::IVec3, 3},
    {"ivec4", FarTypeId::IVec4, 4},
    {"mat2", FarTypeId::Mat2, 4},     {"mat3", FarTypeId::Mat3, 9},
    {"mat4", FarTypeId::Mat4, 16},
    {"dmat2", FarTypeId::DMat2, 4},   {"dmat3", FarTypeId::DMat3, 9},
    {"dmat4", FarTypeId::DMat4, 16},
    {"quat", FarTypeId::Quat, 4},     {"dquat", FarTypeId::DQuat, 4},
    {"color", FarTypeId::Color, 4},   {"color32", FarTypeId::Color32, 4},
    {"transform", FarTypeId::Transform, 10},
    {"bounds", FarTypeId::Bounds, 6},
};

static const MethodInfo kVecMethods[] = {
    {AggMethodId::Length, "length", 0, FarTypeId::F64},
    {AggMethodId::Length2, "length2", 0, FarTypeId::F64},
    {AggMethodId::Dot, "dot", 1, FarTypeId::F64},
    {AggMethodId::Distance, "distance", 1, FarTypeId::F64},
    {AggMethodId::Distance2, "distance2", 1, FarTypeId::F64},
    {AggMethodId::Normalize, "normalize", 0, FarTypeId::DVec2},
    {AggMethodId::Min, "min", 1, FarTypeId::DVec2},
    {AggMethodId::Max, "max", 1, FarTypeId::DVec2},
    {AggMethodId::Clamp, "clamp", 2, FarTypeId::DVec2},
    {AggMethodId::ApproxEq, "approx_eq", 2, FarTypeId::Bool},
    {AggMethodId::Cross, "cross", 1, FarTypeId::DVec3},
};

static const MethodInfo kIVecMethods[] = {
    {AggMethodId::Length, "length", 0, FarTypeId::F64},
    {AggMethodId::Length2, "length2", 0, FarTypeId::I64},
    {AggMethodId::Dot, "dot", 1, FarTypeId::I64},
    {AggMethodId::Min, "min", 1, FarTypeId::IVec2},
    {AggMethodId::Max, "max", 1, FarTypeId::IVec2},
    {AggMethodId::Cross, "cross", 1, FarTypeId::IVec3},
};

static const MethodInfo kPointMethods[] = {
    {AggMethodId::Length, "length", 0, FarTypeId::F64},
    {AggMethodId::Length2, "length2", 0, FarTypeId::F64},
    {AggMethodId::Dot, "dot", 1, FarTypeId::F64},
    {AggMethodId::Distance, "distance", 1, FarTypeId::F64},
    {AggMethodId::Distance2, "distance2", 1, FarTypeId::F64},
    {AggMethodId::Normalize, "normalize", 0, FarTypeId::DPoint},
    {AggMethodId::Min, "min", 1, FarTypeId::DPoint},
    {AggMethodId::Max, "max", 1, FarTypeId::DPoint},
    {AggMethodId::Clamp, "clamp", 2, FarTypeId::DPoint},
    {AggMethodId::ApproxEq, "approx_eq", 2, FarTypeId::Bool},
    {AggMethodId::DistanceTo, "distance_to", 1, FarTypeId::F64},
    {AggMethodId::Translate, "translate", 1, FarTypeId::DPoint},
};

static const MethodInfo kRectMethods[] = {
    {AggMethodId::Width, "width", 0, FarTypeId::F64},
    {AggMethodId::Height, "height", 0, FarTypeId::F64},
    {AggMethodId::Center, "center", 0, FarTypeId::DPoint},
    {AggMethodId::Contains, "contains", 1, FarTypeId::Bool},
    {AggMethodId::Intersects, "intersects", 1, FarTypeId::Bool},
    {AggMethodId::Expand, "expand", 1, FarTypeId::DRect},
    {AggMethodId::Area, "area", 0, FarTypeId::F64},
};

static const MethodInfo kMatMethods[] = {
    {AggMethodId::Transpose, "transpose", 0, FarTypeId::Mat2},
    {AggMethodId::Determinant, "determinant", 0, FarTypeId::F64},
    {AggMethodId::MatMul, "mul", 1, FarTypeId::Mat2},
};

static const MethodInfo kQuatMethods[] = {
    {AggMethodId::Length, "length", 0, FarTypeId::F64},
    {AggMethodId::Length2, "length2", 0, FarTypeId::F64},
    {AggMethodId::Dot, "dot", 1, FarTypeId::F64},
    {AggMethodId::Normalize, "normalize", 0, FarTypeId::Quat},
    {AggMethodId::QuatMul, "mul", 1, FarTypeId::Quat},
};

static const MethodInfo kColorMethods[] = {
    {AggMethodId::Min, "min", 1, FarTypeId::Color},
    {AggMethodId::Max, "max", 1, FarTypeId::Color},
    {AggMethodId::Clamp, "clamp", 2, FarTypeId::Color},
    {AggMethodId::ApproxEq, "approx_eq", 2, FarTypeId::Bool},
};

static const MethodInfo kColor32Methods[] = {
    {AggMethodId::ToColor, "to_color", 0, FarTypeId::Color},
};

static const MethodInfo kBoundsMethods[] = {
    {AggMethodId::Contains, "contains", 1, FarTypeId::Bool},
    {AggMethodId::Intersects, "intersects", 1, FarTypeId::Bool},
    {AggMethodId::Expand, "expand", 1, FarTypeId::Bounds},
    {AggMethodId::Center, "center", 0, FarTypeId::FVec3},
    {AggMethodId::BoundsSize, "size", 0, FarTypeId::FVec3},
};

const AggregateMeta* aggregateMeta(FarTypeId id) {
  for (const auto& m : kAggregates) {
    if (m.id == id)
      return &m;
  }
  return nullptr;
}

bool isAggregateType(FarTypeId id) { return aggregateMeta(id) != nullptr; }

AggregateKind aggregateKind(FarTypeId id) {
  const AggregateMeta* m = aggregateMeta(id);
  return m ? m->kind : AggregateKind::Vec;
}

FarTypeId aggregateScalar(FarTypeId id) {
  const AggregateMeta* m = aggregateMeta(id);
  return m ? m->scalar : FarTypeId::F64;
}

int aggregateDim(FarTypeId id) {
  const AggregateMeta* m = aggregateMeta(id);
  return m ? m->nfields : 0;
}

int aggregateMatDim(FarTypeId id) {
  if (id == FarTypeId::Mat2 || id == FarTypeId::DMat2)
    return 2;
  if (id == FarTypeId::Mat3 || id == FarTypeId::DMat3)
    return 3;
  if (id == FarTypeId::Mat4 || id == FarTypeId::DMat4)
    return 4;
  return 0;
}

FarTypeId vecTypeForDim(FarTypeId scalar, int dim) {
  if (scalar == FarTypeId::I32) {
    if (dim == 2)
      return FarTypeId::IVec2;
    if (dim == 3)
      return FarTypeId::IVec3;
    if (dim == 4)
      return FarTypeId::IVec4;
    throw FarError("invalid ivec dimension");
  }
  if (scalar == FarTypeId::F32) {
    if (dim == 2)
      return FarTypeId::FVec2;
    if (dim == 3)
      return FarTypeId::FVec3;
    if (dim == 4)
      return FarTypeId::FVec4;
  } else if (scalar == FarTypeId::F64) {
    if (dim == 2)
      return FarTypeId::DVec2;
    if (dim == 3)
      return FarTypeId::DVec3;
    if (dim == 4)
      return FarTypeId::DVec4;
  }
  throw FarError("invalid vector dimension");
}

FarTypeId matTypeForDim(FarTypeId scalar, int dim) {
  if (scalar == FarTypeId::F32) {
    if (dim == 2)
      return FarTypeId::Mat2;
    if (dim == 3)
      return FarTypeId::Mat3;
    if (dim == 4)
      return FarTypeId::Mat4;
  } else if (scalar == FarTypeId::F64) {
    if (dim == 2)
      return FarTypeId::DMat2;
    if (dim == 3)
      return FarTypeId::DMat3;
    if (dim == 4)
      return FarTypeId::DMat4;
  }
  throw FarError("invalid matrix dimension");
}

FarTypeId quatTypeForScalar(FarTypeId scalar) {
  return scalar == FarTypeId::F64 ? FarTypeId::DQuat : FarTypeId::Quat;
}

FarTypeId pointTypeForScalar(FarTypeId scalar) {
  return scalar == FarTypeId::F32 ? FarTypeId::FPoint : FarTypeId::DPoint;
}

FarTypeId rectTypeForScalar(FarTypeId scalar) {
  return scalar == FarTypeId::F32 ? FarTypeId::FRect : FarTypeId::DRect;
}

bool isVecFamily(FarTypeId id) { return isAggregateType(id) && aggregateKind(id) == AggregateKind::Vec; }
bool isPointFamily(FarTypeId id) { return isAggregateType(id) && aggregateKind(id) == AggregateKind::Point; }
bool isRectFamily(FarTypeId id) { return isAggregateType(id) && aggregateKind(id) == AggregateKind::Rect; }
bool isIVecFamily(FarTypeId id) { return isAggregateType(id) && aggregateKind(id) == AggregateKind::IVec; }
bool isMatFamily(FarTypeId id) { return isAggregateType(id) && aggregateKind(id) == AggregateKind::Mat; }
bool isQuatFamily(FarTypeId id) { return isAggregateType(id) && aggregateKind(id) == AggregateKind::Quat; }
bool isColorFamily(FarTypeId id) {
  return isAggregateType(id) &&
         (aggregateKind(id) == AggregateKind::Color || aggregateKind(id) == AggregateKind::Color32);
}
bool isBoundsFamily(FarTypeId id) { return isAggregateType(id) && aggregateKind(id) == AggregateKind::Bounds; }

bool sameScalarFamily(FarTypeId a, FarTypeId b) {
  if (isAggregateType(a) && isAggregateType(b))
    return aggregateScalar(a) == aggregateScalar(b);
  if (isAggregateType(a))
    return aggregateScalar(a) == b || (b == FarTypeId::I64 && aggregateScalar(a) == FarTypeId::F64);
  if (isAggregateType(b))
    return aggregateScalar(b) == a || (a == FarTypeId::I64 && aggregateScalar(b) == FarTypeId::F64);
  return false;
}

const ConstructorInfo* lookupConstructor(const std::string& name) {
  for (const auto& c : kConstructors) {
    if (name == c.name)
      return &c;
  }
  return nullptr;
}

int lookupFieldIndex(FarTypeId type, const std::string& field) {
  const AggregateMeta* m = aggregateMeta(type);
  if (!m)
    return -1;
  for (int i = 0; i < m->nfields; ++i) {
    if (field == m->fields[i])
      return i;
  }
  return -1;
}

static FarTypeId methodRetFor(FarTypeId recv, const MethodInfo& mi) {
  if (mi.id == AggMethodId::Normalize || mi.id == AggMethodId::Min || mi.id == AggMethodId::Max ||
      mi.id == AggMethodId::Clamp || mi.id == AggMethodId::Transpose || mi.id == AggMethodId::QuatMul ||
      mi.id == AggMethodId::MatMul)
    return recv;
  if (mi.id == AggMethodId::Cross) {
    if (isIVecFamily(recv))
      return FarTypeId::IVec3;
    return aggregateScalar(recv) == FarTypeId::F32 ? FarTypeId::FVec3 : FarTypeId::DVec3;
  }
  if (mi.id == AggMethodId::Translate || mi.id == AggMethodId::Center) {
    if (isBoundsFamily(recv))
      return FarTypeId::FVec3;
    return pointTypeForScalar(aggregateScalar(recv));
  }
  if (mi.id == AggMethodId::Expand) {
    if (isBoundsFamily(recv))
      return FarTypeId::Bounds;
    return rectTypeForScalar(aggregateScalar(recv));
  }
  if (mi.id == AggMethodId::Length2 && isIVecFamily(recv))
    return FarTypeId::I64;
  if (mi.id == AggMethodId::Dot && isIVecFamily(recv))
    return FarTypeId::I64;
  if (mi.id == AggMethodId::Min || mi.id == AggMethodId::Max || mi.id == AggMethodId::Clamp) {
    if (isIVecFamily(recv))
      return recv;
    if (isColorFamily(recv) && aggregateKind(recv) == AggregateKind::Color)
      return FarTypeId::Color;
  }
  if (mi.id == AggMethodId::BoundsSize)
    return FarTypeId::FVec3;
  return mi.ret;
}

const MethodInfo* lookupMethod(FarTypeId type, const std::string& name) {
  const AggregateMeta* m = aggregateMeta(type);
  if (!m)
    return nullptr;

  const MethodInfo* table = nullptr;
  size_t count = 0;
  switch (m->kind) {
    case AggregateKind::Vec:
      table = kVecMethods;
      count = sizeof(kVecMethods) / sizeof(kVecMethods[0]);
      break;
    case AggregateKind::IVec:
      table = kIVecMethods;
      count = sizeof(kIVecMethods) / sizeof(kIVecMethods[0]);
      break;
    case AggregateKind::Point:
      table = kPointMethods;
      count = sizeof(kPointMethods) / sizeof(kPointMethods[0]);
      break;
    case AggregateKind::Rect:
      table = kRectMethods;
      count = sizeof(kRectMethods) / sizeof(kRectMethods[0]);
      break;
    case AggregateKind::Mat:
      table = kMatMethods;
      count = sizeof(kMatMethods) / sizeof(kMatMethods[0]);
      break;
    case AggregateKind::Quat:
      table = kQuatMethods;
      count = sizeof(kQuatMethods) / sizeof(kQuatMethods[0]);
      break;
    case AggregateKind::Color:
      table = kColorMethods;
      count = sizeof(kColorMethods) / sizeof(kColorMethods[0]);
      break;
    case AggregateKind::Color32:
      table = kColor32Methods;
      count = sizeof(kColor32Methods) / sizeof(kColor32Methods[0]);
      break;
    case AggregateKind::Bounds:
      table = kBoundsMethods;
      count = sizeof(kBoundsMethods) / sizeof(kBoundsMethods[0]);
      break;
    default:
      return nullptr;
  }

  for (size_t i = 0; i < count; ++i) {
    if (name == table[i].name) {
      static MethodInfo resolved;
      resolved = table[i];
      resolved.ret = methodRetFor(type, table[i]);
      return &resolved;
    }
  }
  return nullptr;
}

static bool isScalarForAgg(FarTypeId t, FarTypeId scalar) {
  if (t == scalar)
    return true;
  if (scalar == FarTypeId::I32 && (t == FarTypeId::I32 || t == FarTypeId::I64))
    return true;
  if (scalar == FarTypeId::U8 && isIntegerType(t))
    return true;
  if (scalar == FarTypeId::F64 && (t == FarTypeId::I64 || t == FarTypeId::I32 || t == FarTypeId::F32))
    return true;
  if (scalar == FarTypeId::F32 && (t == FarTypeId::I64 || t == FarTypeId::I32))
    return true;
  return false;
}

static FarTypeId vecForPoint(FarTypeId point) {
  return vecTypeForDim(aggregateScalar(point), 2);
}

static bool isComponentWiseFamily(FarTypeId id) {
  return isVecFamily(id) || isIVecFamily(id) || isColorFamily(id) ||
         aggregateKind(id) == AggregateKind::Color32;
}

FarTypeId checkAggregateBinOp(const std::string& op, FarTypeId lt, FarTypeId rt) {
  const bool la = isAggregateType(lt);
  const bool ra = isAggregateType(rt);

  if (op == "==" || op == "!=") {
    if (la && ra && lt == rt)
      return FarTypeId::Bool;
    throw FarError("incompatible types for '" + op + "'");
  }

  if (op == "and" || op == "or" || op == "<" || op == ">" || op == "<=" || op == ">=")
    throw FarError("operator '" + op + "' not supported on aggregate types");

  if (la && ra) {
    if (isPointFamily(lt) && isPointFamily(rt)) {
      if (op == "-")
        return vecForPoint(lt);
      throw FarError("operator '" + op + "' not supported between two points");
    }
    if (isPointFamily(lt) && isVecFamily(rt) && aggregateScalar(lt) == aggregateScalar(rt)) {
      if (op == "+" || op == "-")
        return lt;
      throw FarError("operator '" + op + "' not supported between point and vector");
    }
    if (isVecFamily(lt) && isPointFamily(rt) && op == "+")
      return rt;
    if (isRectFamily(lt) && isRectFamily(rt) && lt == rt) {
      if (op == "+" || op == "-" || op == "*" || op == "/" || op == "**" || op == "//")
        return lt;
    }
    if (lt == rt && (isComponentWiseFamily(lt) || isRectFamily(lt))) {
      if (op == "+" || op == "-" || op == "*" || op == "/" || op == "**" || op == "//")
        return lt;
    }
    throw FarError("incompatible aggregate operands for '" + op + "'");
  }

  if (la && !ra) {
    FarTypeId sc = aggregateScalar(lt);
    if (isComponentWiseFamily(lt) && isScalarForAgg(rt, sc)) {
      if (op == "+" || op == "-" || op == "*" || op == "/")
        return lt;
    }
    throw FarError("incompatible operands for '" + op + "'");
  }

  if (!la && ra) {
    FarTypeId sc = aggregateScalar(rt);
    if (isComponentWiseFamily(rt) && isScalarForAgg(lt, sc) &&
        (op == "+" || op == "-" || op == "*" || op == "/"))
      return rt;
    throw FarError("incompatible operands for '" + op + "'");
  }

  throw FarError("internal aggregate binop error");
}

FarTypeId checkUnaryAggregateOp(const std::string& op, FarTypeId ty) {
  if (!isAggregateType(ty))
    throw FarError("expected aggregate type");
  if ((isRectFamily(ty) || isMatFamily(ty) || isBoundsFamily(ty) || aggregateKind(ty) == AggregateKind::Transform) &&
      op == "~")
    throw FarError("unary ~ not supported on this type");
  if (isComponentWiseFamily(ty) && (op == "-" || op == "~"))
    return ty;
  if (isVecFamily(ty) && (op == "-" || op == "~"))
    return ty;
  throw FarError("unary operator '" + op + "' not supported on aggregate types");
}

void declareAggregateTypes(std::ostringstream& out) {
  out << "%FarFVec2 = type { float, float }\n";
  out << "%FarFVec3 = type { float, float, float }\n";
  out << "%FarFVec4 = type { float, float, float, float }\n";
  out << "%FarDVec2 = type { double, double }\n";
  out << "%FarDVec3 = type { double, double, double }\n";
  out << "%FarDVec4 = type { double, double, double, double }\n";
  out << "%FarFPoint = type { float, float }\n";
  out << "%FarDPoint = type { double, double }\n";
  out << "%FarFRect = type { float, float, float, float }\n";
  out << "%FarDRect = type { double, double, double, double }\n";
  out << "%FarIVec2 = type { i32, i32 }\n";
  out << "%FarIVec3 = type { i32, i32, i32 }\n";
  out << "%FarIVec4 = type { i32, i32, i32, i32 }\n";
  out << "%FarMat2 = type { float, float, float, float }\n";
  out << "%FarMat3 = type { float, float, float, float, float, float, float, float, float }\n";
  out << "%FarMat4 = type { float, float, float, float, float, float, float, float, float, float, float, float, float, float, float, float }\n";
  out << "%FarDMat2 = type { double, double, double, double }\n";
  out << "%FarDMat3 = type { double, double, double, double, double, double, double, double, double }\n";
  out << "%FarDMat4 = type { double, double, double, double, double, double, double, double, double, double, double, double, double, double, double, double }\n";
  out << "%FarQuat = type { float, float, float, float }\n";
  out << "%FarDQuat = type { double, double, double, double }\n";
  out << "%FarColor = type { float, float, float, float }\n";
  out << "%FarColor32 = type { i8, i8, i8, i8 }\n";
  out << "%FarTransform = type { float, float, float, float, float, float, float, float, float, float }\n";
  out << "%FarBounds = type { float, float, float, float, float, float }\n";
}

void declareAggregateRuntime(std::ostringstream& out) {
  for (const auto& m : kAggregates)
    out << "declare void @far_print_" << m.print_rt << "(" << m.llvm_name << "*)\n";

  const char* vec2[] = {"fvec2", "dvec2", "fpoint", "dpoint"};
  const char* vec2ty[] = {"%FarFVec2", "%FarDVec2", "%FarFPoint", "%FarDPoint"};
  for (int i = 0; i < 4; ++i) {
    out << "declare double @far_" << vec2[i] << "_dot(" << vec2ty[i] << "*, " << vec2ty[i] << "*)\n";
    out << "declare double @far_" << vec2[i] << "_length2(" << vec2ty[i] << "*)\n";
    out << "declare double @far_" << vec2[i] << "_length(" << vec2ty[i] << "*)\n";
    out << "declare double @far_" << vec2[i] << "_distance2(" << vec2ty[i] << "*, " << vec2ty[i] << "*)\n";
    out << "declare double @far_" << vec2[i] << "_distance(" << vec2ty[i] << "*, " << vec2ty[i] << "*)\n";
    out << "declare void @far_" << vec2[i] << "_normalize(" << vec2ty[i] << "*, " << vec2ty[i] << "*)\n";
  }
  out << "declare i64 @far_ivec2_dot(%FarIVec2*, %FarIVec2*)\n";
  out << "declare i64 @far_ivec2_length2(%FarIVec2*)\n";
  out << "declare double @far_ivec2_length(%FarIVec2*)\n";
  out << "declare i64 @far_ivec3_dot(%FarIVec3*, %FarIVec3*)\n";
  out << "declare i64 @far_ivec3_length2(%FarIVec3*)\n";
  out << "declare double @far_ivec3_length(%FarIVec3*)\n";
  out << "declare void @far_ivec3_cross(%FarIVec3*, %FarIVec3*, %FarIVec3*)\n";
  out << "declare i64 @far_ivec4_dot(%FarIVec4*, %FarIVec4*)\n";
  out << "declare i64 @far_ivec4_length2(%FarIVec4*)\n";
  out << "declare double @far_ivec4_length(%FarIVec4*)\n";

  out << "declare double @far_fpoint_distance_to(%FarFPoint*, %FarFPoint*)\n";
  out << "declare double @far_dpoint_distance_to(%FarDPoint*, %FarDPoint*)\n";

  const char* vec3[] = {"fvec3", "dvec3"};
  const char* vec3ty[] = {"%FarFVec3", "%FarDVec3"};
  for (int i = 0; i < 2; ++i) {
    out << "declare double @far_" << vec3[i] << "_dot(" << vec3ty[i] << "*, " << vec3ty[i] << "*)\n";
    out << "declare double @far_" << vec3[i] << "_length2(" << vec3ty[i] << "*)\n";
    out << "declare double @far_" << vec3[i] << "_length(" << vec3ty[i] << "*)\n";
    out << "declare double @far_" << vec3[i] << "_distance2(" << vec3ty[i] << "*, " << vec3ty[i] << "*)\n";
    out << "declare double @far_" << vec3[i] << "_distance(" << vec3ty[i] << "*, " << vec3ty[i] << "*)\n";
    out << "declare void @far_" << vec3[i] << "_normalize(" << vec3ty[i] << "*, " << vec3ty[i] << "*)\n";
    out << "declare void @far_" << vec3[i] << "_cross(" << vec3ty[i] << "*, " << vec3ty[i] << "*, " << vec3ty[i]
        << "*)\n";
  }

  const char* vec4[] = {"fvec4", "dvec4"};
  const char* vec4ty[] = {"%FarFVec4", "%FarDVec4"};
  for (int i = 0; i < 2; ++i) {
    out << "declare double @far_" << vec4[i] << "_dot(" << vec4ty[i] << "*, " << vec4ty[i] << "*)\n";
    out << "declare double @far_" << vec4[i] << "_length2(" << vec4ty[i] << "*)\n";
    out << "declare double @far_" << vec4[i] << "_length(" << vec4ty[i] << "*)\n";
    out << "declare double @far_" << vec4[i] << "_distance2(" << vec4ty[i] << "*, " << vec4ty[i] << "*)\n";
    out << "declare double @far_" << vec4[i] << "_distance(" << vec4ty[i] << "*, " << vec4ty[i] << "*)\n";
    out << "declare void @far_" << vec4[i] << "_normalize(" << vec4ty[i] << "*, " << vec4ty[i] << "*)\n";
  }

  const char* mats[] = {"mat2", "mat3", "mat4", "dmat2", "dmat3", "dmat4"};
  const char* matty[] = {"%FarMat2", "%FarMat3", "%FarMat4", "%FarDMat2", "%FarDMat3", "%FarDMat4"};
  for (int i = 0; i < 6; ++i) {
    out << "declare void @far_" << mats[i] << "_transpose(" << matty[i] << "*, " << matty[i] << "*)\n";
    out << "declare double @far_" << mats[i] << "_determinant(" << matty[i] << "*)\n";
    out << "declare void @far_" << mats[i] << "_mul_mat(" << matty[i] << "*, " << matty[i] << "*, " << matty[i]
        << "*)\n";
  }
  out << "declare void @far_mat2_mul_vec(%FarMat2*, %FarFVec2*, %FarFVec2*)\n";
  out << "declare void @far_mat3_mul_vec(%FarMat3*, %FarFVec3*, %FarFVec3*)\n";
  out << "declare void @far_mat4_mul_vec(%FarMat4*, %FarFVec4*, %FarFVec4*)\n";
  out << "declare void @far_dmat2_mul_vec(%FarDMat2*, %FarDVec2*, %FarDVec2*)\n";
  out << "declare void @far_dmat3_mul_vec(%FarDMat3*, %FarDVec3*, %FarDVec3*)\n";
  out << "declare void @far_dmat4_mul_vec(%FarDMat4*, %FarDVec4*, %FarDVec4*)\n";

  const char* quats[] = {"quat", "dquat"};
  const char* quatty[] = {"%FarQuat", "%FarDQuat"};
  for (int i = 0; i < 2; ++i) {
    out << "declare double @far_" << quats[i] << "_dot(" << quatty[i] << "*, " << quatty[i] << "*)\n";
    out << "declare double @far_" << quats[i] << "_length2(" << quatty[i] << "*)\n";
    out << "declare double @far_" << quats[i] << "_length(" << quatty[i] << "*)\n";
    out << "declare void @far_" << quats[i] << "_normalize(" << quatty[i] << "*, " << quatty[i] << "*)\n";
    out << "declare void @far_" << quats[i] << "_mul(" << quatty[i] << "*, " << quatty[i] << "*, " << quatty[i]
        << "*)\n";
  }

  out << "declare void @far_color32_to_color(%FarColor32*, %FarColor*)\n";

  out << "declare i64 @far_drect_contains(%FarDRect*, %FarDPoint*)\n";
  out << "declare i64 @far_frect_contains(%FarFRect*, %FarFPoint*)\n";
  out << "declare i64 @far_frect_contains_vec(%FarFRect*, %FarFVec2*)\n";
  out << "declare i64 @far_drect_intersects(%FarDRect*, %FarDRect*)\n";
  out << "declare i64 @far_frect_intersects(%FarFRect*, %FarFRect*)\n";
  out << "declare void @far_drect_center(%FarDRect*, %FarDPoint*)\n";
  out << "declare void @far_frect_center(%FarFRect*, %FarFPoint*)\n";
  out << "declare void @far_drect_expand(%FarDRect*, double, %FarDRect*)\n";
  out << "declare void @far_frect_expand(%FarFRect*, float, %FarFRect*)\n";

  out << "declare i64 @far_bounds_contains(%FarBounds*, %FarFVec3*)\n";
  out << "declare i64 @far_bounds_intersects(%FarBounds*, %FarBounds*)\n";
  out << "declare void @far_bounds_expand(%FarBounds*, float, %FarBounds*)\n";
  out << "declare void @far_bounds_center(%FarBounds*, %FarFVec3*)\n";
  out << "declare void @far_bounds_size(%FarBounds*, %FarFVec3*)\n";

  out << "declare double @far_fmin(double, double)\n";
  out << "declare double @far_fmax(double, double)\n";
}

}  // namespace far
