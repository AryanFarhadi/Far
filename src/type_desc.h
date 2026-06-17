#pragma once

#include "types.h"

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace far {

enum class TypeForm {
  Primitive,
  Array,
  List,
  FixedArray,
  Dict,
  Set,
  Queue,
  Stack,
  LinkedList,
  Span,
  Slice,
  Tuple,
  Range,
  Function,
  Optional,
  Result,
  TypeVar,
  User,
  Pointer,
  BorrowRef,
  Box,
  Rc,
  Arena,
  MemPool,
  Channel,
  Mutex,
  Semaphore,
  Atomic,
  ThreadPool,
  LockFreeQueue,
  Task,
};

struct Expr;

struct TypeDesc {
  TypeForm form = TypeForm::Primitive;
  FarTypeId primitive = FarTypeId::I64;
  std::vector<TypeDesc> args;
  int64_t const_n = 0;
  std::shared_ptr<Expr> comptime_size;
  std::string type_var;
  std::string user_name;

  static TypeDesc prim(FarTypeId id);
  static TypeDesc array(TypeDesc elem);
  static TypeDesc list(TypeDesc elem);
  static TypeDesc fixedArray(TypeDesc elem, int64_t n);
  static TypeDesc dict(TypeDesc key, TypeDesc val);
  static TypeDesc set(TypeDesc elem);
  static TypeDesc queue(TypeDesc elem);
  static TypeDesc stack(TypeDesc elem);
  static TypeDesc linkedList(TypeDesc elem);
  static TypeDesc span(TypeDesc elem);
  static TypeDesc slice(TypeDesc elem);
  static TypeDesc tuple(std::vector<TypeDesc> fields);
  static TypeDesc range();
  static TypeDesc function(std::vector<TypeDesc> params, TypeDesc ret);
  static TypeDesc optional(TypeDesc inner);
  static TypeDesc result(TypeDesc ok, TypeDesc err);
  static TypeDesc typeVar(std::string name);
  static TypeDesc user(std::string name, std::vector<TypeDesc> args = {});
  static TypeDesc pointer(TypeDesc pointee);
  static TypeDesc borrowRef(TypeDesc pointee);
  static TypeDesc box(TypeDesc elem);
  static TypeDesc rc(TypeDesc elem);
  static TypeDesc arena();
  static TypeDesc memPool(TypeDesc elem);
  static TypeDesc channel(TypeDesc elem);
  static TypeDesc mutex();
  static TypeDesc semaphore();
  static TypeDesc atomic(TypeDesc elem);
  static TypeDesc threadPool();
  static TypeDesc lockFreeQueue(TypeDesc elem);
  static TypeDesc task();
};

TypeDesc resolveSubst(const std::unordered_map<std::string, TypeDesc>& sub, TypeDesc td);
TypeDesc substituteTypeDesc(const TypeDesc& td, const std::unordered_map<std::string, TypeDesc>& sub);
bool unifyTypeVar(const std::string& name, const TypeDesc& concrete,
                  std::unordered_map<std::string, TypeDesc>& out);
void unifyTypes(const TypeDesc& expected, const TypeDesc& actual,
                std::unordered_map<std::string, TypeDesc>& out);

bool typeDescEquals(const TypeDesc& a, const TypeDesc& b);
bool isPrimitiveDesc(const TypeDesc& td);
bool isCollectionDesc(const TypeDesc& td);
FarTypeId primitiveOf(const TypeDesc& td);
TypeDesc elemTypeOf(const TypeDesc& td);
std::string typeDescName(const TypeDesc& td);
bool canAssignTypes(const TypeDesc& from, const TypeDesc& to);
bool canCastTypes(const TypeDesc& from, const TypeDesc& to);

uint16_t typeTag(const TypeDesc& td);
int elemSizeBytes(const TypeDesc& td);
int elemAlignBytes(const TypeDesc& td);
bool isHashableType(const TypeDesc& td);
bool isCollectionHandle(const TypeDesc& td);
bool isOwnedHeapCollectionDesc(const TypeDesc& td);
bool isCollectionValue(const TypeDesc& td);
bool isFunctionDesc(const TypeDesc& td);
inline TypeDesc defaultIntType() { return TypeDesc::prim(FarTypeId::I32); }
TypeDesc inferIntLiteralType(int64_t value, bool unsigned_decimal);
bool isUserDesc(const TypeDesc& td);
bool isPointerDesc(const TypeDesc& td);
bool isBorrowRefDesc(const TypeDesc& td);
bool isBoxDesc(const TypeDesc& td);
bool isRcDesc(const TypeDesc& td);
bool isArenaDesc(const TypeDesc& td);
bool isMemPoolDesc(const TypeDesc& td);
bool isMemoryHandleDesc(const TypeDesc& td);
bool isPtrLikeDesc(const TypeDesc& td);
TypeDesc pointeeOf(const TypeDesc& td);
bool isConcurrencyHandleDesc(const TypeDesc& td);
bool isOptionDesc(const TypeDesc& td);
bool isResultDesc(const TypeDesc& td);

TypeDesc parseTypeDescName(const std::string& name);

}  // namespace far
