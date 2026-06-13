#include "memory_codegen.h"

#include "error.h"
#include "type_desc.h"

namespace far {

std::string emitMemConstruct(MemCodegenCtx ctx, TypeForm form, const TypeDesc& elem_ty,
                             const std::vector<std::string>& arg_vals) {
  std::string tmp = ctx.fresh("mem");
  int esz = elemSizeBytes(elem_ty);
  switch (form) {
    case TypeForm::Box: {
      ctx.out << "  %" << tmp << " = call i64 @far_box_new(i64 " << esz << ")\n";
      if (!arg_vals.empty()) {
        std::string hp = ctx.fresh("binit");
        ctx.out << "  %" << hp << " = call i64 @far_box_get(i64 %" << tmp << ")\n";
        emitPtrStore(ctx, TypeDesc::pointer(elem_ty), "%" + hp, arg_vals[0]);
      }
      break;
    }
    case TypeForm::Rc: {
      ctx.out << "  %" << tmp << " = call i64 @far_rc_new(i64 " << esz << ")\n";
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
  ctx.out << "  %" << cnt << " = mul i64 " << count_val << ", " << esz << "\n";
  ctx.out << "  %" << bytes << " = alloca i8, i64 %" << cnt << "\n";
  std::string ptr = ctx.fresh("stkp");
  ctx.out << "  %" << ptr << " = ptrtoint i8* %" << bytes << " to i64\n";
  return "%" + ptr;
}

std::string emitPtrDeref(MemCodegenCtx ctx, const TypeDesc& ptr_ty, const std::string& ptr_val) {
  TypeDesc elem = pointeeOf(ptr_ty);
  int esz = elemSizeBytes(elem);
  if (esz == 8) {
    std::string p = ctx.fresh("dptr");
    ctx.out << "  %" << p << " = inttoptr i64 " << ptr_val << " to i64*\n";
    std::string tmp = ctx.fresh("dval");
    ctx.out << "  %" << tmp << " = load i64, i64* %" << p << "\n";
    return "%" + tmp;
  }
  std::string p = ctx.fresh("dptr");
  ctx.out << "  %" << p << " = inttoptr i64 " << ptr_val << " to i8*\n";
  std::string tmp = ctx.fresh("dval");
  ctx.out << "  %" << tmp << " = load i8, i8* %" << p << "\n";
  std::string wide = ctx.fresh("dw");
  ctx.out << "  %" << wide << " = zext i8 %" << tmp << " to i64\n";
  return "%" + wide;
}

void emitPtrStore(MemCodegenCtx ctx, const TypeDesc& ptr_ty, const std::string& ptr_val,
                  const std::string& value) {
  TypeDesc elem = pointeeOf(ptr_ty);
  int esz = elemSizeBytes(elem);
  if (esz == 8) {
    std::string p = ctx.fresh("sptr");
    ctx.out << "  %" << p << " = inttoptr i64 " << ptr_val << " to i64*\n";
    ctx.out << "  store i64 " << value << ", i64* %" << p << "\n";
    return;
  }
  std::string p = ctx.fresh("sptr");
  ctx.out << "  %" << p << " = inttoptr i64 " << ptr_val << " to i8*\n";
  std::string narrow = ctx.fresh("sn");
  ctx.out << "  %" << narrow << " = trunc i64 " << value << " to i8\n";
  ctx.out << "  store i8 %" << narrow << ", i8* %" << p << "\n";
}

std::string emitAddressOf(MemCodegenCtx ctx, const std::string& slot_name, const char* llvm_slot_ty) {
  std::string ptr = ctx.fresh("addr");
  ctx.out << "  %" << ptr << " = ptrtoint " << llvm_slot_ty << "* %" << slot_name << " to i64\n";
  return "%" + ptr;
}

}  // namespace far
