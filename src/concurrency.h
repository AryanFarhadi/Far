#pragma once

#include "type_desc.h"

#include <sstream>
#include <string>

namespace far {

enum class ConcMethodId {
  Send,
  Recv,
  Close,
  Lock,
  Unlock,
  Wait,
  Signal,
  Load,
  Store,
  FetchAdd,
  CompareExchange,
  Submit,
  Shutdown,
  Push,
  Pop,
  Tell,
  Ask,
  Stop,
  Await,
  TryRecv,
  TrySend,
  TryWait,
  IsClosed,
  Pending,
};

struct ConcMethodInfo {
  ConcMethodId id;
  const char* name;
  int nargs;
};

struct ConcConstructorInfo {
  const char* name;
  TypeForm form;
  int nargs;
  bool has_elem_type;
};

const ConcConstructorInfo* lookupConcConstructor(const std::string& name);
const ConcMethodInfo* lookupConcMethod(TypeForm form, const std::string& name);
const ConcMethodInfo* lookupActorMethod(const std::string& name);
TypeDesc concMethodRetType(TypeForm form, ConcMethodId id, const TypeDesc& recv, const TypeDesc& arg);

void declareConcurrencyRuntime(std::ostringstream& out);

}  // namespace far
