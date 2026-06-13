#pragma once

#include "type_desc.h"

#include <sstream>
#include <string>

namespace far {

enum class ErrMethodId {
  IsSome,
  IsNone,
  Unwrap,
  UnwrapOr,
  IsOk,
  IsErr,
  Ok,
  Err,
};

struct ErrMethodInfo {
  ErrMethodId id;
  const char* name;
  int nargs;
};

struct ErrConstructorInfo {
  const char* name;
  bool is_ok;
  bool is_some;
  int nargs;
};

const ErrConstructorInfo* lookupErrConstructor(const std::string& name);
const ErrMethodInfo* lookupOptionMethod(const std::string& name);
const ErrMethodInfo* lookupResultMethod(const std::string& name);
TypeDesc errMethodRetType(ErrMethodId id, const TypeDesc& recv, const TypeDesc& arg);

void declareErrorRuntime(std::ostringstream& out);

}  // namespace far
