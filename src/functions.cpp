#include "functions.h"

#include "error.h"
#include "generics.h"
#include "type_desc.h"
#include "types.h"

#include <sstream>

namespace far {

std::string mangleType(const TypeDesc& td) {
  if (isPrimitiveDesc(td)) {
    switch (td.primitive) {
      case FarTypeId::I8: return "i8";
      case FarTypeId::I16: return "i16";
      case FarTypeId::I32: return "i32";
      case FarTypeId::I64: return "i64";
      case FarTypeId::U8: return "u8";
      case FarTypeId::U16: return "u16";
      case FarTypeId::U32: return "u32";
      case FarTypeId::U64: return "u64";
      case FarTypeId::F32: return "f32";
      case FarTypeId::F64: return "f64";
      case FarTypeId::Bool: return "bool";
      case FarTypeId::Char: return "char";
      case FarTypeId::String: return "str";
      default: return "p";
    }
  }
  switch (td.form) {
    case TypeForm::Array: return "A" + mangleType(td.args[0]);
    case TypeForm::List: return "L" + mangleType(td.args[0]);
    case TypeForm::Function: {
      std::ostringstream os;
      os << "F";
      for (size_t i = 0; i + 1 < td.args.size(); ++i)
        os << mangleType(td.args[i]);
      os << "R" << mangleType(td.args.back());
      return os.str();
    }
    case TypeForm::Optional: return "O" + mangleType(td.args[0]);
    case TypeForm::Result:
      return "R" + mangleType(td.args[0]) + mangleType(td.args[1]);
    case TypeForm::TypeVar: return "V" + td.type_var;
    case TypeForm::User:
      if (td.args.empty())
        return "U" + td.user_name;
      return userMangleTypeName(td.user_name, td.args);
    default: return "X";
  }
}

std::string mangleFunction(const Function& fn) {
  if (!fn.llvm_name.empty())
    return fn.llvm_name;
  std::ostringstream os;
  os << fn.name;
  for (const auto& t : fn.mono_type_args)
    os << "$" << mangleType(t);
  for (const auto& p : fn.params) {
    if (p.is_variadic)
      os << "$V";
    else
      os << "$" << mangleType(p.type);
  }
  if (fn.is_lambda)
    os << "$L" << fn.lambda_id;
  return os.str();
}

std::string specializeGenericName(const std::string& base, const std::vector<TypeDesc>& type_args) {
  std::ostringstream os;
  os << base;
  for (const auto& t : type_args)
    os << "$" << mangleType(t);
  return os.str();
}

static bool hasVariadic(const Function& fn) {
  return !fn.params.empty() && fn.params.back().is_variadic;
}

static size_t fixedParamCount(const Function& fn) {
  if (fn.params.empty())
    return 0;
  return hasVariadic(fn) ? fn.params.size() - 1 : fn.params.size();
}

static BindResult tryBind(const Function& fn, const Call& call,
                          const std::function<TypeDesc(Expr&)>& type_of_expr) {
  BindResult br;
  size_t nfixed = fixedParamCount(fn);
  bool variadic = hasVariadic(fn);
  std::vector<bool> used(fn.params.size(), false);
  br.args.resize(fn.params.size(), nullptr);
  size_t pos_idx = 0;

  for (const auto& ca : call.args) {
    if (!ca.name.empty()) {
      bool found = false;
      for (size_t i = 0; i < fn.params.size(); ++i) {
        if (fn.params[i].is_variadic)
          continue;
        if (fn.params[i].name == ca.name) {
          if (used[i]) {
            br.error = "duplicate argument '" + ca.name + "' in call to '" + fn.name + "'";
            return br;
          }
          type_of_expr(*ca.value);
          br.args[i] = ca.value.get();
          used[i] = true;
          found = true;
          break;
        }
      }
      if (!found) {
        br.error = "unknown argument '" + ca.name + "' in call to '" + fn.name + "'";
        return br;
      }
    } else {
      if (variadic && pos_idx >= nfixed) {
        type_of_expr(*ca.value);
        br.variadic_args.push_back(ca.value.get());
        continue;
      }
      if (pos_idx >= fn.params.size()) {
        br.error = "too many arguments in call to '" + fn.name + "'";
        return br;
      }
      if (fn.params[pos_idx].is_variadic)
        break;
      type_of_expr(*ca.value);
      br.args[pos_idx] = ca.value.get();
      used[pos_idx] = true;
      pos_idx++;
    }
  }

  for (size_t i = 0; i < fn.params.size(); ++i) {
    if (used[i] || fn.params[i].is_variadic)
      continue;
    if (fn.params[i].default_value) {
      type_of_expr(*fn.params[i].default_value);
      br.args[i] = fn.params[i].default_value.get();
      used[i] = true;
    } else if (fn.params[i].is_optional || fn.params[i].type.form == TypeForm::Optional) {
      auto z = Expr::makeInt(0);
      br.owned.push_back(std::move(z));
      br.args[i] = br.owned.back().get();
      used[i] = true;
    } else {
      br.error = "missing argument '" + fn.params[i].name + "' in call to '" + fn.name + "'";
      return br;
    }
  }

  for (size_t i = 0; i < fn.params.size(); ++i) {
    if (fn.params[i].is_variadic) {
      TypeDesc elem = fn.params[i].type.form == TypeForm::Array ? fn.params[i].type.args[0]
                                                                : TypeDesc::prim(FarTypeId::I64);
      for (Expr* v : br.variadic_args) {
        if (!canAssignTypes(v->type, elem)) {
          br.error = "argument type mismatch in call to '" + fn.name +
                     "': variadic parameter expected " + typeDescName(elem) + ", got " +
                     typeDescName(v->type);
          return br;
        }
        if (isPrimitiveDesc(elem) && isPrimitiveDesc(v->type) &&
            isNarrowingIntegerAssign(v->type.primitive, elem.primitive)) {
          br.error = "implicit narrowing from " + typeDescName(v->type) + " to " + typeDescName(elem) +
                     " in call to '" + fn.name + "'; use an explicit cast";
          return br;
        }
        if (isPrimitiveDesc(elem) && isIntegerType(elem.primitive) &&
            !intLiteralExprFitsType(*v, elem.primitive)) {
          br.error = "integer literal out of range for variadic parameter in call to '" + fn.name + "'";
          return br;
        }
      }
      continue;
    }
    if (!br.args[i])
      return br;
    TypeDesc got = br.args[i]->type;
    TypeDesc expect = fn.params[i].type;
    if (expect.form == TypeForm::Optional && !expect.args.empty())
      expect = expect.args[0];
    if (!canAssignTypes(got, expect)) {
      br.error = "argument type mismatch in call to '" + fn.name + "': parameter '" +
                 fn.params[i].name + "' expected " + typeDescName(expect) + ", got " +
                 typeDescName(got);
      return br;
    }
    if (isPrimitiveDesc(got) && isPrimitiveDesc(expect) &&
        isNarrowingIntegerAssign(got.primitive, expect.primitive)) {
      br.error = "implicit narrowing from " + typeDescName(got) + " to " + typeDescName(expect) +
                 " in call to '" + fn.name + "'; use an explicit cast";
      return br;
    }
    if (isPrimitiveDesc(expect) && isIntegerType(expect.primitive) &&
        !intLiteralExprFitsType(*br.args[i], expect.primitive)) {
      br.error = "integer literal out of range for parameter '" + fn.params[i].name + "' in call to '" +
                 fn.name + "'";
      return br;
    }
  }

  br.ok = true;
  return br;
}

bool inferGenericArgs(const Function& tmpl, const Call& call,
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
    size_t pos = 0;
    for (const auto& ca : call.args) {
      if (!ca.name.empty()) {
        bool found = false;
        for (const auto& p : tmpl.params) {
          if (p.name == ca.name && !p.is_variadic) {
            TypeDesc arg_t = type_of_expr(*ca.value);
            unifyTypes(p.type, arg_t, sub);
            found = true;
            break;
          }
        }
        if (!found)
          return false;
      } else {
        if (pos >= tmpl.params.size() || tmpl.params[pos].is_variadic)
          break;
        TypeDesc arg_t = type_of_expr(*ca.value);
        unifyTypes(tmpl.params[pos].type, arg_t, sub);
        pos++;
      }
    }
    for (const auto& tp : tmpl.type_params) {
      if (!sub.count(tp.name))
        return false;
    }
  }
  out.clear();
  for (const auto& tp : tmpl.type_params)
    out.push_back(resolveSubst(sub, sub.at(tp.name)));
  return true;
}

