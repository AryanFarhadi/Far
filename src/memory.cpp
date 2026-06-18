#include "memory.h"

#include "error.h"

namespace far {

static const MemConstructorInfo kMemConstructors[] = {
    {"Box", TypeForm::Box, 1, true},
    {"Rc", TypeForm::Rc, 1, true},
    {"Arena", TypeForm::Arena, 1, false},
    {"Pool", TypeForm::MemPool, 1, true},
};

static const MemMethodInfo kBoxMethods[] = {
    {MemMethodId::Get, "get", 0},
    {MemMethodId::Drop, "drop", 0},
};

static const MemMethodInfo kRcMethods[] = {
    {MemMethodId::Get, "get", 0},
    {MemMethodId::Clone, "clone", 0},
    {MemMethodId::Drop, "drop", 0},
};

static const MemMethodInfo kArenaMethods[] = {
    {MemMethodId::Alloc, "alloc", 1},
    {MemMethodId::Reset, "reset", 0},
    {MemMethodId::Drop, "drop", 0},
};

static const MemMethodInfo kPoolMethods[] = {
    {MemMethodId::Acquire, "acquire", 0},
    {MemMethodId::Release, "release", 1},
    {MemMethodId::Drop, "drop", 0},
};

const MemConstructorInfo* lookupMemConstructor(const std::string& name) {
  for (const auto& c : kMemConstructors) {
    if (name == c.name)
      return &c;
  }
  return nullptr;
}

const MemMethodInfo* lookupMemMethod(TypeForm form, const std::string& name) {
  const MemMethodInfo* table = nullptr;
  size_t count = 0;
  switch (form) {
    case TypeForm::Box:
      table = kBoxMethods;
      count = sizeof(kBoxMethods) / sizeof(kBoxMethods[0]);
      break;
    case TypeForm::Rc:
      table = kRcMethods;
      count = sizeof(kRcMethods) / sizeof(kRcMethods[0]);
      break;
    case TypeForm::Arena:
      table = kArenaMethods;
      count = sizeof(kArenaMethods) / sizeof(kArenaMethods[0]);
      break;
    case TypeForm::MemPool:
      table = kPoolMethods;
      count = sizeof(kPoolMethods) / sizeof(kPoolMethods[0]);
      break;
    default:
      return nullptr;
  }
  for (size_t i = 0; i < count; ++i) {
    if (name == table[i].name)
      return &table[i];
  }
  return nullptr;
}

TypeDesc memMethodRetType(TypeForm form, MemMethodId id, const TypeDesc& recv, const TypeDesc& arg) {
  (void)arg;
  switch (form) {
    case TypeForm::Box:
    case TypeForm::Rc:
      if (id == MemMethodId::Get && !recv.args.empty())
        return TypeDesc::pointer(recv.args[0]);
      if (id == MemMethodId::Clone)
        return recv;
      return TypeDesc::prim(FarTypeId::I64);
    case TypeForm::Arena:
      if (id == MemMethodId::Alloc)
        return TypeDesc::pointer(TypeDesc::prim(FarTypeId::I8));
      return TypeDesc::prim(FarTypeId::I64);
    case TypeForm::MemPool:
      if (id == MemMethodId::Acquire && !recv.args.empty())
        return TypeDesc::pointer(recv.args[0]);
      return TypeDesc::prim(FarTypeId::I64);
    default:
      return TypeDesc::prim(FarTypeId::I64);
  }
}

void declareMemoryRuntime(std::ostringstream& out) {
  out << "declare i64 @far_malloc(i64)\n";
  out << "declare void @far_free(i64)\n";
  out << "declare i64 @far_realloc(i64, i64)\n";
  out << "declare i64 @far_box_new(i64)\n";
  out << "declare i64 @far_box_get(i64)\n";
  out << "declare void @far_box_drop(i64)\n";
  out << "declare i64 @far_box_move(i64)\n";
  out << "declare i64 @far_rc_new(i64)\n";
  out << "declare i64 @far_rc_get(i64)\n";
  out << "declare i64 @far_rc_clone(i64)\n";
  out << "declare void @far_rc_drop(i64)\n";
  out << "declare i64 @far_arena_new(i64)\n";
  out << "declare i64 @far_arena_alloc(i64, i64)\n";
  out << "declare void @far_arena_reset(i64)\n";
  out << "declare void @far_arena_drop(i64)\n";
  out << "declare i64 @far_pool_new(i64, i64)\n";
  out << "declare i64 @far_pool_acquire(i64)\n";
  out << "declare void @far_pool_release(i64, i64)\n";
  out << "declare void @far_pool_drop(i64)\n";
  out << "declare void @far_ptr_store_i64(i64, i64)\n";
  out << "declare i64 @far_ptr_load_i64(i64)\n";
  out << "declare i64 @far_ptr_load_i8_as_i64(i64)\n";
  out << "declare i64 @far_ptr_load_i16_as_i64(i64)\n";
  out << "declare i64 @far_ptr_load_i32_as_i64(i64)\n";
  out << "declare double @far_ptr_load_f64(i64)\n";
  out << "declare double @far_ptr_load_f32_as_f64(i64)\n";
  out << "declare i16 @far_ptr_load_f16(i64)\n";
  out << "declare void @far_ptr_store_i8(i64, i64)\n";
  out << "declare void @far_ptr_store_i16(i64, i64)\n";
  out << "declare void @far_ptr_store_i32(i64, i64)\n";
  out << "declare void @far_ptr_store_f64(i64, double)\n";
  out << "declare void @far_ptr_store_f32(i64, double)\n";
  out << "declare void @far_ptr_store_f16(i64, i16)\n";
}

}  // namespace far
