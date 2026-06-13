#pragma once

#include "type_desc.h"

#include <sstream>
#include <string>

namespace far {

enum class MemMethodId {
  Get,
  Clone,
  Drop,
  Alloc,
  Reset,
  Acquire,
  Release,
};

struct MemMethodInfo {
  MemMethodId id;
  const char* name;
  int nargs;
};

struct MemConstructorInfo {
  const char* name;
  TypeForm form;
  int nargs;
  bool has_elem_type;
};

const MemConstructorInfo* lookupMemConstructor(const std::string& name);
const MemMethodInfo* lookupMemMethod(TypeForm form, const std::string& name);
TypeDesc memMethodRetType(TypeForm form, MemMethodId id, const TypeDesc& recv, const TypeDesc& arg);

void declareMemoryRuntime(std::ostringstream& out);

}  // namespace far
