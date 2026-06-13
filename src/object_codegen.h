#pragma once

#include "ast.h"
#include "object_model.h"

#include <functional>
#include <ostream>
#include <string>
#include <vector>

namespace far {

struct ObjCodegenCtx {
  std::ostream& out;
  std::function<std::string(const std::string&)> fresh;
};

std::string userLlvmStructName(const std::string& type_name);
void declareUserTypes(std::ostream& out, const ObjectRegistry& reg);
const char* userFieldLlvmType(const TypeDesc& ty);
std::string emitUserAllocate(ObjCodegenCtx ctx, const UserTypeDef& td);
std::string emitUserConstruct(ObjCodegenCtx ctx, const UserTypeDef& td, const std::vector<std::string>& arg_vals);
std::string emitUserConstructCall(ObjCodegenCtx ctx, const UserTypeDef& td, const UserMethod& ctor,
                                  const std::vector<std::string>& arg_vals);
std::string emitUserMember(ObjCodegenCtx ctx, const ObjectRegistry& reg, const MemberAccess& mem,
                           const TypeDesc& obj_ty, const std::string& obj_val);
void emitUserMemberStore(ObjCodegenCtx ctx, const ObjectRegistry& reg, const MemberAccess& mem,
                         const TypeDesc& obj_ty, const std::string& obj_val, const std::string& value);
std::string emitUserMethodCall(ObjCodegenCtx ctx, const ObjectRegistry& reg, const MethodCall& call,
                               const TypeDesc& obj_ty, const std::string& obj_val,
                               const std::vector<std::string>& arg_vals);

}  // namespace far
