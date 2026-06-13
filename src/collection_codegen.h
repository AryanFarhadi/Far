#pragma once

#include "ast.h"
#include "type_desc.h"

#include <functional>
#include <sstream>
#include <string>

namespace far {

struct CollCodegenCtx {
  std::ostringstream& out;
  std::function<std::string(const std::string&)> fresh;
  std::function<std::string(const Expr&)> emit_expr;
  std::function<TypeDesc(const Expr&)> expr_type;
};

std::string emitTypedArrayLit(CollCodegenCtx ctx, const ArrayLit& lit, TypeDesc elem);
std::string emitDictLit(CollCodegenCtx ctx, const DictLit& lit, TypeDesc key, TypeDesc val);
std::string emitCollectionIndex(CollCodegenCtx ctx, const TypeDesc& arr_ty, const std::string& arr,
                                const std::string& idx);
void emitCollectionStore(CollCodegenCtx ctx, const TypeDesc& arr_ty, const std::string& arr,
                         const std::string& idx, const std::string& value);
std::string emitCollectionLen(CollCodegenCtx ctx, const TypeDesc& arr_ty, const std::string& arr);
std::string emitCollectionSlice(CollCodegenCtx ctx, const TypeDesc& arr_ty, const std::string& arr,
                                const std::string& start, const std::string& end);
std::string emitCollectionMethod(CollCodegenCtx ctx, const MethodCall& call, const TypeDesc& obj_ty,
                                 const TypeDesc& ret_ty);
void emitCollectionPrint(CollCodegenCtx ctx, const TypeDesc& ty, const std::string& val);
std::string emitRangeNew(CollCodegenCtx ctx, int64_t start, int64_t end, int64_t step);
std::string emitCollConstructor(CollCodegenCtx ctx, const std::string& name, const TypeDesc& ty);

}  // namespace far
