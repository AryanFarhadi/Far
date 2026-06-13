#include "geom_class.h"

#include "error.h"

namespace far {

std::string geomClassName(FarTypeId id) {
  const AggregateMeta* m = aggregateMeta(id);
  if (!m)
    return "";
  const std::string& n = m->name;
  if (n == "fvec2" || n == "fvec3" || n == "fvec4")
    return "vec" + n.substr(4);
  if (n == "fpoint")
    return "point";
  if (n == "frect")
    return "rect";
  return n;
}

FarTypeId lookupGeomAggType(const std::string& class_name) {
  static const struct {
    const char* cls;
    FarTypeId id;
  } kMap[] = {
      {"vec2", FarTypeId::FVec2},       {"vec3", FarTypeId::FVec3},       {"vec4", FarTypeId::FVec4},
      {"fvec2", FarTypeId::FVec2},     {"fvec3", FarTypeId::FVec3},     {"fvec4", FarTypeId::FVec4},
      {"dvec2", FarTypeId::DVec2},     {"dvec3", FarTypeId::DVec3},     {"dvec4", FarTypeId::DVec4},
      {"ivec2", FarTypeId::IVec2},     {"ivec3", FarTypeId::IVec3},     {"ivec4", FarTypeId::IVec4},
      {"point", FarTypeId::FPoint},    {"dpoint", FarTypeId::DPoint},
      {"fpoint", FarTypeId::FPoint},
      {"rect", FarTypeId::FRect},      {"drect", FarTypeId::DRect},
      {"frect", FarTypeId::FRect},
      {"mat2", FarTypeId::Mat2},       {"mat3", FarTypeId::Mat3},       {"mat4", FarTypeId::Mat4},
      {"dmat2", FarTypeId::DMat2},     {"dmat3", FarTypeId::DMat3},     {"dmat4", FarTypeId::DMat4},
      {"quat", FarTypeId::Quat},       {"dquat", FarTypeId::DQuat},
      {"color", FarTypeId::Color},     {"color32", FarTypeId::Color32},
      {"transform", FarTypeId::Transform},
      {"bounds", FarTypeId::Bounds},
      // legacy PascalCase (hints only)
      {"Vec2", FarTypeId::FVec2},       {"Vec3", FarTypeId::FVec3},       {"Vec4", FarTypeId::FVec4},
      {"DVec2", FarTypeId::DVec2},     {"DVec3", FarTypeId::DVec3},     {"DVec4", FarTypeId::DVec4},
      {"IVec2", FarTypeId::IVec2},     {"IVec3", FarTypeId::IVec3},     {"IVec4", FarTypeId::IVec4},
      {"Point", FarTypeId::FPoint},    {"DPoint", FarTypeId::DPoint},
      {"Rect", FarTypeId::FRect},      {"DRect", FarTypeId::DRect},
      {"Mat2", FarTypeId::Mat2},       {"Mat3", FarTypeId::Mat3},       {"Mat4", FarTypeId::Mat4},
      {"DMat2", FarTypeId::DMat2},     {"DMat3", FarTypeId::DMat3},     {"DMat4", FarTypeId::DMat4},
      {"Quat", FarTypeId::Quat},       {"DQuat", FarTypeId::DQuat},
      {"Color", FarTypeId::Color},     {"Color32", FarTypeId::Color32},
      {"Transform", FarTypeId::Transform},
      {"Bounds", FarTypeId::Bounds},
  };
  for (const auto& e : kMap) {
    if (class_name == e.cls)
      return e.id;
  }
  return FarTypeId::Void;
}

const MethodInfo* lookupGeomMethod(FarTypeId agg_type, const std::string& method_name) {
  if (!isAggregateType(agg_type))
    return nullptr;
  return lookupMethod(agg_type, method_name);
}

std::string geomModuleName(FarTypeId id) {
  if (isVecFamily(id) || isIVecFamily(id))
    return "vectors";
  if (isPointFamily(id))
    return "points";
  if (isRectFamily(id))
    return "rects";
  if (isMatFamily(id))
    return "matrices";
  if (isQuatFamily(id))
    return "quaternions";
  if (id == FarTypeId::Color || id == FarTypeId::Color32)
    return "colors";
  if (id == FarTypeId::Bounds)
    return "bounds";
  if (id == FarTypeId::Transform)
    return "transforms";
  return geomClassName(id);
}

std::string geomNamespaceMethodHint(FarTypeId agg_type, const std::string& method_name) {
  return "use " + geomModuleName(agg_type) + "." + method_name + "(...) instead of " +
         geomClassName(agg_type) + "." + method_name + "(...)";
}

std::string geomInstanceMethodHint(FarTypeId agg_type, const std::string& method_name, int instance_nargs) {
  const std::string mod = geomModuleName(agg_type);
  if (instance_nargs == 0)
    return "use " + mod + "." + method_name + "(v) instead of v." + method_name + "()";
  if (instance_nargs == 1)
    return "use " + mod + "." + method_name + "(a, b) instead of a." + method_name + "(b)";
  if (instance_nargs == 2)
    return "use " + mod + "." + method_name + "(v, lo, hi) instead of v." + method_name + "(lo, hi)";
  return "use " + mod + "." + method_name + "(...) instead of instance method call";
}

}  // namespace far
