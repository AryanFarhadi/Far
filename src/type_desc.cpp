#include "type_desc.h"

#include "aggregate.h"
#include "error.h"
#include "types.h"

#include <sstream>

namespace far {

TypeDesc TypeDesc::prim(FarTypeId id) {
  TypeDesc td;
  td.form = TypeForm::Primitive;
  td.primitive = id;
  return td;
}

TypeDesc TypeDesc::array(TypeDesc elem) {
  TypeDesc td;
  td.form = TypeForm::Array;
  td.args = {std::move(elem)};
  return td;
}

TypeDesc TypeDesc::list(TypeDesc elem) {
  TypeDesc td;
  td.form = TypeForm::List;
  td.args = {std::move(elem)};
  return td;
}

TypeDesc TypeDesc::fixedArray(TypeDesc elem, int64_t n) {
  TypeDesc td;
  td.form = TypeForm::FixedArray;
  td.args = {std::move(elem)};
  td.const_n = n;
  return td;
}

TypeDesc TypeDesc::dict(TypeDesc key, TypeDesc val) {
  TypeDesc td;
  td.form = TypeForm::Dict;
  td.args = {std::move(key), std::move(val)};
  return td;
}

TypeDesc TypeDesc::set(TypeDesc elem) {
  TypeDesc td;
  td.form = TypeForm::Set;
  td.args = {std::move(elem)};
  return td;
}

TypeDesc TypeDesc::queue(TypeDesc elem) {
  TypeDesc td;
  td.form = TypeForm::Queue;
  td.args = {std::move(elem)};
  return td;
}

TypeDesc TypeDesc::stack(TypeDesc elem) {
  TypeDesc td;
  td.form = TypeForm::Stack;
  td.args = {std::move(elem)};
  return td;
}

TypeDesc TypeDesc::linkedList(TypeDesc elem) {
  TypeDesc td;
  td.form = TypeForm::LinkedList;
  td.args = {std::move(elem)};
  return td;
}

TypeDesc TypeDesc::span(TypeDesc elem) {
  TypeDesc td;
  td.form = TypeForm::Span;
  td.args = {std::move(elem)};
  return td;
}

TypeDesc TypeDesc::slice(TypeDesc elem) {
  TypeDesc td;
  td.form = TypeForm::Slice;
  td.args = {std::move(elem)};
  return td;
}

TypeDesc TypeDesc::tuple(std::vector<TypeDesc> fields) {
  TypeDesc td;
  td.form = TypeForm::Tuple;
  td.args = std::move(fields);
  return td;
}

TypeDesc TypeDesc::range() {
  TypeDesc td;
  td.form = TypeForm::Range;
  return td;
}

TypeDesc TypeDesc::function(std::vector<TypeDesc> params, TypeDesc ret) {
  TypeDesc td;
  td.form = TypeForm::Function;
  td.args = std::move(params);
  td.args.push_back(std::move(ret));
  return td;
}

TypeDesc TypeDesc::optional(TypeDesc inner) {
  TypeDesc td;
  td.form = TypeForm::Optional;
  td.args = {std::move(inner)};
  return td;
}

TypeDesc TypeDesc::result(TypeDesc ok, TypeDesc err) {
  TypeDesc td;
  td.form = TypeForm::Result;
  td.args = {std::move(ok), std::move(err)};
  return td;
}

bool isOptionDesc(const TypeDesc& td) { return td.form == TypeForm::Optional; }

bool isResultDesc(const TypeDesc& td) { return td.form == TypeForm::Result; }

TypeDesc TypeDesc::typeVar(std::string name) {
  TypeDesc td;
  td.form = TypeForm::TypeVar;
  td.type_var = std::move(name);
  return td;
}

TypeDesc TypeDesc::user(std::string name, std::vector<TypeDesc> args) {
  TypeDesc td;
  td.form = TypeForm::User;
  td.user_name = std::move(name);
  td.args = std::move(args);
  return td;
}

TypeDesc TypeDesc::pointer(TypeDesc pointee) {
  TypeDesc td;
  td.form = TypeForm::Pointer;
  td.args = {std::move(pointee)};
  return td;
}

TypeDesc TypeDesc::borrowRef(TypeDesc pointee) {
  TypeDesc td;
  td.form = TypeForm::BorrowRef;
  td.args = {std::move(pointee)};
  return td;
}

TypeDesc TypeDesc::box(TypeDesc elem) {
  TypeDesc td;
  td.form = TypeForm::Box;
  td.args = {std::move(elem)};
  return td;
}

TypeDesc TypeDesc::rc(TypeDesc elem) {
  TypeDesc td;
  td.form = TypeForm::Rc;
  td.args = {std::move(elem)};
  return td;
}

TypeDesc TypeDesc::arena() {
  TypeDesc td;
  td.form = TypeForm::Arena;
  return td;
}

TypeDesc TypeDesc::memPool(TypeDesc elem) {
  TypeDesc td;
  td.form = TypeForm::MemPool;
  td.args = {std::move(elem)};
  return td;
}

TypeDesc TypeDesc::channel(TypeDesc elem) {
  TypeDesc td;
  td.form = TypeForm::Channel;
  td.args = {std::move(elem)};
  return td;
}

TypeDesc TypeDesc::mutex() {
  TypeDesc td;
  td.form = TypeForm::Mutex;
  return td;
}

TypeDesc TypeDesc::semaphore() {
  TypeDesc td;
  td.form = TypeForm::Semaphore;
  return td;
}

TypeDesc TypeDesc::atomic(TypeDesc elem) {
  TypeDesc td;
  td.form = TypeForm::Atomic;
  td.args = {std::move(elem)};
  return td;
}

TypeDesc TypeDesc::threadPool() {
  TypeDesc td;
  td.form = TypeForm::ThreadPool;
  return td;
}

TypeDesc TypeDesc::lockFreeQueue(TypeDesc elem) {
  TypeDesc td;
  td.form = TypeForm::LockFreeQueue;
  td.args = {std::move(elem)};
  return td;
}

TypeDesc TypeDesc::task() {
  TypeDesc td;
  td.form = TypeForm::Task;
  return td;
}

bool isUserDesc(const TypeDesc& td) { return td.form == TypeForm::User; }

bool isConcurrencyHandleDesc(const TypeDesc& td) {
  switch (td.form) {
    case TypeForm::Channel:
    case TypeForm::Mutex:
    case TypeForm::Semaphore:
    case TypeForm::Atomic:
    case TypeForm::ThreadPool:
    case TypeForm::LockFreeQueue:
    case TypeForm::Task:
      return true;
    default:
      return false;
  }
}

bool isPointerDesc(const TypeDesc& td) { return td.form == TypeForm::Pointer; }
bool isBorrowRefDesc(const TypeDesc& td) { return td.form == TypeForm::BorrowRef; }
bool isBoxDesc(const TypeDesc& td) { return td.form == TypeForm::Box; }
bool isRcDesc(const TypeDesc& td) { return td.form == TypeForm::Rc; }
bool isArenaDesc(const TypeDesc& td) { return td.form == TypeForm::Arena; }
bool isMemPoolDesc(const TypeDesc& td) { return td.form == TypeForm::MemPool; }

bool isMemoryHandleDesc(const TypeDesc& td) {
  return isBoxDesc(td) || isRcDesc(td) || isArenaDesc(td) || isMemPoolDesc(td);
}

bool isPtrLikeDesc(const TypeDesc& td) {
  return isPointerDesc(td) || isBorrowRefDesc(td) ||
         (isPrimitiveDesc(td) && (td.primitive == FarTypeId::Ptr || td.primitive == FarTypeId::Ref));
}

TypeDesc pointeeOf(const TypeDesc& td) {
  if (isPointerDesc(td) || isBorrowRefDesc(td))
    return td.args.empty() ? TypeDesc::prim(FarTypeId::I8) : td.args[0];
  if (isPrimitiveDesc(td) && td.primitive == FarTypeId::Ptr)
    return TypeDesc::prim(FarTypeId::I8);
  if (isPrimitiveDesc(td) && td.primitive == FarTypeId::Ref)
    return TypeDesc::prim(FarTypeId::I8);
  throw FarError("type is not a pointer or reference");
}

TypeDesc substituteTypeDesc(const TypeDesc& td, const std::unordered_map<std::string, TypeDesc>& sub) {
  if (td.form == TypeForm::TypeVar) {
    auto it = sub.find(td.type_var);
    if (it != sub.end())
      return it->second;
    return td;
  }
  TypeDesc out = td;
  out.args.clear();
  for (const auto& a : td.args)
    out.args.push_back(substituteTypeDesc(a, sub));
  return out;
}

bool unifyTypeVar(const std::string& name, const TypeDesc& concrete,
                  std::unordered_map<std::string, TypeDesc>& out) {
  if (concrete.form == TypeForm::TypeVar) {
    if (concrete.type_var == name)
      return true;
    auto it = out.find(concrete.type_var);
    if (it != out.end())
      return unifyTypeVar(name, it->second, out);
    out[name] = concrete;
    return true;
  }
  auto it = out.find(name);
  if (it == out.end()) {
    out[name] = concrete;
    return true;
  }
  return typeDescEquals(it->second, concrete);
}

void unifyTypes(const TypeDesc& expected, const TypeDesc& actual,
                std::unordered_map<std::string, TypeDesc>& out) {
  if (expected.form == TypeForm::TypeVar) {
    unifyTypeVar(expected.type_var, actual, out);
    return;
  }
  if (expected.form == TypeForm::Optional && !expected.args.empty()) {
    unifyTypes(expected.args[0], actual, out);
    return;
  }
  if (expected.form == TypeForm::User && actual.form == TypeForm::User) {
    if (expected.user_name == actual.user_name && expected.args.size() == actual.args.size()) {
      for (size_t i = 0; i < expected.args.size(); ++i)
        unifyTypes(expected.args[i], actual.args[i], out);
    }
    return;
  }
  (void)actual;
}

bool isPrimitiveDesc(const TypeDesc& td) { return td.form == TypeForm::Primitive; }

bool isCollectionDesc(const TypeDesc& td) {
  switch (td.form) {
    case TypeForm::Array:
    case TypeForm::List:
    case TypeForm::FixedArray:
    case TypeForm::Dict:
    case TypeForm::Set:
    case TypeForm::Queue:
    case TypeForm::Stack:
    case TypeForm::LinkedList:
    case TypeForm::Span:
    case TypeForm::Slice:
    case TypeForm::Tuple:
    case TypeForm::Range:
      return true;
    default:
      return false;
  }
}

FarTypeId primitiveOf(const TypeDesc& td) {
  if (!isPrimitiveDesc(td))
    throw FarError("expected primitive type");
  return td.primitive;
}

TypeDesc elemTypeOf(const TypeDesc& td) {
  if (td.args.empty())
    throw FarError("type has no element type");
  return td.args[0];
}

static std::string primName(FarTypeId id) { return typeInfo(id).name; }

std::string typeDescName(const TypeDesc& td) {
  switch (td.form) {
    case TypeForm::Primitive:
      return primName(td.primitive);
    case TypeForm::Array:
      return typeDescName(td.args[0]) + "[]";
    case TypeForm::List:
      return "List<" + typeDescName(td.args[0]) + ">";
    case TypeForm::FixedArray:
      return "FixedArray<" + typeDescName(td.args[0]) + ", " + std::to_string(td.const_n) + ">";
    case TypeForm::Dict:
      return "Dict<" + typeDescName(td.args[0]) + ", " + typeDescName(td.args[1]) + ">";
    case TypeForm::Set:
      return "Set<" + typeDescName(td.args[0]) + ">";
    case TypeForm::Queue:
      return "Queue<" + typeDescName(td.args[0]) + ">";
    case TypeForm::Stack:
      return "Stack<" + typeDescName(td.args[0]) + ">";
    case TypeForm::LinkedList:
      return "LinkedList<" + typeDescName(td.args[0]) + ">";
    case TypeForm::Span:
      return "Span<" + typeDescName(td.args[0]) + ">";
    case TypeForm::Slice:
      return "Slice<" + typeDescName(td.args[0]) + ">";
    case TypeForm::Tuple: {
      std::ostringstream os;
      os << "Tuple<";
      for (size_t i = 0; i < td.args.size(); ++i) {
        if (i)
          os << ", ";
        os << typeDescName(td.args[i]);
      }
      os << ">";
      return os.str();
    }
    case TypeForm::Range:
      return "Range";
    case TypeForm::Function: {
      if (td.args.empty())
        return "fn()";
      std::ostringstream os;
      os << "fn(";
      for (size_t i = 0; i + 1 < td.args.size(); ++i) {
        if (i)
          os << ", ";
        os << typeDescName(td.args[i]);
      }
      os << ") -> " << typeDescName(td.args.back());
      return os.str();
    }
    case TypeForm::Optional:
      return "Option<" + typeDescName(td.args[0]) + ">";
    case TypeForm::Result:
      return "Result<" + typeDescName(td.args[0]) + ", " + typeDescName(td.args[1]) + ">";
    case TypeForm::TypeVar:
      return td.type_var;
    case TypeForm::User:
      if (td.args.empty())
        return td.user_name;
      {
        std::ostringstream os;
        os << td.user_name << "<";
        for (size_t i = 0; i < td.args.size(); ++i) {
          if (i)
            os << ", ";
          os << typeDescName(td.args[i]);
        }
        os << ">";
        return os.str();
      }
    case TypeForm::Pointer:
      return "ptr<" + typeDescName(td.args[0]) + ">";
    case TypeForm::BorrowRef:
      return "ref<" + typeDescName(td.args[0]) + ">";
    case TypeForm::Box:
      return "Box<" + typeDescName(td.args[0]) + ">";
    case TypeForm::Rc:
      return "Rc<" + typeDescName(td.args[0]) + ">";
    case TypeForm::Arena:
      return "Arena";
    case TypeForm::MemPool:
      return "Pool<" + typeDescName(td.args[0]) + ">";
    case TypeForm::Channel:
      return "Channel<" + typeDescName(td.args[0]) + ">";
    case TypeForm::Mutex:
      return "Mutex";
    case TypeForm::Semaphore:
      return "Semaphore";
    case TypeForm::Atomic:
      return "Atomic<" + typeDescName(td.args[0]) + ">";
    case TypeForm::ThreadPool:
      return "ThreadPool";
    case TypeForm::LockFreeQueue:
      return "LockFreeQueue<" + typeDescName(td.args[0]) + ">";
    case TypeForm::Task:
      return "Task";
  }
  return "?";
}

bool typeDescEquals(const TypeDesc& a, const TypeDesc& b) {
  if (a.form != b.form)
    return false;
  if (a.form == TypeForm::Primitive)
    return a.primitive == b.primitive;
  if (a.form == TypeForm::FixedArray)
    return a.const_n == b.const_n && typeDescEquals(a.args[0], b.args[0]);
  if (a.form == TypeForm::Dict)
    return a.args.size() == 2 && b.args.size() == 2 && typeDescEquals(a.args[0], b.args[0]) &&
           typeDescEquals(a.args[1], b.args[1]);
  if (a.form == TypeForm::Tuple) {
    if (a.args.size() != b.args.size())
      return false;
    for (size_t i = 0; i < a.args.size(); ++i)
      if (!typeDescEquals(a.args[i], b.args[i]))
        return false;
    return true;
  }
  if (a.form == TypeForm::Range)
    return true;
  if (a.form == TypeForm::Function) {
    if (a.args.size() != b.args.size())
      return false;
    for (size_t i = 0; i < a.args.size(); ++i)
      if (!typeDescEquals(a.args[i], b.args[i]))
        return false;
    return true;
  }
  if (a.form == TypeForm::Optional)
    return b.form == TypeForm::Optional && typeDescEquals(a.args[0], b.args[0]);
  if (a.form == TypeForm::Result)
    return b.form == TypeForm::Result && a.args.size() == 2 && b.args.size() == 2 &&
           typeDescEquals(a.args[0], b.args[0]) && typeDescEquals(a.args[1], b.args[1]);
  if (a.form == TypeForm::TypeVar)
    return b.form == TypeForm::TypeVar && a.type_var == b.type_var;
  if (a.form == TypeForm::User) {
    if (b.form != TypeForm::User || a.user_name != b.user_name)
      return false;
    if (a.args.size() != b.args.size())
      return false;
    for (size_t i = 0; i < a.args.size(); ++i)
      if (!typeDescEquals(a.args[i], b.args[i]))
        return false;
    return true;
  }
  if (a.form == TypeForm::Arena)
    return b.form == TypeForm::Arena;
  if (a.form == TypeForm::Pointer || a.form == TypeForm::BorrowRef || a.form == TypeForm::Box ||
      a.form == TypeForm::Rc || a.form == TypeForm::MemPool || a.form == TypeForm::Channel ||
      a.form == TypeForm::Atomic || a.form == TypeForm::LockFreeQueue) {
    return b.form == a.form && a.args.size() == 1 && b.args.size() == 1 &&
           typeDescEquals(a.args[0], b.args[0]);
  }
  if (a.form == TypeForm::Mutex || a.form == TypeForm::Semaphore || a.form == TypeForm::ThreadPool ||
      a.form == TypeForm::Task)
    return b.form == a.form;
  if (a.args.size() != 1 || b.args.size() != 1)
    return false;
  return typeDescEquals(a.args[0], b.args[0]);
}

bool canAssignTypes(const TypeDesc& from, const TypeDesc& to) {
  if (typeDescEquals(from, to))
    return true;
  if (from.form == TypeForm::Array && to.form == TypeForm::Array)
    return canAssignTypes(from.args[0], to.args[0]);
  if (isPrimitiveDesc(to) && to.primitive == FarTypeId::Arr && from.form == TypeForm::Array)
    return true;
  if (isPrimitiveDesc(from) && from.primitive == FarTypeId::Arr && to.form == TypeForm::Array)
    return true;
  if (to.form == TypeForm::List && from.form == TypeForm::Array)
    return canAssignTypes(from.args[0], to.args[0]);
  if (to.form == TypeForm::Slice && from.form == TypeForm::Array)
    return canAssignTypes(from.args[0], to.args[0]);
  if (to.form == TypeForm::Optional && !to.args.empty())
    return canAssignTypes(from, to.args[0]) ||
           (isPrimitiveDesc(from) && from.primitive == FarTypeId::I64);
  if (from.form == TypeForm::Function && to.form == TypeForm::Function)
    return typeDescEquals(from, to);
  if (from.form == TypeForm::User && to.form == TypeForm::User)
    return typeDescEquals(from, to);
  if (isPointerDesc(from) && isPointerDesc(to))
    return canAssignTypes(from.args[0], to.args[0]) || typeDescEquals(from.args[0], to.args[0]);
  if (isBorrowRefDesc(from) && isBorrowRefDesc(to))
    return typeDescEquals(from.args[0], to.args[0]);
  if (isMemoryHandleDesc(from) && isMemoryHandleDesc(to))
    return from.form == to.form && typeDescEquals(from.args[0], to.args[0]);
  if (isConcurrencyHandleDesc(from) && isConcurrencyHandleDesc(to))
    return from.form == to.form && typeDescEquals(from.args[0], to.args[0]);
  if (isPrimitiveDesc(from) && isPrimitiveDesc(to))
    return canAssign(from.primitive, to.primitive);
  return false;
}

uint16_t typeTag(const TypeDesc& td) {
  if (isUserDesc(td))
    return static_cast<uint16_t>(0x8000);  // refined at codegen with registry tag
  if (!isPrimitiveDesc(td))
    return 0xFFFF;
  return static_cast<uint16_t>(td.primitive);
}

int elemSizeBytes(const TypeDesc& td) {
  TypeDesc e = isPrimitiveDesc(td) ? td : td.args[0];
  if (!isPrimitiveDesc(e))
    return 8;
  FarTypeId p = e.primitive;
  if (isAggregateType(p))
    return typeInfo(p).bits / 8;
  if (p == FarTypeId::I8 || p == FarTypeId::U8 || p == FarTypeId::Bool)
    return 1;
  if (p == FarTypeId::I16 || p == FarTypeId::U16 || p == FarTypeId::Char)
    return 2;
  if (p == FarTypeId::I32 || p == FarTypeId::U32 || p == FarTypeId::F32)
    return 4;
  if (p == FarTypeId::F64 || p == FarTypeId::I64 || p == FarTypeId::U64 || p == FarTypeId::String ||
      p == FarTypeId::Ptr || p == FarTypeId::Ref || p == FarTypeId::Any)
    return 8;
  return 8;
}

bool isHashableType(const TypeDesc& td) {
  if (!isPrimitiveDesc(td))
    return false;
  FarTypeId p = td.primitive;
  return isIntegerType(p) || p == FarTypeId::String || p == FarTypeId::Bool || p == FarTypeId::Char ||
         isAggregateType(p);
}

bool isCollectionHandle(const TypeDesc& td) {
  switch (td.form) {
    case TypeForm::Array:
    case TypeForm::List:
    case TypeForm::Dict:
    case TypeForm::Set:
    case TypeForm::Queue:
    case TypeForm::Stack:
    case TypeForm::LinkedList:
    case TypeForm::Slice:
    case TypeForm::Box:
    case TypeForm::Rc:
    case TypeForm::Arena:
    case TypeForm::MemPool:
    case TypeForm::Channel:
    case TypeForm::Mutex:
    case TypeForm::Semaphore:
    case TypeForm::Atomic:
    case TypeForm::ThreadPool:
    case TypeForm::LockFreeQueue:
    case TypeForm::Task:
      return true;
    default:
      return false;
  }
}

bool isCollectionValue(const TypeDesc& td) {
  return td.form == TypeForm::FixedArray || td.form == TypeForm::Span || td.form == TypeForm::Tuple ||
         td.form == TypeForm::Range;
}

bool isFunctionDesc(const TypeDesc& td) { return td.form == TypeForm::Function; }

TypeDesc parseTypeDescName(const std::string& name) {
  if (name == "List")
    throw FarError("List requires type argument");
  if (name == "Range")
    return TypeDesc::range();
  return TypeDesc::prim(parseTypeName(name));
}

}  // namespace far
