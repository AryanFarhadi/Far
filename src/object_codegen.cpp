#include "object_codegen.h"

#include "error.h"
#include "generics.h"
#include "type_desc.h"
#include "types.h"

namespace far {

std::string userLlvmStructName(const std::string& type_name) {
  return "%Far.User." + type_name;
}

static std::string userLlvmStructNameDesc(const TypeDesc& ty) {
  return "%Far.User." + userTypeKey(ty);
}

const char* userFieldLlvmType(const TypeDesc& ty) {
  if (isPrimitiveDesc(ty) && isFloatType(ty.primitive))
    return "double";
  return "i64";
}

void declareUserTypes(std::ostream& out, const ObjectRegistry& reg) {
  for (const UserTypeDef* td : reg.ordered) {
    if (td->kind == UserTypeKind::Enum || td->kind == UserTypeKind::Union ||
        td->kind == UserTypeKind::Interface || td->kind == UserTypeKind::Trait)
      continue;
    if (!td->type_params.empty())
      continue;
    if (td->fields.empty())
      continue;
    const std::string st_name = td->mangled_name.empty() ? td->name : td->mangled_name;
    out << userLlvmStructName(st_name) << " = type { ";
    for (size_t i = 0; i < td->fields.size(); ++i) {
      if (i)
        out << ", ";
      out << userFieldLlvmType(td->fields[i].type);
    }
    out << " }\n";
  }
}

std::string emitUserAllocate(ObjCodegenCtx ctx, const UserTypeDef& td) {
  std::string size = std::to_string(std::max<size_t>(td.fields.size(), 1) * 8);
  std::string raw = ctx.fresh("box");
  ctx.out << "  %" << raw << " = call i64 @far_box_alloc(i64 " << size << ")\n";
  return "%" + raw;
}

std::string emitUserConstruct(ObjCodegenCtx ctx, const UserTypeDef& td, const std::vector<std::string>& arg_vals) {
  const std::string st_name = td.mangled_name.empty() ? td.name : td.mangled_name;
  const std::string st = userLlvmStructName(st_name);
  std::string raw = emitUserAllocate(ctx, td);
  if (!td.fields.empty()) {
    std::string ptr = ctx.fresh("boxp");
    ctx.out << "  %" << ptr << " = inttoptr i64 " << raw << " to " << st << "*\n";
    for (size_t i = 0; i < td.fields.size() && i < arg_vals.size(); ++i) {
      const char* ft = userFieldLlvmType(td.fields[i].type);
      std::string fptr = ctx.fresh("bfp");
      ctx.out << "  %" << fptr << " = getelementptr inbounds " << st << ", " << st << "* %" << ptr
              << ", i32 0, i32 " << i << "\n";
      ctx.out << "  store " << ft << " " << arg_vals[i] << ", " << ft << "* %" << fptr << "\n";
    }
  }
  return raw;
}

std::string emitUserConstructCall(ObjCodegenCtx ctx, const UserTypeDef& td, const UserMethod& ctor,
                                  const std::vector<std::string>& arg_vals) {
  const std::string type_name = td.mangled_name.empty() ? td.name : td.mangled_name;
  std::string obj = emitUserAllocate(ctx, td);
  std::string sym = userMangleMethod(type_name, ctor.name);
  std::string tmp = ctx.fresh("ctor");
  ctx.out << "  %" << tmp << " = call i64 @" << sym << "(i64 " << obj;
  for (const auto& av : arg_vals)
    ctx.out << ", i64 " << av;
  ctx.out << ")\n";
  return obj;
}

std::string emitUserMember(ObjCodegenCtx ctx, const ObjectRegistry& reg, const MemberAccess& mem,
                           const TypeDesc& obj_ty, const std::string& obj_val) {
  const UserTypeDef* td =
      reg.program ? resolveUserType(obj_ty, const_cast<ObjectRegistry&>(reg), *reg.program) : nullptr;
  if (!td)
    throw FarError("unknown user type");
  int idx = reg.lookupFieldIndex(obj_ty, mem.member);
  if (idx < 0)
    throw FarError("unknown field '" + mem.member + "'");
  const char* ft = userFieldLlvmType(td->fields[static_cast<size_t>(idx)].type);
  const std::string st = userLlvmStructNameDesc(obj_ty);
  std::string ptr = ctx.fresh("bptr");
  ctx.out << "  %" << ptr << " = inttoptr i64 " << obj_val << " to " << st << "*\n";
  std::string fptr = ctx.fresh("mfp");
  ctx.out << "  %" << fptr << " = getelementptr inbounds " << st << ", " << st << "* %" << ptr
          << ", i32 0, i32 " << idx << "\n";
  std::string tmp = ctx.fresh("mfld");
  ctx.out << "  %" << tmp << " = load " << ft << ", " << ft << "* %" << fptr << "\n";
  if (ft == std::string("double")) {
    std::string ext = ctx.fresh("mfld");
    ctx.out << "  %" << ext << " = fptosi double %" << tmp << " to i64\n";
    return "%" + ext;
  }
  return "%" + tmp;
}

void emitUserMemberStore(ObjCodegenCtx ctx, const ObjectRegistry& reg, const MemberAccess& mem,
                         const TypeDesc& obj_ty, const std::string& obj_val, const std::string& value) {
  const UserTypeDef* td =
      reg.program ? resolveUserType(obj_ty, const_cast<ObjectRegistry&>(reg), *reg.program) : nullptr;
  if (!td)
    throw FarError("unknown user type");
  int idx = reg.lookupFieldIndex(obj_ty, mem.member);
  if (idx < 0)
    throw FarError("unknown field '" + mem.member + "'");
  const char* ft = userFieldLlvmType(td->fields[static_cast<size_t>(idx)].type);
  const std::string st = userLlvmStructNameDesc(obj_ty);
  std::string ptr = ctx.fresh("bptr");
  ctx.out << "  %" << ptr << " = inttoptr i64 " << obj_val << " to " << st << "*\n";
  std::string fptr = ctx.fresh("mfp");
  ctx.out << "  %" << fptr << " = getelementptr inbounds " << st << ", " << st << "* %" << ptr
          << ", i32 0, i32 " << idx << "\n";
  ctx.out << "  store " << ft << " " << value << ", " << ft << "* %" << fptr << "\n";
}

std::string emitUserMethodCall(ObjCodegenCtx ctx, const ObjectRegistry& reg, const MethodCall& call,
                               const TypeDesc& obj_ty, const std::string& obj_val,
                               const std::vector<std::string>& arg_vals) {
  std::string type_name = userTypeKey(obj_ty);
  const UserMethod* m = reg.lookupMethod(obj_ty, call.method);
  if (!m)
    m = reg.lookupExtension(type_name, call.method);
  if (!m)
    throw FarError("unknown method '" + call.method + "' on " + type_name);
  std::string sym = userMangleMethod(type_name, m->name);
  std::string tmp = ctx.fresh("umcall");
  ctx.out << "  %" << tmp << " = call i64 @" << sym << "(i64 " << obj_val;
  for (const auto& av : arg_vals)
    ctx.out << ", i64 " << av;
  ctx.out << ")\n";
  return "%" + tmp;
}

}  // namespace far
