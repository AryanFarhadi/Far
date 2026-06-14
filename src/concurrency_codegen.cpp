#include "concurrency_codegen.h"

#include "error.h"

namespace far {

std::string emitConcConstruct(ConcCodegenCtx ctx, TypeForm form, const TypeDesc& elem_ty,
                            const std::vector<std::string>& arg_vals) {
  std::string tmp = ctx.fresh("conc");
  switch (form) {
    case TypeForm::Channel:
      ctx.out << "  %" << tmp << " = call i64 @far_channel_new(i64 "
              << (arg_vals.empty() ? "8" : arg_vals[0]) << ")\n";
      break;
    case TypeForm::Mutex:
      ctx.out << "  %" << tmp << " = call i64 @far_mutex_new()\n";
      break;
    case TypeForm::Semaphore:
      ctx.out << "  %" << tmp << " = call i64 @far_semaphore_new(i64 "
              << (arg_vals.empty() ? "1" : arg_vals[0]) << ")\n";
      break;
    case TypeForm::Atomic:
      ctx.out << "  %" << tmp << " = call i64 @far_atomic_new(i64 "
              << (arg_vals.empty() ? "0" : arg_vals[0]) << ")\n";
      (void)elem_ty;
      break;
    case TypeForm::ThreadPool:
      ctx.out << "  %" << tmp << " = call i64 @far_threadpool_new(i64 "
              << (arg_vals.empty() ? "2" : arg_vals[0]) << ")\n";
      break;
    case TypeForm::LockFreeQueue:
      ctx.out << "  %" << tmp << " = call i64 @far_lfqueue_new(i64 "
              << (arg_vals.empty() ? "16" : arg_vals[0]) << ")\n";
      break;
    default:
      throw FarError("invalid concurrency constructor");
  }
  return "%" + tmp;
}

std::string emitConcMethod(ConcCodegenCtx ctx, const MethodCall& call, const TypeDesc& recv_ty,
                           const std::string& recv_val) {
  const ConcMethodInfo* mi = lookupConcMethod(recv_ty.form, call.method);
  if (!mi)
    throw FarError("unknown concurrency method '" + call.method + "'");
  std::string tmp = ctx.fresh("cm");
  switch (mi->id) {
    case ConcMethodId::Send: {
      std::string v = ctx.emit_expr(*call.args[0]);
      ctx.out << "  %" << tmp << " = call i64 @far_channel_send(i64 " << recv_val << ", i64 " << v
              << ")\n";
      break;
    }
    case ConcMethodId::Recv:
      ctx.out << "  %" << tmp << " = call i64 @far_channel_recv(i64 " << recv_val << ")\n";
      break;
    case ConcMethodId::Close:
      ctx.out << "  call void @far_channel_close(i64 " << recv_val << ")\n";
      return "0";
    case ConcMethodId::Lock:
      ctx.out << "  call void @far_mutex_lock(i64 " << recv_val << ")\n";
      return "0";
    case ConcMethodId::Unlock:
      ctx.out << "  call void @far_mutex_unlock(i64 " << recv_val << ")\n";
      return "0";
    case ConcMethodId::Wait:
      ctx.out << "  call void @far_semaphore_wait(i64 " << recv_val << ")\n";
      return "0";
    case ConcMethodId::Signal:
      ctx.out << "  call void @far_semaphore_signal(i64 " << recv_val << ")\n";
      return "0";
    case ConcMethodId::Load:
      ctx.out << "  %" << tmp << " = call i64 @far_atomic_load(i64 " << recv_val << ")\n";
      break;
    case ConcMethodId::Store: {
      std::string v = ctx.emit_expr(*call.args[0]);
      ctx.out << "  call void @far_atomic_store(i64 " << recv_val << ", i64 " << v << ")\n";
      return "0";
    }
    case ConcMethodId::FetchAdd: {
      std::string v = ctx.emit_expr(*call.args[0]);
      ctx.out << "  %" << tmp << " = call i64 @far_atomic_fetch_add(i64 " << recv_val << ", i64 " << v
              << ")\n";
      break;
    }
    case ConcMethodId::CompareExchange: {
      std::string e = ctx.emit_expr(*call.args[0]);
      std::string d = ctx.emit_expr(*call.args[1]);
      ctx.out << "  %" << tmp << " = call i64 @far_atomic_compare_exchange(i64 " << recv_val
              << ", i64 " << e << ", i64 " << d << ")\n";
      break;
    }
    case ConcMethodId::Submit: {
      std::string fn = ctx.emit_expr(*call.args[0]);
      std::string arg = ctx.emit_expr(*call.args[1]);
      ctx.out << "  %" << tmp << " = call i64 @far_threadpool_submit(i64 " << recv_val << ", i8* "
              << fn << ", i64 " << arg << ")\n";
      break;
    }
    case ConcMethodId::Shutdown:
      ctx.out << "  call void @far_threadpool_shutdown(i64 " << recv_val << ")\n";
      return "0";
    case ConcMethodId::Push: {
      std::string v = ctx.emit_expr(*call.args[0]);
      ctx.out << "  %" << tmp << " = call i64 @far_lfqueue_push(i64 " << recv_val << ", i64 " << v
              << ")\n";
      break;
    }
    case ConcMethodId::Pop:
      ctx.out << "  %" << tmp << " = call i64 @far_lfqueue_pop(i64 " << recv_val << ")\n";
      break;
    case ConcMethodId::Await:
      ctx.out << "  %" << tmp << " = call i64 @far_await(i64 " << recv_val << ")\n";
      break;
    default:
      throw FarError("unsupported concurrency method");
  }
  return "%" + tmp;
}

void emitConcDrop(ConcCodegenCtx ctx, TypeForm form, const std::string& handle) {
  switch (form) {
    case TypeForm::Mutex:
      ctx.out << "  call void @far_mutex_drop(i64 " << handle << ")\n";
      break;
    case TypeForm::Semaphore:
      ctx.out << "  call void @far_semaphore_drop(i64 " << handle << ")\n";
      break;
    case TypeForm::Atomic:
      ctx.out << "  call void @far_atomic_drop(i64 " << handle << ")\n";
      break;
    case TypeForm::ThreadPool:
      ctx.out << "  call void @far_threadpool_drop(i64 " << handle << ")\n";
      break;
    case TypeForm::LockFreeQueue:
      ctx.out << "  call void @far_lfqueue_drop(i64 " << handle << ")\n";
      break;
    case TypeForm::Channel:
      ctx.out << "  call void @far_channel_drop(i64 " << handle << ")\n";
      break;
    default:
      break;
  }
}

std::string emitParallelFor(ConcCodegenCtx ctx, const std::string& fn_sym, const std::string& start,
                            const std::string& end) {
  std::string tmp = ctx.fresh("pfor");
  ctx.out << "  %" << tmp << " = call i64 @far_parallel_for(i8* bitcast (i64 (i64)* @" << fn_sym
          << " to i8*), i64 " << start << ", i64 " << end << ")\n";
  return "%" + tmp;
}

std::string emitActorMethod(ConcCodegenCtx ctx, const MethodCall& call, const std::string& recv_val) {
  const ConcMethodInfo* mi = lookupActorMethod(call.method);
  if (!mi)
    throw FarError("unknown actor method");
  if (mi->id == ConcMethodId::Stop) {
    ctx.out << "  call void @far_actor_stop(i64 " << recv_val << ")\n";
    return "0";
  }
  std::string tmp = ctx.fresh("act");
  std::string msg = ctx.emit_expr(*call.args[0]);
  if (mi->id == ConcMethodId::Tell) {
    ctx.out << "  call void @far_actor_tell(i64 " << recv_val << ", i64 " << msg << ")\n";
    return "0";
  }
  if (mi->id == ConcMethodId::Ask) {
    ctx.out << "  %" << tmp << " = call i64 @far_actor_ask(i64 " << recv_val << ", i64 " << msg
            << ")\n";
    return "%" + tmp;
  }
  throw FarError("unsupported actor method");
}

std::string emitActorConstruct(ConcCodegenCtx ctx, const std::string& handler_sym,
                               const std::string& init_val) {
  std::string tmp = ctx.fresh("actor");
  ctx.out << "  %" << tmp << " = call i64 @far_actor_spawn(i8* bitcast (i64 (i64, i64)* @"
          << handler_sym << " to i8*), i64 " << init_val << ")\n";
  return "%" + tmp;
}

}  // namespace far
