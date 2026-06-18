#include "memory_codegen.h"

#include "error.h"
#include "type_desc.h"
#include "types.h"

namespace far {

static const char* kF64 = "double";
static const char* kF32 = "float";
static const char* kF16 = "half";
static const char* kI64 = "i64";

static const char* ptrElemLlvmTypeFor(const TypeDesc& elem) {
  if (isPrimitiveDesc(elem)) {
    if (elem.primitive == FarTypeId::F16)
      return kF16;
    if (elem.primitive == FarTypeId::F32)
      return kF32;
    if (elem.primitive == FarTypeId::F64)
      return kF64;
    return typeInfo(elem.primitive).llvm;
  }
  int esz = elemSizeBytes(elem);
  switch (esz) {
    case 1:
      return "i8";
    case 2:
      return "i16";
    case 4:
      return "i32";
    default:
      return kI64;
  }
}

static void emitMemHandleOrPanic(MemCodegenCtx& ctx, const std::string& handle) {
  std::string ok = ctx.fresh("memok");
  std::string cont = ctx.fresh("memcont");
  std::string fail = ctx.fresh("memfail");
  ctx.out << "  %" << ok << " = icmp ne i64 " << handle << ", 0\n";
  ctx.out << "  br i1 %" << ok << ", label %" << cont << ", label %" << fail << "\n";
  ctx.out << fail << ":\n";
  ctx.out << "  call void @far_panic(i64 0)\n";
  ctx.out << "  unreachable\n";
  ctx.out << cont << ":\n";
}

std::string emitMemConstruct(MemCodegenCtx ctx, TypeForm form, const TypeDesc& elem_ty,
                             const std::vector<std::string>& arg_vals) {
  std::string tmp = ctx.fresh("mem");
  int esz = elemSizeBytes(elem_ty);
  switch (form) {
    case TypeForm::Box: {
      ctx.out << "  %" << tmp << " = call i64 @far_box_new(i64 " << esz << ")\n";
      emitMemHandleOrPanic(ctx, "%" + tmp);
      if (!arg_vals.empty()) {
        std::string hp = ctx.fresh("binit");
        ctx.out << "  %" << hp << " = call i64 @far_box_get(i64 %" << tmp << ")\n";
        emitPtrStore(ctx, TypeDesc::pointer(elem_ty), "%" + hp, arg_vals[0]);
      }
      break;
    }
    case TypeForm::Rc: {
      ctx.out << "  %" << tmp << " = call i64 @far_rc_new(i64 " << esz << ")\n";
      emitMemHandleOrPanic(ctx, "%" + tmp);
      if (!arg_vals.empty()) {
        std::string hp = ctx.fresh("rinit");
        ctx.out << "  %" << hp << " = call i64 @far_rc_get(i64 %" << tmp << ")\n";
        emitPtrStore(ctx, TypeDesc::pointer(elem_ty), "%" + hp, arg_vals[0]);
      }
      break;
    }
    case TypeForm::Arena:
      ctx.out << "  %" << tmp << " = call i64 @far_arena_new(i64 "
              << (arg_vals.empty() ? "4096" : arg_vals[0]) << ")\n";
      break;
    case TypeForm::MemPool: {
      std::string cap = arg_vals.empty() ? "8" : arg_vals[0];
      ctx.out << "  %" << tmp << " = call i64 @far_pool_new(i64 " << esz << ", i64 " << cap << ")\n";
      break;
    }
    default:
      throw FarError("invalid memory constructor");
  }
  return "%" + tmp;
}

std::string emitMemMethod(MemCodegenCtx ctx, const MethodCall& call, const TypeDesc& recv_ty,
                          const std::string& recv_val) {
  const MemMethodInfo* mi = lookupMemMethod(recv_ty.form, call.method);
  if (!mi)
    throw FarError("unknown memory method");
  std::string tmp = ctx.fresh("mm");
  switch (mi->id) {
    case MemMethodId::Get:
      if (recv_ty.form == TypeForm::Box)
        ctx.out << "  %" << tmp << " = call i64 @far_box_get(i64 " << recv_val << ")\n";
      else
        ctx.out << "  %" << tmp << " = call i64 @far_rc_get(i64 " << recv_val << ")\n";
      break;
    case MemMethodId::Clone:
      ctx.out << "  %" << tmp << " = call i64 @far_rc_clone(i64 " << recv_val << ")\n";
      break;
    case MemMethodId::Drop:
      emitMemDrop(ctx, recv_ty.form, recv_val);
      return "0";
    case MemMethodId::Alloc: {
      std::string sz = ctx.emit_expr(*call.args[0]);
      ctx.out << "  %" << tmp << " = call i64 @far_arena_alloc(i64 " << recv_val << ", i64 " << sz << ")\n";
      break;
    }
    case MemMethodId::Reset:
      ctx.out << "  call void @far_arena_reset(i64 " << recv_val << ")\n";
      return "0";
    case MemMethodId::Acquire:
      ctx.out << "  %" << tmp << " = call i64 @far_pool_acquire(i64 " << recv_val << ")\n";
      break;
    case MemMethodId::Release: {
      std::string obj = ctx.emit_expr(*call.args[0]);
      ctx.out << "  call void @far_pool_release(i64 " << recv_val << ", i64 " << obj << ")\n";
      return "0";
    }
    default:
      throw FarError("unsupported memory method");
  }
  return "%" + tmp;
}

void emitMemDrop(MemCodegenCtx ctx, TypeForm form, const std::string& handle) {
  switch (form) {
    case TypeForm::Box:
      ctx.out << "  call void @far_box_drop(i64 " << handle << ")\n";
      break;
    case TypeForm::Rc:
      ctx.out << "  call void @far_rc_drop(i64 " << handle << ")\n";
      break;
    case TypeForm::Arena:
      ctx.out << "  call void @far_arena_drop(i64 " << handle << ")\n";
      break;
    case TypeForm::MemPool:
      ctx.out << "  call void @far_pool_drop(i64 " << handle << ")\n";
      break;
    default:
      break;
  }
}

std::string emitStackAlloc(MemCodegenCtx ctx, const TypeDesc& elem_ty, const std::string& count_val) {
  int esz = elemSizeBytes(elem_ty);
  std::string arr = ctx.fresh("stk");
  if (esz == 8) {
    ctx.out << "  %" << arr << " = alloca i64, i64 " << count_val << "\n";
    std::string ptr = ctx.fresh("stkp");
    ctx.out << "  %" << ptr << " = ptrtoint i64* %" << arr << " to i64\n";
    return "%" + ptr;
  }
  std::string bytes = ctx.fresh("stkb");
  std::string cnt = ctx.fresh("stkn");
  ctx.out << "  %" << cnt << " = call i64 @far_i64_mul_checked(i64 " << count_val << ", i64 " << esz
            << ")\n";
  ctx.out << "  %" << bytes << " = alloca i8, i64 %" << cnt << "\n";
  std::string ptr = ctx.fresh("stkp");
  ctx.out << "  %" << ptr << " = ptrtoint i8* %" << bytes << " to i64\n";
  return "%" + ptr;
}

std::string emitPtrDeref(MemCodegenCtx ctx, const TypeDesc& ptr_ty, const std::string& ptr_val) {
  TypeDesc elem = pointeeOf(ptr_ty);
  std::string tmp = ctx.fresh("dval");
  if (isPrimitiveDesc(elem) && elem.primitive == FarTypeId::F64) {
    ctx.out << "  %" << tmp << " = call " << kF64 << " @far_ptr_load_f64(i64 " << ptr_val << ")\n";
    return "%" + tmp;
  }
  if (isPrimitiveDesc(elem) && elem.primitive == FarTypeId::F32) {
    ctx.out << "  %" << tmp << " = call " << kF64 << " @far_ptr_load_f32_as_f64(i64 " << ptr_val << ")\n";
    return "%" + tmp;
  }
  if (isPrimitiveDesc(elem) && elem.primitive == FarTypeId::F16) {
    std::string raw = ctx.fresh("d16");
    ctx.out << "  %" << raw << " = call i16 @far_ptr_load_f16(i64 " << ptr_val << ")\n";
    std::string ext = ctx.fresh("dwh");
    ctx.out << "  %" << ext << " = fpext " << kF16 << " %" << raw << " to " << kF64 << "\n";
    return "%" + ext;
  }
  int esz = elemSizeBytes(elem);
  if (esz == 8) {
    ctx.out << "  %" << tmp << " = call " << kI64 << " @far_ptr_load_i64(i64 " << ptr_val << ")\n";
    return "%" + tmp;
  }
  if (esz == 4) {
    ctx.out << "  %" << tmp << " = call " << kI64 << " @far_ptr_load_i32_as_i64(i64 " << ptr_val << ")\n";
  } else if (esz == 2) {
    ctx.out << "  %" << tmp << " = call " << kI64 << " @far_ptr_load_i16_as_i64(i64 " << ptr_val << ")\n";
  } else {
    ctx.out << "  %" << tmp << " = call " << kI64 << " @far_ptr_load_i8_as_i64(i64 " << ptr_val << ")\n";
  }
  if (isPrimitiveDesc(elem) && isIntegerType(elem.primitive) && typeInfo(elem.primitive).is_signed)
    return "%" + tmp;
  std::string wide = ctx.fresh("dw");
  const char* lt = ptrElemLlvmTypeFor(elem);
  ctx.out << "  %" << wide << " = zext " << lt << " %" << tmp << " to " << kI64 << "\n";
  return "%" + wide;
}

void emitPtrStore(MemCodegenCtx ctx, const TypeDesc& ptr_ty, const std::string& ptr_val,
                  const std::string& value) {
  TypeDesc elem = pointeeOf(ptr_ty);
  if (isPrimitiveDesc(elem) && elem.primitive == FarTypeId::F16) {
    std::string narrow = ctx.fresh("snh");
    ctx.out << "  %" << narrow << " = fptrunc " << kF64 << " " << value << " to " << kF16 << "\n";
    std::string bits = ctx.fresh("snb");
    ctx.out << "  %" << bits << " = bitcast " << kF16 << " %" << narrow << " to i16\n";
    ctx.out << "  call void @far_ptr_store_f16(i64 " << ptr_val << ", i16 %" << bits << ")\n";
    return;
  }
  if (isPrimitiveDesc(elem) && elem.primitive == FarTypeId::F32) {
    ctx.out << "  call void @far_ptr_store_f32(i64 " << ptr_val << ", " << kF64 << " " << value << ")\n";
    return;
  }
  if (isPrimitiveDesc(elem) && elem.primitive == FarTypeId::F64) {
    ctx.out << "  call void @far_ptr_store_f64(i64 " << ptr_val << ", " << kF64 << " " << value << ")\n";
    return;
  }
  int esz = elemSizeBytes(elem);
  if (esz == 8) {
    ctx.out << "  call void @far_ptr_store_i64(i64 " << ptr_val << ", " << kI64 << " " << value << ")\n";
    return;
  }
  if (esz == 4) {
    ctx.out << "  call void @far_ptr_store_i32(i64 " << ptr_val << ", " << kI64 << " " << value << ")\n";
    return;
  }
  if (esz == 2) {
    ctx.out << "  call void @far_ptr_store_i16(i64 " << ptr_val << ", " << kI64 << " " << value << ")\n";
    return;
  }
  ctx.out << "  call void @far_ptr_store_i8(i64 " << ptr_val << ", " << kI64 << " " << value << ")\n";
}

std::string emitAddressOf(MemCodegenCtx ctx, const std::string& slot_name, const char* llvm_slot_ty) {
  std::string ptr = ctx.fresh("addr");
  ctx.out << "  %" << ptr << " = ptrtoint " << llvm_slot_ty << "* %" << slot_name << " to i64\n";
  return "%" + ptr;
}

}  // namespace far