Function instantiateGeneric(const Function& tmpl, const std::vector<TypeDesc>& type_args) {
  if (tmpl.type_params.size() != type_args.size())
    throw FarError("generic argument count mismatch");
  std::unordered_map<std::string, TypeDesc> sub;
  for (size_t i = 0; i < tmpl.type_params.size(); ++i)
    sub[tmpl.type_params[i].name] = type_args[i];
  Function fn;
  fn.name = tmpl.name;
  fn.mono_type_args = type_args;
  fn.is_async = tmpl.is_async;
  fn.is_generator = tmpl.is_generator;
  fn.is_coroutine = tmpl.is_coroutine;
  fn.body_source = &tmpl;
  for (const auto& p : tmpl.params) {
    Param np;
    np.name = p.name;
    np.type = substituteTypeDesc(p.type, sub);
    np.is_optional = p.is_optional;
    np.is_variadic = p.is_variadic;
    np.type_explicit = p.type_explicit;
    fn.params.push_back(std::move(np));
  }
  fn.return_type = substituteTypeDesc(tmpl.return_type, sub);
  fn.llvm_name = mangleFunction(fn);
  return fn;
}

std::string genericInstanceKey(const std::string& name, const std::vector<TypeDesc>& type_args) {
  return specializeGenericName(name, type_args);
}

