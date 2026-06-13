#pragma once

#include "aggregate.h"

#include <string>

namespace far {

std::string geomClassName(FarTypeId id);
std::string geomModuleName(FarTypeId id);
FarTypeId lookupGeomAggType(const std::string& class_name);
const MethodInfo* lookupGeomMethod(FarTypeId agg_type, const std::string& method_name);
std::string geomInstanceMethodHint(FarTypeId agg_type, const std::string& method_name, int instance_nargs);
std::string geomNamespaceMethodHint(FarTypeId agg_type, const std::string& method_name);

}  // namespace far
