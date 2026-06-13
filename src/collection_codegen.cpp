#include "collection_codegen.h"

#include "collections.h"
#include "error.h"
#include "type_desc.h"

namespace far {

static uint16_t tagOf(const TypeDesc& elem) {
  if (isPrimitiveDesc(elem))
    return typeTag(elem);
  return 0;
}

std::string emitTypedArrayLit(CollCodegenCtx ctx, const ArrayLit& lit, TypeDesc elem) {
  std::string handle = ctx.fresh("tarr");
  uint16_t tag = tagOf(elem);
  ctx.out << "  %" << handle << " = call " << "i64"
          << " @far_tarray_new(i64 " << lit.elements.size() << ", i16 " << tag << ", i64 "
          << elemSizeBytes(elem) << ")\n";
  for (size_t i = 0; i < lit.elements.size(); ++i) {
    std::string val = ctx.emit_expr(*lit.elements[i]);
    ctx.out << "  call void @far_tarray_set(i64 %" << handle << ", i64 " << i << ", i64 " << val
            << ")\n";
  }
  return "%" + handle;
}

std::string emitDictLit(CollCodegenCtx ctx, const DictLit& lit, TypeDesc key, TypeDesc val) {
  std::string handle = ctx.fresh("dict");
  ctx.out << "  %" << handle << " = call i64 @far_dict_new(i16 " << typeTag(key) << ", i16 "
          << typeTag(val) << ")\n";
  for (const auto& entry : lit.entries) {
    std::string k = ctx.emit_expr(*entry.key);
    std::string v = ctx.emit_expr(*entry.value);
    ctx.out << "  call void @far_dict_set(i64 %" << handle << ", i64 " << k << ", i64 " << v << ")\n";
  }
  return "%" + handle;
}

std::string emitCollectionIndex(CollCodegenCtx ctx, const TypeDesc& arr_ty, const std::string& arr,
                                const std::string& idx) {
  std::string tmp = ctx.fresh("idx");
  switch (arr_ty.form) {
    case TypeForm::Array:
    case TypeForm::Slice:
      ctx.out << "  %" << tmp << " = call i64 @far_tarray_get(i64 " << arr << ", i64 " << idx << ")\n";
      break;
    case TypeForm::List:
      ctx.out << "  %" << tmp << " = call i64 @far_list_get(i64 " << arr << ", i64 " << idx << ")\n";
      break;
    case TypeForm::Dict:
      ctx.out << "  %" << tmp << " = call i64 @far_dict_get(i64 " << arr << ", i64 " << idx << ")\n";
      break;
    default:
      ctx.out << "  %" << tmp << " = call i64 @far_array_get(i64 " << arr << ", i64 " << idx << ")\n";
      break;
  }
  return "%" + tmp;
}

void emitCollectionStore(CollCodegenCtx ctx, const TypeDesc& arr_ty, const std::string& arr,
                         const std::string& idx, const std::string& value) {
  switch (arr_ty.form) {
    case TypeForm::Array:
    case TypeForm::Slice:
      ctx.out << "  call void @far_tarray_set(i64 " << arr << ", i64 " << idx << ", i64 " << value
              << ")\n";
      break;
    case TypeForm::List:
      ctx.out << "  call void @far_list_set(i64 " << arr << ", i64 " << idx << ", i64 " << value
              << ")\n";
      break;
    case TypeForm::Dict:
      ctx.out << "  call void @far_dict_set(i64 " << arr << ", i64 " << idx << ", i64 " << value
              << ")\n";
      break;
    default:
      ctx.out << "  call void @far_array_set(i64 " << arr << ", i64 " << idx << ", i64 " << value
              << ")\n";
      break;
  }
}

std::string emitCollectionLen(CollCodegenCtx ctx, const TypeDesc& arr_ty, const std::string& arr) {
  std::string tmp = ctx.fresh("len");
  switch (arr_ty.form) {
    case TypeForm::Array:
    case TypeForm::Slice:
      ctx.out << "  %" << tmp << " = call i64 @far_tarray_len(i64 " << arr << ")\n";
      break;
    case TypeForm::List:
      ctx.out << "  %" << tmp << " = call i64 @far_list_len(i64 " << arr << ")\n";
      break;
    default:
      ctx.out << "  %" << tmp << " = call i64 @far_array_len(i64 " << arr << ")\n";
      break;
  }
  return "%" + tmp;
}

std::string emitCollectionSlice(CollCodegenCtx ctx, const TypeDesc& arr_ty, const std::string& arr,
                                const std::string& start, const std::string& end) {
  std::string tmp = ctx.fresh("sl");
  TypeDesc elem = elemTypeOf(arr_ty);
  ctx.out << "  %" << tmp << " = call i64 @far_slice_new(i64 " << arr << ", i64 " << start << ", i64 "
          << end << ", i16 " << tagOf(elem) << ")\n";
  return "%" + tmp;
}

std::string emitCollectionMethod(CollCodegenCtx ctx, const MethodCall& call, const TypeDesc& obj_ty,
                                 const TypeDesc& ret_ty) {
  const CollMethodInfo* mi = lookupCollMethod(obj_ty.form, call.method);
  if (!mi)
    throw FarError("unknown collection method");
  std::string obj = ctx.emit_expr(*call.object);
  std::string tmp = ctx.fresh("cm");
  switch (mi->id) {
    case CollMethodId::Len:
      if (obj_ty.form == TypeForm::Array || obj_ty.form == TypeForm::Slice)
        ctx.out << "  %" << tmp << " = call i64 @far_tarray_len(i64 " << obj << ")\n";
      else if (obj_ty.form == TypeForm::List)
        ctx.out << "  %" << tmp << " = call i64 @far_list_len(i64 " << obj << ")\n";
      else if (obj_ty.form == TypeForm::Dict)
        ctx.out << "  %" << tmp << " = call i64 @far_dict_len(i64 " << obj << ")\n";
      else if (obj_ty.form == TypeForm::Set)
        ctx.out << "  %" << tmp << " = call i64 @far_set_len(i64 " << obj << ")\n";
      else if (obj_ty.form == TypeForm::Queue)
        ctx.out << "  %" << tmp << " = call i64 @far_queue_len(i64 " << obj << ")\n";
      else if (obj_ty.form == TypeForm::Stack)
        ctx.out << "  %" << tmp << " = call i64 @far_stack_len(i64 " << obj << ")\n";
      else if (obj_ty.form == TypeForm::LinkedList)
        ctx.out << "  %" << tmp << " = call i64 @far_llist_len(i64 " << obj << ")\n";
      else if (obj_ty.form == TypeForm::Range)
        ctx.out << "  %" << tmp << " = call i64 @far_range_len(i64 " << obj << ")\n";
      else
        ctx.out << "  %" << tmp << " = call i64 @far_array_len(i64 " << obj << ")\n";
      break;
    case CollMethodId::Push: {
      std::string arg = ctx.emit_expr(*call.args[0]);
      if (obj_ty.form == TypeForm::Stack)
        ctx.out << "  call void @far_stack_push(i64 " << obj << ", i64 " << arg << ")\n";
      else
        ctx.out << "  call void @far_list_push(i64 " << obj << ", i64 " << arg << ")\n";
      return obj;
    }
    case CollMethodId::Pop:
      if (obj_ty.form == TypeForm::Stack)
        ctx.out << "  %" << tmp << " = call i64 @far_stack_pop(i64 " << obj << ")\n";
      else
        ctx.out << "  %" << tmp << " = call i64 @far_list_pop(i64 " << obj << ")\n";
      break;
    case CollMethodId::Get: {
      std::string arg = ctx.emit_expr(*call.args[0]);
      if (obj_ty.form == TypeForm::Dict)
        ctx.out << "  %" << tmp << " = call i64 @far_dict_get(i64 " << obj << ", i64 " << arg << ")\n";
      else
        ctx.out << "  %" << tmp << " = call i64 @far_list_get(i64 " << obj << ", i64 " << arg << ")\n";
      break;
    }
    case CollMethodId::Set: {
      std::string k = ctx.emit_expr(*call.args[0]);
      std::string v = ctx.emit_expr(*call.args[1]);
      if (obj_ty.form == TypeForm::Dict)
        ctx.out << "  call void @far_dict_set(i64 " << obj << ", i64 " << k << ", i64 " << v << ")\n";
      else
        ctx.out << "  call void @far_list_set(i64 " << obj << ", i64 " << k << ", i64 " << v << ")\n";
      return obj;
    }
    case CollMethodId::ContainsKey: {
      std::string arg = ctx.emit_expr(*call.args[0]);
      ctx.out << "  %" << tmp << " = call i64 @far_dict_contains_key(i64 " << obj << ", i64 " << arg
              << ")\n";
      break;
    }
    case CollMethodId::Remove: {
      std::string arg = ctx.emit_expr(*call.args[0]);
      if (obj_ty.form == TypeForm::Dict)
        ctx.out << "  call void @far_dict_remove(i64 " << obj << ", i64 " << arg << ")\n";
      else if (obj_ty.form == TypeForm::Set)
        ctx.out << "  call void @far_set_remove(i64 " << obj << ", i64 " << arg << ")\n";
      else if (obj_ty.form == TypeForm::List)
        ctx.out << "  call void @far_list_remove_at(i64 " << obj << ", i64 " << arg << ")\n";
      else
        throw FarError("unsupported collection method: " + call.method);
      return obj;
    }
    case CollMethodId::Insert: {
      std::string idx = ctx.emit_expr(*call.args[0]);
      std::string val = ctx.emit_expr(*call.args[1]);
      if (obj_ty.form == TypeForm::List)
        ctx.out << "  call void @far_list_insert(i64 " << obj << ", i64 " << idx << ", i64 " << val << ")\n";
      else
        throw FarError("unsupported collection method: " + call.method);
      return obj;
    }
    case CollMethodId::Clear:
      if (obj_ty.form == TypeForm::List)
        ctx.out << "  call void @far_list_clear(i64 " << obj << ")\n";
      else
        throw FarError("unsupported collection method: " + call.method);
      return obj;
    case CollMethodId::Keys:
      ctx.out << "  %" << tmp << " = call i64 @far_dict_keys(i64 " << obj << ")\n";
      break;
    case CollMethodId::Values:
      ctx.out << "  %" << tmp << " = call i64 @far_dict_values(i64 " << obj << ")\n";
      break;
    case CollMethodId::Add: {
      std::string arg = ctx.emit_expr(*call.args[0]);
      ctx.out << "  call void @far_set_add(i64 " << obj << ", i64 " << arg << ")\n";
      return obj;
    }
    case CollMethodId::Contains: {
      std::string arg = ctx.emit_expr(*call.args[0]);
      if (obj_ty.form == TypeForm::Range)
        ctx.out << "  %" << tmp << " = call i64 @far_range_contains(i64 " << obj << ", i64 " << arg
                << ")\n";
      else
        ctx.out << "  %" << tmp << " = call i64 @far_set_contains(i64 " << obj << ", i64 " << arg
                << ")\n";
      break;
    }
    case CollMethodId::Enqueue: {
      std::string arg = ctx.emit_expr(*call.args[0]);
      ctx.out << "  call void @far_queue_enqueue(i64 " << obj << ", i64 " << arg << ")\n";
      return obj;
    }
    case CollMethodId::Dequeue:
      ctx.out << "  %" << tmp << " = call i64 @far_queue_dequeue(i64 " << obj << ")\n";
      break;
    case CollMethodId::Peek:
      if (obj_ty.form == TypeForm::Queue)
        ctx.out << "  %" << tmp << " = call i64 @far_queue_peek(i64 " << obj << ")\n";
      else
        ctx.out << "  %" << tmp << " = call i64 @far_stack_peek(i64 " << obj << ")\n";
      break;
    case CollMethodId::PushFront: {
      std::string arg = ctx.emit_expr(*call.args[0]);
      ctx.out << "  call void @far_llist_push_front(i64 " << obj << ", i64 " << arg << ")\n";
      return obj;
    }
    case CollMethodId::PushBack: {
      std::string arg = ctx.emit_expr(*call.args[0]);
      ctx.out << "  call void @far_llist_push_back(i64 " << obj << ", i64 " << arg << ")\n";
      return obj;
    }
    case CollMethodId::PopFront:
      ctx.out << "  %" << tmp << " = call i64 @far_llist_pop_front(i64 " << obj << ")\n";
      break;
    case CollMethodId::PopBack:
      ctx.out << "  %" << tmp << " = call i64 @far_llist_pop_back(i64 " << obj << ")\n";
      break;
    case CollMethodId::Slice: {
      std::string a = ctx.emit_expr(*call.args[0]);
      std::string b = ctx.emit_expr(*call.args[1]);
      if (obj_ty.form == TypeForm::List)
        ctx.out << "  %" << tmp << " = call i64 @far_list_slice(i64 " << obj << ", i64 " << a << ", i64 "
                << b << ")\n";
      else
        ctx.out << "  %" << tmp << " = call i64 @far_slice_new(i64 " << obj << ", i64 " << a << ", i64 "
                << b << ", i16 " << tagOf(elemTypeOf(obj_ty)) << ")\n";
      (void)ret_ty;
      break;
    }
    default:
      throw FarError("unsupported collection method: " + call.method);
  }
  return "%" + tmp;
}

void emitCollectionPrint(CollCodegenCtx ctx, const TypeDesc& ty, const std::string& val) {
  if (ty.form == TypeForm::Array || ty.form == TypeForm::Slice) {
    ctx.out << "  call void @far_print_tarray(i64 " << val << ")\n";
    return;
  }
  if (ty.form == TypeForm::List) {
    ctx.out << "  call void @far_print_list(i64 " << val << ")\n";
    return;
  }
  ctx.out << "  call void @far_print_i64(i64 " << val << ")\n";
}

std::string emitRangeNew(CollCodegenCtx ctx, int64_t start, int64_t end, int64_t step) {
  std::string tmp = ctx.fresh("rng");
  ctx.out << "  %" << tmp << " = call i64 @far_range_new(i64 " << start << ", i64 " << end << ", i64 "
          << step << ")\n";
  return "%" + tmp;
}

std::string emitCollConstructor(CollCodegenCtx ctx, const std::string& name, const TypeDesc& ty) {
  std::string tmp = ctx.fresh("coll");
  if (name == "Range") {
    throw FarError("Range constructor requires arguments");
  }
  if (name == "Dict") {
    TypeDesc key = ty.args.size() >= 1 ? ty.args[0] : TypeDesc::prim(FarTypeId::I64);
    TypeDesc val = ty.args.size() >= 2 ? ty.args[1] : TypeDesc::prim(FarTypeId::I64);
    ctx.out << "  %" << tmp << " = call i64 @far_dict_new(i16 " << typeTag(key) << ", i16 "
            << typeTag(val) << ")\n";
    return "%" + tmp;
  }
  if (name == "List") {
    TypeDesc elem = ty.args.empty() ? TypeDesc::prim(FarTypeId::I64) : ty.args[0];
    ctx.out << "  %" << tmp << " = call i64 @far_list_new(i16 " << typeTag(elem) << ", i64 4)\n";
    return "%" + tmp;
  }
  if (name == "Set") {
    TypeDesc elem = ty.args.empty() ? TypeDesc::prim(FarTypeId::I64) : ty.args[0];
    ctx.out << "  %" << tmp << " = call i64 @far_set_new(i16 " << typeTag(elem) << ")\n";
    return "%" + tmp;
  }
  if (name == "Queue" || name == "Stack" || name == "LinkedList") {
    TypeDesc elem = ty.args.empty() ? TypeDesc::prim(FarTypeId::I64) : ty.args[0];
    if (name == "Queue")
      ctx.out << "  %" << tmp << " = call i64 @far_queue_new(i16 " << typeTag(elem) << ")\n";
    else if (name == "Stack")
      ctx.out << "  %" << tmp << " = call i64 @far_stack_new(i16 " << typeTag(elem) << ")\n";
    else
      ctx.out << "  %" << tmp << " = call i64 @far_llist_new(i16 " << typeTag(elem) << ")\n";
    return "%" + tmp;
  }
  TypeDesc elem = ty.args.empty() ? TypeDesc::prim(FarTypeId::I64) : ty.args[0];
  ctx.out << "  %" << tmp << " = call i64 @far_tarray_new(i64 0, i16 " << typeTag(elem) << ", i64 "
          << elemSizeBytes(elem) << ")\n";
  return "%" + tmp;
}

}  // namespace far
