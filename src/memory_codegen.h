#pragma once

#include "ast.h"
#include "memory.h"

#include <functional>
#include <ostream>
#include <string>

namespace far {

struct MemCodegenCtx {
  std::ostream& out;
  std::function<std::string(const std::string&)> fresh;
  std::function<std::string(const Expr&)> emit_expr;
  std::function<TypeDesc(const Expr&)> expr_type;
};

std::string emitMemConstruct(MemCodegenCtx ctx, TypeForm form, const TypeDesc& elem_ty,
                             const std::vector<std::string>& arg_vals);
std::string emitMemMethod(MemCodegenCtx ctx, const MethodCall& call, const TypeDesc& recv_ty,
                          const std::string& recv_val);
void emitMemDrop(MemCodegenCtx ctx, TypeForm form, const std::string& handle);
std::string emitStackAlloc(MemCodegenCtx ctx, const TypeDesc& elem_ty, const std::string& count_val);
std::string emitPtrDeref(MemCodegenCtx ctx, const TypeDesc& ptr_ty, const std::string& ptr_val);
void emitPtrStore(MemCodegenCtx ctx, const TypeDesc& ptr_ty, const std::string& ptr_val,
                  const std::string& value);
std::string emitAddressOf(MemCodegenCtx ctx, const std::string& slot_name, const char* llvm_slot_ty);

}  // namespace far
