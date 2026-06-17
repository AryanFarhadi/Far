#include "concurrency.h"

#include "error.h"

namespace far {

static const ConcConstructorInfo kConcConstructors[] = {
    {"Channel", TypeForm::Channel, 1, true},
    {"Mutex", TypeForm::Mutex, 0, false},
    {"Semaphore", TypeForm::Semaphore, 1, false},
    {"Atomic", TypeForm::Atomic, 1, true},
    {"ThreadPool", TypeForm::ThreadPool, 1, false},
    {"LockFreeQueue", TypeForm::LockFreeQueue, 1, true},
    {"Task", TypeForm::Task, 0, false},
};

static const ConcMethodInfo kChannelMethods[] = {
    {ConcMethodId::Send, "send", 1}, {ConcMethodId::Recv, "recv", 0},
    {ConcMethodId::TryRecv, "try_recv", 0}, {ConcMethodId::TrySend, "try_send", 1},
    {ConcMethodId::Close, "close", 0}, {ConcMethodId::IsClosed, "is_closed", 0},
    {ConcMethodId::Pending, "pending", 0},
};

static const ConcMethodInfo kMutexMethods[] = {
    {ConcMethodId::Lock, "lock", 0}, {ConcMethodId::Unlock, "unlock", 0},
};

static const ConcMethodInfo kSemaphoreMethods[] = {
    {ConcMethodId::Wait, "wait", 0}, {ConcMethodId::Signal, "signal", 0},
    {ConcMethodId::TryWait, "try_wait", 0},
};

static const ConcMethodInfo kAtomicMethods[] = {
    {ConcMethodId::Load, "load", 0},
    {ConcMethodId::Store, "store", 1},
    {ConcMethodId::FetchAdd, "fetch_add", 1},
    {ConcMethodId::CompareExchange, "compare_exchange", 2},
};

static const ConcMethodInfo kThreadPoolMethods[] = {
    {ConcMethodId::Submit, "submit", 2},
    {ConcMethodId::Shutdown, "shutdown", 0},
};

static const ConcMethodInfo kLockFreeQueueMethods[] = {
    {ConcMethodId::Push, "push", 1}, {ConcMethodId::Pop, "pop", 0},
};

static const ConcMethodInfo kTaskMethods[] = {
    {ConcMethodId::Await, "await", 0},
};

static const ConcMethodInfo kActorMethods[] = {
    {ConcMethodId::Tell, "tell", 1}, {ConcMethodId::Ask, "ask", 1}, {ConcMethodId::Stop, "stop", 0},
};

const ConcConstructorInfo* lookupConcConstructor(const std::string& name) {
  for (const auto& c : kConcConstructors) {
    if (name == c.name)
      return &c;
  }
  return nullptr;
}

const ConcMethodInfo* lookupConcMethod(TypeForm form, const std::string& name) {
  const ConcMethodInfo* table = nullptr;
  size_t count = 0;
  switch (form) {
    case TypeForm::Channel:
      table = kChannelMethods;
      count = sizeof(kChannelMethods) / sizeof(kChannelMethods[0]);
      break;
    case TypeForm::Mutex:
      table = kMutexMethods;
      count = sizeof(kMutexMethods) / sizeof(kMutexMethods[0]);
      break;
    case TypeForm::Semaphore:
      table = kSemaphoreMethods;
      count = sizeof(kSemaphoreMethods) / sizeof(kSemaphoreMethods[0]);
      break;
    case TypeForm::Atomic:
      table = kAtomicMethods;
      count = sizeof(kAtomicMethods) / sizeof(kAtomicMethods[0]);
      break;
    case TypeForm::ThreadPool:
      table = kThreadPoolMethods;
      count = sizeof(kThreadPoolMethods) / sizeof(kThreadPoolMethods[0]);
      break;
    case TypeForm::LockFreeQueue:
      table = kLockFreeQueueMethods;
      count = sizeof(kLockFreeQueueMethods) / sizeof(kLockFreeQueueMethods[0]);
      break;
    case TypeForm::Task:
      table = kTaskMethods;
      count = sizeof(kTaskMethods) / sizeof(kTaskMethods[0]);
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

const ConcMethodInfo* lookupActorMethod(const std::string& name) {
  for (size_t i = 0; i < sizeof(kActorMethods) / sizeof(kActorMethods[0]); ++i) {
    if (name == kActorMethods[i].name)
      return &kActorMethods[i];
  }
  return nullptr;
}

TypeDesc concMethodRetType(TypeForm form, ConcMethodId id, const TypeDesc& recv, const TypeDesc& arg) {
  (void)recv;
  (void)arg;
  switch (form) {
    case TypeForm::Channel:
      if ((id == ConcMethodId::Recv || id == ConcMethodId::TryRecv) && !recv.args.empty())
        return recv.args[0];
      return TypeDesc::prim(FarTypeId::I64);
    case TypeForm::Atomic:
      if (id == ConcMethodId::Load || id == ConcMethodId::FetchAdd ||
          id == ConcMethodId::CompareExchange)
        return recv.args.empty() ? TypeDesc::prim(FarTypeId::I64) : recv.args[0];
      return TypeDesc::prim(FarTypeId::I64);
    case TypeForm::LockFreeQueue:
      if (id == ConcMethodId::Pop && !recv.args.empty())
        return recv.args[0];
      return TypeDesc::prim(FarTypeId::I64);
    case TypeForm::ThreadPool:
    case TypeForm::Task:
      if (id == ConcMethodId::Submit || id == ConcMethodId::Await)
        return TypeDesc::task();
      return TypeDesc::prim(FarTypeId::I64);
    default:
      return TypeDesc::prim(FarTypeId::I64);
  }
}

void declareConcurrencyRuntime(std::ostringstream& out) {
  out << "declare i64 @far_channel_new(i64)\n";
  out << "declare i64 @far_channel_send(i64, i64)\n";
  out << "declare i64 @far_channel_recv(i64)\n";
  out << "declare i64 @far_channel_try_recv(i64)\n";
  out << "declare i64 @far_channel_try_send(i64, i64)\n";
  out << "declare void @far_channel_close(i64)\n";
  out << "declare i64 @far_channel_is_closed(i64)\n";
  out << "declare i64 @far_channel_pending(i64)\n";
  out << "declare void @far_channel_drop(i64)\n";
  out << "declare i64 @far_mutex_new()\n";
  out << "declare void @far_mutex_lock(i64)\n";
  out << "declare void @far_mutex_unlock(i64)\n";
  out << "declare void @far_mutex_drop(i64)\n";
  out << "declare i64 @far_semaphore_new(i64)\n";
  out << "declare void @far_semaphore_wait(i64)\n";
  out << "declare i64 @far_semaphore_try_wait(i64)\n";
  out << "declare void @far_semaphore_signal(i64)\n";
  out << "declare void @far_semaphore_drop(i64)\n";
  out << "declare i64 @far_atomic_new(i64)\n";
  out << "declare i64 @far_atomic_load(i64)\n";
  out << "declare void @far_atomic_store(i64, i64)\n";
  out << "declare i64 @far_atomic_fetch_add(i64, i64)\n";
  out << "declare i64 @far_atomic_compare_exchange(i64, i64, i64)\n";
  out << "declare void @far_atomic_drop(i64)\n";
  out << "declare i64 @far_threadpool_new(i64)\n";
  out << "declare i64 @far_threadpool_submit(i64, i8*, i64)\n";
  out << "declare void @far_threadpool_shutdown(i64)\n";
  out << "declare void @far_threadpool_drop(i64)\n";
  out << "declare i64 @far_lfqueue_new(i64)\n";
  out << "declare i64 @far_lfqueue_push(i64, i64)\n";
  out << "declare i64 @far_lfqueue_pop(i64)\n";
  out << "declare void @far_lfqueue_drop(i64)\n";
  out << "declare i64 @far_parallel_for(i8*, i64, i64)\n";
  out << "declare i64 @far_parallel_for_cl(i64, i64, i64)\n";
  out << "declare i64 @far_actor_spawn(i8*, i64)\n";
  out << "declare void @far_actor_tell(i64, i64)\n";
  out << "declare i64 @far_actor_ask(i64, i64)\n";
  out << "declare void @far_actor_stop(i64)\n";
}

}  // namespace far