static bool monoArgsEqual(const std::vector<TypeDesc>& a, const std::vector<TypeDesc>& b) {
  if (a.size() != b.size())
    return false;
  for (size_t i = 0; i < a.size(); ++i)
    if (!typeDescEquals(a[i], b[i]))
      return false;
  return true;
}

static const Function* findOrCreateMono(Program& program, const Function* tmpl,
                                        const std::vector<TypeDesc>& type_args,
                                        std::unordered_map<std::string, std::vector<const Function*>>& overloads) {
  for (const auto& f : program.synthetic_functions) {
    if (f.body_source == tmpl && monoArgsEqual(f.mono_type_args, type_args))
      return &f;
  }
  program.synthetic_functions.push_back(instantiateGeneric(*tmpl, type_args));
  const Function* inst = &program.synthetic_functions.back();
  overloads[tmpl->name].push_back(inst);
  return inst;
}

BoundCall resolveCall(const std::string& name, Call& call,
                      std::unordered_map<std::string, std::vector<const Function*>>& overloads,
                      const std::function<TypeDesc(Expr&)>& type_of_expr, Program& program,
                      ObjectRegistry* reg) {
  auto it = overloads.find(name);
  if (it == overloads.end())
    throw FarError("undefined function '" + name + "'");

  const Function* best = nullptr;
  BindResult best_bind;
  std::string best_error;
  for (const Function* cand : it->second) {
    const Function* fn = cand;
    std::vector<TypeDesc> gargs;
    if (!cand->type_params.empty()) {
      if (!inferGenericArgs(*cand, call, type_of_expr, gargs))
        continue;
      if (reg)
        validateTypeConstraints(cand->type_params, gargs, *reg);
      fn = findOrCreateMono(program, cand, gargs, overloads);
    }
    BindResult br = tryBind(*fn, call, type_of_expr);
    if (br.ok) {
      best = fn;
      best_bind = std::move(br);
      break;
    }
    if (!br.error.empty() && (best_error.empty() || br.error.find("type mismatch") != std::string::npos))
      best_error = br.error;
  }
  if (!best) {
    if (it->second.size() == 1 && !best_error.empty())
      throw FarError(best_error);
    if (!best_error.empty())
      throw FarError("no matching overload for '" + name + "': " + best_error);
    throw FarError("no matching overload for '" + name + "'");
  }

  BoundCall result;
  result.fn = best;
  result.llvm_name = mangleFunction(*best);
  call.resolved = best;
  call.resolved_llvm_name = result.llvm_name;
  call.bound_exprs = best_bind.args;
  call.variadic_exprs = best_bind.variadic_args;
  result.bound = std::move(best_bind);
  return result;
}

void registerFunctions(const Program& program,
                       std::unordered_map<std::string, std::vector<const Function*>>& out) {
  out.clear();
  for (const auto& fn : program.functions)
    out[fn.name].push_back(&fn);
  for (const auto& fn : program.synthetic_functions)
    out[fn.name].push_back(&fn);
}

}  // namespace far
