#pragma once

#include "type_desc.h"

#include <sstream>
#include <string>

namespace far {

enum class StrMethodId {
  Trim,
  ToLower,
  ToUpper,
  Split,
  Contains,
  StartsWith,
  EndsWith,
};

struct StrMethodInfo {
  StrMethodId id;
  const char* name;
  int nargs;
};

const StrMethodInfo* lookupStrMethod(const std::string& name);
TypeDesc strMethodRetType(StrMethodId id);
void declareStringRuntime(std::ostringstream& out);

}  // namespace far
