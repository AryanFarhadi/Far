#pragma once

#include "type_desc.h"

#include <sstream>
#include <string>
#include <vector>

namespace far {

enum class CollMethodId {
  Len,
  Push,
  Pop,
  Insert,
  Remove,
  RemoveValue,
  Clear,
  Get,
  Set,
  ContainsKey,
  Keys,
  Values,
  Add,
  Contains,
  Enqueue,
  Dequeue,
  Peek,
  PushFront,
  PushBack,
  PopFront,
  PopBack,
  Slice,
  ToColor,
  Mul,
};

struct CollMethodInfo {
  CollMethodId id;
  const char* name;
  int nargs;
};

struct CollConstructorInfo {
  const char* name;
  TypeForm form;
  int nargs;
  bool has_elem_type;
};

const CollConstructorInfo* lookupCollConstructor(const std::string& name);
const CollMethodInfo* lookupCollMethod(TypeForm form, const std::string& name);
TypeDesc collMethodRetType(TypeForm form, CollMethodId id, const TypeDesc& recv, const TypeDesc& arg);

void declareCollectionTypes(std::ostringstream& out);
void declareCollectionRuntime(std::ostringstream& out);

}  // namespace far
