#include "generics.h"

#include "error.h"
#include "functions.h"

namespace far {

std::string userMangleTypeName(const std::string& base, const std::vector<TypeDesc>& type_args) {
  if (type_args.empty())
    return base;
  return specializeGenericName(base, type_args);
}

std::string userTypeKey(const TypeDesc& ty) {
  if (!isUserDesc(ty))
    return {};
  if (ty.args.empty())
    return ty.user_name;
  return userMangleTypeName(ty.user_name, ty.args);
}

static bool hasTraitBinding(const UserTypeDef& td, const std::string& trait_name) {
  for (const auto& m : td.mixins) {
    if (m == trait_name)
      return true;
  }
  for (const auto& i : td.implements) {
    if (i == trait_name)
      return true;
  }
  return false;
}

bool typeSatisfiesTrait(const TypeDesc& ty, const std::string& trait_name, const ObjectRegistry& reg) {
  if (!isUserDesc(ty))
    return false;
  const UserTypeDef* td = reg.lookup(ty.user_name);
  if (!td)
    return false;
  if (hasTraitBinding(*td, trait_name))
    return true;
  const UserTypeDef* trait = reg.lookup(trait_name);
  if (!trait || trait->kind != UserTypeKind::Trait)
    return false;
  for (const auto& req : trait->methods) {
    bool found = false;
    for (const auto& m : td->methods) {
      if (m.name == req.name) {
        found = true;
        break;
      }
    }
    if (!found)
      return false;
  }
  return true;
}

void validateTypeConstraints(const std::vector<TypeParam>& params, const std::vector<TypeDesc>& type_args,
                             const ObjectRegistry& reg) {
  if (params.size() != type_args.size())
    throw FarError("generic argument count mismatch");
  for (size_t i = 0; i < params.size(); ++i) {
    if (!params[i].constraint.empty() && !typeSatisfiesTrait(type_args[i], params[i].constraint, reg))
      throw FarError("type " + typeDescName(type_args[i]) + " does not satisfy trait " + params[i].constraint);
  }
}

bool inferUserTypeArgs(const UserTypeDef& tmpl, const Call& call,
                       const std::function<TypeDesc(Expr&)>& type_of_expr, std::vector<TypeDesc>& out) {
  if (tmpl.type_params.empty())
    return false;
  std::unordered_map<std::string, TypeDesc> sub;
  if (!call.type_args.empty()) {
    if (call.type_args.size() != tmpl.type_params.size())
      return false;
    for (size_t i = 0; i < tmpl.type_params.size(); ++i)
      sub[tmpl.type_params[i].name] = call.type_args[i];
  } else {
    if (call.args.size() != tmpl.fields.size())
      return false;
    for (size_t i = 0; i < tmpl.fields.size(); ++i) {
      TypeDesc arg_t = type_of_expr(*call.args[i].value);
      unifyTypes(tmpl.fields[i].type, arg_t, sub);
    }
    for (const auto& tp : tmpl.type_params) {
      if (!sub.count(tp.name))
        return false;
    }
  }
  out.clear();
  for (const auto& tp : tmpl.type_params)
    out.push_back(sub.at(tp.name));
  return true;
}

static UserField substituteField(const UserField& f, const std::unordered_map<std::string, TypeDesc>& sub) {
  UserField out;
  out.name = f.name;
  out.type = substituteTypeDesc(f.type, sub);
  return out;
}

static UserMethod substituteMethod(const UserMethod& m, const std::unordered_map<std::string, TypeDesc>& sub) {
  UserMethod out;
  out.name = m.name;
  out.return_type = substituteTypeDesc(m.return_type, sub);
  for (const auto& p : m.params) {
    Param np;
    np.name = p.name;
    np.type = substituteTypeDesc(p.type, sub);
    np.is_optional = p.is_optional;
    np.is_variadic = p.is_variadic;
    np.type_explicit = p.type_explicit;
    out.params.push_back(std::move(np));
  }
  return out;
}

UserTypeDef instantiateUserType(const UserTypeDef& tmpl, const std::vector<TypeDesc>& type_args) {
  if (tmpl.type_params.size() != type_args.size())
    throw FarError("generic type argument count mismatch");
  std::unordered_map<std::string, TypeDesc> sub;
  for (size_t i = 0; i < tmpl.type_params.size(); ++i)
    sub[tmpl.type_params[i].name] = type_args[i];
  UserTypeDef inst;
  inst.name = tmpl.name;
  inst.mono_type_args = type_args;
  inst.body_source = &tmpl;
  inst.kind = tmpl.kind;
  inst.mixins = tmpl.mixins;
  inst.implements = tmpl.implements;
  for (const auto& f : tmpl.fields)
    inst.fields.push_back(substituteField(f, sub));
  for (const auto& m : tmpl.methods)
    inst.methods.push_back(substituteMethod(m, sub));
  for (const auto& op : tmpl.operators) {
    OperatorDef no;
    no.op = op.op;
    no.return_type = substituteTypeDesc(op.return_type, sub);
    for (const auto& p : op.params) {
      Param np;
      np.name = p.name;
      np.type = substituteTypeDesc(p.type, sub);
      no.params.push_back(std::move(np));
    }
    inst.operators.push_back(std::move(no));
  }
  inst.mangled_name = userMangleTypeName(inst.name, type_args);
  return inst;
}

static bool monoArgsEqual(const std::vector<TypeDesc>& a, const std::vector<TypeDesc>& b) {
  if (a.size() != b.size())
    return false;
  for (size_t i = 0; i < a.size(); ++i)
    if (!typeDescEquals(a[i], b[i]))
      return false;
  return true;
}

static void materializeTypeMethods(Program& program, const UserTypeDef& td) {
  const std::string& key = td.mangled_name.empty() ? td.name : td.mangled_name;
  const UserTypeDef* src = td.body_source ? td.body_source : &td;
  for (const auto& m : td.methods) {
    const UserMethod* src_m = nullptr;
    for (const auto& sm : src->methods) {
      if (sm.name == m.name) {
        src_m = &sm;
        break;
      }
    }
    Function fn;
    fn.name = userMangleMethod(key, m.name);
    fn.llvm_name = fn.name;
    fn.params.clear();
    for (const auto& p : m.params) {
      Param np;
      np.name = p.name;
      np.type = p.type;
      np.is_optional = p.is_optional;
      np.is_variadic = p.is_variadic;
      np.type_explicit = p.type_explicit;
      fn.params.push_back(std::move(np));
    }
    fn.return_type = m.return_type;
    if (src_m)
      fn.shared_body = &src_m->body;
    program.synthetic_functions.push_back(std::move(fn));
  }
}

const UserTypeDef* findOrCreateUserMono(Program& program, ObjectRegistry& reg, const UserTypeDef& tmpl,
                                        const std::vector<TypeDesc>& type_args) {
  validateTypeConstraints(tmpl.type_params, type_args, reg);
  for (const auto& ut : program.synthetic_user_types) {
    if (ut.body_source == &tmpl && monoArgsEqual(ut.mono_type_args, type_args))
      return &ut;
  }
  UserTypeDef inst = instantiateUserType(tmpl, type_args);
  program.synthetic_user_types.push_back(std::move(inst));
  UserTypeDef* ptr = &program.synthetic_user_types.back();
  ptr->type_tag = reg.next_type_tag++;
  reg.by_name[ptr->mangled_name] = ptr;
  reg.ordered.push_back(ptr);
  reg.validateImplements(*ptr);
  materializeTypeMethods(program, *ptr);
  return ptr;
}

const UserTypeDef* resolveUserType(const TypeDesc& ty, ObjectRegistry& reg, Program& program) {
  if (!isUserDesc(ty))
    return nullptr;
  if (ty.args.empty())
    return reg.lookup(ty.user_name);
  const UserTypeDef* mono = reg.lookup(userTypeKey(ty));
  if (mono)
    return mono;
  const UserTypeDef* tmpl = reg.lookup(ty.user_name);
  if (!tmpl || tmpl->type_params.empty())
    return tmpl;
  return findOrCreateUserMono(program, reg, *tmpl, ty.args);
}

TypeDesc userTypeDesc(const std::string& base, const std::vector<TypeDesc>& type_args) {
  TypeDesc td = TypeDesc::user(base);
  td.args = type_args;
  return td;
}

}  // namespace far
