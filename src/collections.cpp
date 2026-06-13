#include "collections.h"

namespace far {

static const CollConstructorInfo kCollConstructors[] = {
    {"List", TypeForm::List, 0, true},
    {"FixedArray", TypeForm::FixedArray, -1, true},
    {"Dict", TypeForm::Dict, 0, true},
    {"Set", TypeForm::Set, 0, true},
    {"Queue", TypeForm::Queue, 0, true},
    {"Stack", TypeForm::Stack, 0, true},
    {"LinkedList", TypeForm::LinkedList, 0, true},
    {"Span", TypeForm::Span, 0, true},
    {"Slice", TypeForm::Slice, 0, true},
    {"Tuple", TypeForm::Tuple, -1, true},
    {"Range", TypeForm::Range, 2, false},
};

static const CollMethodInfo kListMethods[] = {
    {CollMethodId::Push, "push", 1},
    {CollMethodId::Pop, "pop", 0},         {CollMethodId::Insert, "insert", 2},
    {CollMethodId::Remove, "remove", 1},   {CollMethodId::Clear, "clear", 0},
    {CollMethodId::Get, "get", 1},         {CollMethodId::Set, "set", 2},
    {CollMethodId::Slice, "slice", 2},
};

static const CollMethodInfo kDictMethods[] = {
    {CollMethodId::Get, "get", 1},
    {CollMethodId::Set, "set", 2},           {CollMethodId::ContainsKey, "contains_key", 1},
    {CollMethodId::Remove, "remove", 1},     {CollMethodId::Keys, "keys", 0},
    {CollMethodId::Values, "values", 0},
};

static const CollMethodInfo kSetMethods[] = {
    {CollMethodId::Add, "add", 1},
    {CollMethodId::Remove, "remove", 1},   {CollMethodId::Contains, "contains", 1},
};

static const CollMethodInfo kQueueMethods[] = {
    {CollMethodId::Enqueue, "enqueue", 1},
    {CollMethodId::Dequeue, "dequeue", 0}, {CollMethodId::Peek, "peek", 0},
};

static const CollMethodInfo kStackMethods[] = {
    {CollMethodId::Push, "push", 1},
    {CollMethodId::Pop, "pop", 0},      {CollMethodId::Peek, "peek", 0},
};

static const CollMethodInfo kLinkedListMethods[] = {
    {CollMethodId::PushFront, "push_front", 1},
    {CollMethodId::PushBack, "push_back", 1},  {CollMethodId::PopFront, "pop_front", 0},
    {CollMethodId::PopBack, "pop_back", 0},
};

static const CollMethodInfo kArrayMethods[] = {
    {CollMethodId::Slice, "slice", 2},
};

static const CollMethodInfo kSliceMethods[] = {
    {CollMethodId::Slice, "slice", 2},
};

static const CollMethodInfo kRangeMethods[] = {
    {CollMethodId::Contains, "contains", 1},
};

const CollConstructorInfo* lookupCollConstructor(const std::string& name) {
  for (const auto& c : kCollConstructors) {
    if (name == c.name)
      return &c;
  }
  return nullptr;
}

const CollMethodInfo* lookupCollMethod(TypeForm form, const std::string& name) {
  const CollMethodInfo* table = nullptr;
  size_t count = 0;
  switch (form) {
    case TypeForm::Array:
      table = kArrayMethods;
      count = sizeof(kArrayMethods) / sizeof(kArrayMethods[0]);
      break;
    case TypeForm::Slice:
      table = kSliceMethods;
      count = sizeof(kSliceMethods) / sizeof(kSliceMethods[0]);
      break;
    case TypeForm::List:
      table = kListMethods;
      count = sizeof(kListMethods) / sizeof(kListMethods[0]);
      break;
    case TypeForm::Dict:
      table = kDictMethods;
      count = sizeof(kDictMethods) / sizeof(kDictMethods[0]);
      break;
    case TypeForm::Set:
      table = kSetMethods;
      count = sizeof(kSetMethods) / sizeof(kSetMethods[0]);
      break;
    case TypeForm::Queue:
      table = kQueueMethods;
      count = sizeof(kQueueMethods) / sizeof(kQueueMethods[0]);
      break;
    case TypeForm::Stack:
      table = kStackMethods;
      count = sizeof(kStackMethods) / sizeof(kStackMethods[0]);
      break;
    case TypeForm::LinkedList:
      table = kLinkedListMethods;
      count = sizeof(kLinkedListMethods) / sizeof(kLinkedListMethods[0]);
      break;
    case TypeForm::Range:
      table = kRangeMethods;
      count = sizeof(kRangeMethods) / sizeof(kRangeMethods[0]);
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

TypeDesc collMethodRetType(TypeForm form, CollMethodId id, const TypeDesc& recv, const TypeDesc& arg) {
  TypeDesc elem = recv.args.empty() ? TypeDesc::prim(FarTypeId::I64) : recv.args[0];
  switch (id) {
    case CollMethodId::Len:
    case CollMethodId::Contains:
    case CollMethodId::ContainsKey:
      return TypeDesc::prim(FarTypeId::I64);
    case CollMethodId::Push:
    case CollMethodId::Insert:
    case CollMethodId::Remove:
    case CollMethodId::Clear:
    case CollMethodId::Set:
    case CollMethodId::Add:
    case CollMethodId::Enqueue:
    case CollMethodId::PushFront:
    case CollMethodId::PushBack:
      return recv;
    case CollMethodId::Pop:
    case CollMethodId::Dequeue:
    case CollMethodId::Get:
    case CollMethodId::Peek:
    case CollMethodId::PopFront:
    case CollMethodId::PopBack:
      return elem;
    case CollMethodId::Keys:
      return TypeDesc::list(recv.args[0]);
    case CollMethodId::Values:
      return TypeDesc::list(recv.args[1]);
    case CollMethodId::Slice:
      return TypeDesc::slice(elem);
    default:
      return recv;
  }
}

void declareCollectionTypes(std::ostringstream& out) {
  out << "%FarTypedArray = type opaque\n";
  out << "%FarList = type opaque\n";
  out << "%FarDict = type opaque\n";
  out << "%FarSet = type opaque\n";
  out << "%FarQueue = type opaque\n";
  out << "%FarStack = type opaque\n";
  out << "%FarLinkedList = type opaque\n";
  out << "%FarSlice = type opaque\n";
  out << "%FarSpan = type { i64, i64 }\n";
  out << "%FarRange = type { i64, i64, i64 }\n";
}

void declareCollectionRuntime(std::ostringstream& out) {
  out << "declare i64 @far_tarray_new(i64, i16, i64)\n";
  out << "declare i64 @far_tarray_len(i64)\n";
  out << "declare i64 @far_tarray_get(i64, i64)\n";
  out << "declare void @far_tarray_set(i64, i64, i64)\n";
  out << "declare void @far_print_tarray(i64)\n";

  out << "declare i64 @far_list_new(i16, i64)\n";
  out << "declare i64 @far_list_len(i64)\n";
  out << "declare void @far_list_push(i64, i64)\n";
  out << "declare i64 @far_list_pop(i64)\n";
  out << "declare i64 @far_list_get(i64, i64)\n";
  out << "declare void @far_list_set(i64, i64, i64)\n";
  out << "declare void @far_list_clear(i64)\n";
  out << "declare i64 @far_list_slice(i64, i64, i64)\n";
  out << "declare void @far_list_insert(i64, i64, i64)\n";
  out << "declare void @far_list_remove_at(i64, i64)\n";
  out << "declare void @far_print_list(i64)\n";

  out << "declare i64 @far_dict_new(i16, i16)\n";
  out << "declare i64 @far_dict_len(i64)\n";
  out << "declare i64 @far_dict_get(i64, i64)\n";
  out << "declare void @far_dict_set(i64, i64, i64)\n";
  out << "declare i64 @far_dict_contains_key(i64, i64)\n";
  out << "declare void @far_dict_remove(i64, i64)\n";
  out << "declare i64 @far_dict_keys(i64)\n";
  out << "declare i64 @far_dict_values(i64)\n";

  out << "declare i64 @far_set_new(i16)\n";
  out << "declare i64 @far_set_len(i64)\n";
  out << "declare void @far_set_add(i64, i64)\n";
  out << "declare i64 @far_set_contains(i64, i64)\n";
  out << "declare void @far_set_remove(i64, i64)\n";

  out << "declare i64 @far_queue_new(i16)\n";
  out << "declare i64 @far_queue_len(i64)\n";
  out << "declare void @far_queue_enqueue(i64, i64)\n";
  out << "declare i64 @far_queue_dequeue(i64)\n";
  out << "declare i64 @far_queue_peek(i64)\n";

  out << "declare i64 @far_stack_new(i16)\n";
  out << "declare i64 @far_stack_len(i64)\n";
  out << "declare void @far_stack_push(i64, i64)\n";
  out << "declare i64 @far_stack_pop(i64)\n";
  out << "declare i64 @far_stack_peek(i64)\n";

  out << "declare i64 @far_llist_new(i16)\n";
  out << "declare i64 @far_llist_len(i64)\n";
  out << "declare void @far_llist_push_front(i64, i64)\n";
  out << "declare void @far_llist_push_back(i64, i64)\n";
  out << "declare i64 @far_llist_pop_front(i64)\n";
  out << "declare i64 @far_llist_pop_back(i64)\n";

  out << "declare i64 @far_slice_new(i64, i64, i64, i64)\n";
  out << "declare i64 @far_range_new(i64, i64, i64)\n";
  out << "declare i64 @far_range_len(i64)\n";
  out << "declare i64 @far_range_contains(i64, i64)\n";
}

}  // namespace far
