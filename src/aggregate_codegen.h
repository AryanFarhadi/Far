#pragma once

#include "aggregate.h"
#include "ast.h"

#include <functional>
#include <sstream>
#include <string>

namespace far {

struct AggCodegenCtx {
  std::ostringstream& out;
  std::function<std::string(const std::string&)> fresh;
  std::function<std::string(const Expr&)> emit_expr;
  std::function<std::string(const Expr&)> emit_as_double;
  std::function<std::string(const Expr&)> emit_as_i64;
  std::function<FarTypeId(const Expr&)> expr_type;
};

const char* aggLlvmType(FarTypeId id);
const char* aggScalarLlvm(FarTypeId scalar);

std::string emitAggregateConstruct(AggCodegenCtx ctx, FarTypeId type, const std::vector<Expr*>& args);
std::string emitAggregateMember(AggCodegenCtx ctx, const MemberAccess& mem, FarTypeId obj_ty);
std::string emitAggregateMethod(AggCodegenCtx ctx, const MethodCall& call, FarTypeId obj_ty,
                                FarTypeId ret_ty);
std::string emitAggregateStaticCall(AggCodegenCtx ctx, const MethodCall& call, FarTypeId obj_ty,
                                    FarTypeId ret_ty);
std::string emitAggregateBinOp(AggCodegenCtx ctx, const std::string& op, FarTypeId lt, FarTypeId rt,
                               const std::string& left, const std::string& right);
std::string emitUnaryAggregate(AggCodegenCtx ctx, const std::string& op, FarTypeId ty,
                               const std::string& val);
std::string emitAggregateCompare(AggCodegenCtx ctx, const std::string& op, FarTypeId ty,
                                 const std::string& left, const std::string& right);
void emitAggregatePrint(AggCodegenCtx ctx, FarTypeId ty, const std::string& val);

std::string aggValueToPtr(AggCodegenCtx ctx, const std::string& val, FarTypeId type);
std::string loadAggValue(AggCodegenCtx ctx, const std::string& ptr, FarTypeId type);

}  // namespace far
