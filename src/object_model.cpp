#include "object_model.h"

#include "error.h"
#include "generics.h"

#include <algorithm>
#include <unordered_set>
#include <utility>

namespace far {

bool isUserValueType(const UserTypeDef& td) {
  return td.kind == UserTypeKind::Struct || td.kind == UserTypeKind::Record;
}

bool isUserRefType(const UserTypeDef& td) { return td.kind == UserTypeKind::Class; }

std::string userMangleMethod(const std::string& type_name, const std::string& method) {
  return type_name + "$" + method;
}

Visibility defaultMemberVisibility(UserTypeKind kind) {
  return kind == UserTypeKind::Class ? Visibility::Private : Visibility::Public;
}

const UserField* lookupUserField(const UserTypeDef& td, const std::string& name) {
  for (const auto& f : td.fields) {
    if (f.name == name)
      return &f;
  }
  return nullptr;
}

std::string userTypeBaseName(const std::string& type_name) {
  const size_t dot = type_name.rfind('.');
  return dot == std::string::npos ? type_name : type_name.substr(dot + 1);
}

bool userTypeHasConstructor(const UserTypeDef& td) {
  for (const auto& m : td.methods) {
    if (m.is_constructor)
      return true;
  }
  return false;
}

const UserMethod* lookupUserConstructor(const UserTypeDef& td, size_t nargs) {
  for (const auto& m : td.methods) {
    if (!m.is_constructor)
      continue;
    if (userMethodCallArgCount(m) == nargs)
      return &m;
  }
  return nullptr;
}

size_t userMethodCallArgCount(const UserMethod& method) {
  if (method.params.empty())
    return 0;
  const std::string& first = method.params[0].name;
  if (first == "this" || first == "self")
    return method.params.size() - 1;
  return method.params.size();
}

namespace {

void ensureInstanceParam(const UserTypeDef& td, UserMethod& m) {
  if (m.is_static)
    return;
  TypeDesc ty = TypeDesc::user(td.name);
  if (!m.params.empty()) {
    const std::string& first = m.params[0].name;
    if (first == "self" || first == "this") {
      m.params[0].name = "this";
      m.params[0].type = ty;
      return;
    }
    if (first == "__state")
      return;
  }
  Param p;
  p.name = "this";
  p.type = ty;
  m.params.insert(m.params.begin(), std::move(p));
}

std::unique_ptr<Expr> makeThisMemberExpr(const UserTypeDef& td, const std::string& field, const TypeDesc& fty) {
  auto e = std::make_unique<Expr>();
  e->kind = Expr::MemberExpr;
  e->type = fty;
  e->member.object = Expr::makeVar("this", TypeDesc::user(td.name));
  e->member.member = field;
  return e;
}

void transformExpr(Expr& expr, const UserTypeDef& td, const std::unordered_set<std::string>& exclude);

void transformStmt(Stmt& stmt, const UserTypeDef& td, const std::unordered_set<std::string>& exclude) {
  switch (stmt.kind) {
    case Stmt::LetStmt: {
      const UserField* field = lookupUserField(td, stmt.let.name);
      if (field && !exclude.count(stmt.let.name)) {
        transformExpr(*stmt.let.value, td, exclude);
        auto assign = std::make_unique<Expr>();
        assign->kind = Expr::AssignExprK;
        assign->assign.op = "=";
        assign->assign.target = makeThisMemberExpr(td, field->name, field->type);
        assign->assign.value = std::move(stmt.let.value);
        stmt.kind = Stmt::ExprStmtK;
        stmt.expr_stmt.expr = std::move(assign);
        break;
      }
      transformExpr(*stmt.let.value, td, exclude);
      break;
    }
    case Stmt::ReturnStmt:
      if (stmt.ret.has_value)
        transformExpr(*stmt.ret.value, td, exclude);
      break;
    case Stmt::ExprStmtK:
      transformExpr(*stmt.expr_stmt.expr, td, exclude);
      break;
    case Stmt::PrintStmt:
      transformExpr(*stmt.print.value, td, exclude);
      break;
    case Stmt::IfStmt:
      for (const auto& c : stmt.if_stmt.clauses) {
        transformExpr(*c.condition, td, exclude);
        for (const auto& s : c.body)
          transformStmt(*s, td, exclude);
      }
      for (const auto& s : stmt.if_stmt.else_body)
        transformStmt(*s, td, exclude);
      break;
    case Stmt::WhileStmt:
      transformExpr(*stmt.while_stmt.condition, td, exclude);
      for (const auto& s : stmt.while_stmt.body)
        transformStmt(*s, td, exclude);
      break;
    case Stmt::ForStmt:
      if (stmt.for_stmt.init)
        transformStmt(*stmt.for_stmt.init, td, exclude);
      if (stmt.for_stmt.cond)
        transformExpr(*stmt.for_stmt.cond, td, exclude);
      if (stmt.for_stmt.step)
        transformStmt(*stmt.for_stmt.step, td, exclude);
      for (const auto& s : stmt.for_stmt.body)
        transformStmt(*s, td, exclude);
      break;
    case Stmt::YieldStmtK:
      if (stmt.yield.has_value)
        transformExpr(*stmt.yield.value, td, exclude);
      break;
    case Stmt::DeferStmtK:
      transformExpr(*stmt.defer.expr, td, exclude);
      break;
    case Stmt::UnsafeStmtK:
      for (const auto& s : stmt.unsafe.body)
        transformStmt(*s, td, exclude);
      break;
    case Stmt::TryStmtK:
      for (const auto& s : stmt.try_stmt.try_body)
        transformStmt(*s, td, exclude);
      for (const auto& s : stmt.try_stmt.catch_body)
        transformStmt(*s, td, exclude);
      for (const auto& s : stmt.try_stmt.finally_body)
        transformStmt(*s, td, exclude);
      break;
    case Stmt::ThrowStmtK:
      transformExpr(*stmt.throw_stmt.value, td, exclude);
      break;
    default:
      break;
  }
}

void transformExpr(Expr& expr, const UserTypeDef& td, const std::unordered_set<std::string>& exclude) {
  switch (expr.kind) {
    case Expr::Variable: {
      if (expr.var.name == "self") {
        expr.var.name = "this";
        break;
      }
      if (exclude.count(expr.var.name))
        break;
      if (const UserField* f = lookupUserField(td, expr.var.name)) {
        auto neu = makeThisMemberExpr(td, f->name, f->type);
        neu->line = expr.line;
        neu->col = expr.col;
        expr = std::move(*neu);
      }
      break;
    }
    case Expr::AssignExprK: {
      if (expr.assign.target->kind == Expr::Variable) {
        const std::string& name = expr.assign.target->var.name;
        if (name != "self" && name != "this" && !exclude.count(name)) {
          if (const UserField* f = lookupUserField(td, name)) {
            expr.assign.target = makeThisMemberExpr(td, f->name, f->type);
          } else if (name == "self") {
            expr.assign.target->var.name = "this";
          }
        }
      } else {
        transformExpr(*expr.assign.target, td, exclude);
      }
      transformExpr(*expr.assign.value, td, exclude);
      break;
    }
    case Expr::Binary:
      transformExpr(*expr.bin_op.left, td, exclude);
      transformExpr(*expr.bin_op.right, td, exclude);
      break;
    case Expr::FnCall:
      for (const auto& a : expr.call.args)
        transformExpr(*a.value, td, exclude);
      break;
    case Expr::CastExpr:
      transformExpr(*expr.cast.value, td, exclude);
      break;
    case Expr::IndexExpr:
      transformExpr(*expr.index.array, td, exclude);
      transformExpr(*expr.index.index, td, exclude);
      break;
    case Expr::SliceExpr:
      transformExpr(*expr.slice.array, td, exclude);
      if (expr.slice.start)
        transformExpr(*expr.slice.start, td, exclude);
      if (expr.slice.end)
        transformExpr(*expr.slice.end, td, exclude);
      break;
    case Expr::ArrayLitExpr:
      for (const auto& el : expr.array_lit.elements)
        transformExpr(*el, td, exclude);
      break;
    case Expr::DictLitExpr:
      for (const auto& entry : expr.dict_lit.entries) {
        transformExpr(*entry.key, td, exclude);
        transformExpr(*entry.value, td, exclude);
      }
      break;
    case Expr::TupleLitExpr:
      for (const auto& el : expr.tuple_lit.elements)
        transformExpr(*el, td, exclude);
      break;
    case Expr::MemberExpr:
      transformExpr(*expr.member.object, td, exclude);
      break;
    case Expr::MethodExpr:
      transformExpr(*expr.method_call.object, td, exclude);
      for (const auto& a : expr.method_call.args)
        transformExpr(*a, td, exclude);
      break;
    case Expr::TernaryExprK:
      transformExpr(*expr.ternary.cond, td, exclude);
      transformExpr(*expr.ternary.then_br, td, exclude);
      transformExpr(*expr.ternary.else_br, td, exclude);
      break;
    case Expr::PrefixExprK:
      transformExpr(*expr.prefix.operand, td, exclude);
      break;
    case Expr::PostfixExprK:
      transformExpr(*expr.postfix.operand, td, exclude);
      break;
    case Expr::AwaitExprK:
      transformExpr(*expr.await.value, td, exclude);
      break;
    default:
      break;
  }
}

void transformMethodBody(const UserTypeDef& td, std::vector<std::unique_ptr<Stmt>>& body,
                         const std::unordered_set<std::string>& exclude) {
  for (const auto& stmt : body)
    transformStmt(*stmt, td, exclude);
}

std::unordered_set<std::string> methodParamNames(const UserMethod& m) {
  std::unordered_set<std::string> names;
  for (const auto& p : m.params)
    names.insert(p.name);
  return names;
}

std::vector<Param> cloneParams(const std::vector<Param>& src) {
  std::vector<Param> out;
  out.reserve(src.size());
  for (const auto& p : src) {
    Param cp;
    cp.name = p.name;
    cp.type = p.type;
    cp.is_optional = p.is_optional;
    cp.is_variadic = p.is_variadic;
    cp.type_explicit = p.type_explicit;
    out.push_back(std::move(cp));
  }
  return out;
}

}  // namespace

Visibility defaultMethodVisibility(UserTypeKind kind) {
  (void)kind;
  return Visibility::Public;
}

Visibility defaultPropertyVisibility(UserTypeKind kind) {
  (void)kind;
  return Visibility::Public;
}

void prepareUserTypeMethods(UserTypeDef& td) {
  for (auto& f : td.fields) {
    if (f.visibility == Visibility::Internal)
      f.visibility = defaultMemberVisibility(td.kind);
  }
  for (auto& m : td.methods) {
    if (m.visibility == Visibility::Internal)
      m.visibility = defaultMethodVisibility(td.kind);
    const bool actor_handler = td.kind == UserTypeKind::Actor && m.name == "on_msg";
    if (!actor_handler)
      ensureInstanceParam(td, m);
    transformMethodBody(td, m.body, methodParamNames(m));
  }
  for (auto& p : td.properties) {
    if (p.visibility == Visibility::Internal)
      p.visibility = defaultPropertyVisibility(td.kind);
    std::unordered_set<std::string> getter_exclude = {"this", "self"};
    transformMethodBody(td, p.getter, getter_exclude);
    std::unordered_set<std::string> setter_exclude = {"this", "self", p.setter_param};
    transformMethodBody(td, p.setter, setter_exclude);
  }
  for (auto& idx : td.indexers) {
    std::unordered_set<std::string> ex = {"this", "self", idx.key_param, idx.value_param};
    transformMethodBody(td, idx.getter_body, ex);
    transformMethodBody(td, idx.setter_body, ex);
  }
  for (auto& op : td.operators) {
    std::unordered_set<std::string> ex;
    for (const auto& p : op.params)
      ex.insert(p.name);
    transformMethodBody(td, op.body, ex);
  }
}

std::string userOpMethodName(const std::string& op) {
  static const std::unordered_map<std::string, std::string> map = {
      {"+", "__op_add"},    {"-", "__op_sub"},    {"*", "__op_mul"},    {"/", "__op_div"},
      {"%", "__op_mod"},    {"**", "__op_pow"},   {"//", "__op_fdiv"},  {"==", "__op_eq"},
      {"!=", "__op_ne"},    {"<", "__op_lt"},    {">", "__op_gt"},     {"<=", "__op_le"},
      {">=", "__op_ge"},    {"&", "__op_band"},  {"|", "__op_bor"},    {"^", "__op_bxor"},
      {"<<", "__op_shl"},   {">>", "__op_shr"},  {"[]", "__op_index"},
  };
  auto it = map.find(op);
  return it != map.end() ? it->second : "__op_unknown";
}

const char* userKindName(UserTypeKind k) {
  switch (k) {
    case UserTypeKind::Struct:
      return "struct";
    case UserTypeKind::Class:
      return "class";
    case UserTypeKind::Record:
      return "record";
    case UserTypeKind::Interface:
      return "interface";
    case UserTypeKind::Enum:
      return "enum";
    case UserTypeKind::FlagsEnum:
      return "flags";
    case UserTypeKind::Trait:
      return "trait";
    case UserTypeKind::Actor:
      return "actor";
    case UserTypeKind::Exception:
      return "exception";
    case UserTypeKind::Union:
      return "union";
  }
  return "unknown";
}

static UserMethod makeSyntheticMethod(const std::string& type_name, const std::string& name,
                                      std::vector<Param> params, TypeDesc ret,
                                      std::vector<std::unique_ptr<Stmt>> body) {
  UserMethod m;
  m.name = name;
  m.params = std::move(params);
  m.return_type = std::move(ret);
  m.body = std::move(body);
  return m;
}

void ObjectRegistry::lowerSugar(UserTypeDef& td, Program& program) {
  (void)program;
  prepareUserTypeMethods(td);
  if (td.kind == UserTypeKind::Actor) {
    for (auto& m : td.methods) {
      if (m.name != "on_msg")
        continue;
      bool has_state = false;
      for (const auto& p : m.params) {
        if (p.name == "__state") {
          has_state = true;
          break;
        }
      }
      if (has_state)
        continue;
      std::vector<Param> params;
      Param state;
      state.name = "__state";
      state.type = TypeDesc::prim(FarTypeId::I64);
      params.push_back(std::move(state));
      for (auto& p : m.params)
        params.push_back(std::move(p));
      m.params = std::move(params);
    }
  }
  for (auto& prop : td.properties) {
    if (!prop.getter.empty()) {
      std::vector<Param> params;
      Param self;
      self.name = "this";
      self.type = TypeDesc::user(td.name);
      params.push_back(std::move(self));
      UserMethod getter = makeSyntheticMethod(td.name, "__prop_get_" + prop.name, std::move(params), prop.type,
                                              std::move(prop.getter));
      td.methods.push_back(std::move(getter));
    }
    if (!prop.setter.empty()) {
      std::vector<Param> params;
      Param self;
      self.name = "this";
      self.type = TypeDesc::user(td.name);
      params.push_back(std::move(self));
      Param val;
      val.name = prop.setter_param;
      val.type = prop.type;
      params.push_back(std::move(val));
      UserMethod setter =
          makeSyntheticMethod(td.name, "__prop_set_" + prop.name, std::move(params),
                              TypeDesc::prim(FarTypeId::I64), std::move(prop.setter));
      td.methods.push_back(std::move(setter));
    }
  }
  td.properties.clear();

  for (auto& idx : td.indexers) {
    if (!idx.getter_body.empty()) {
      std::vector<Param> params;
      Param self;
      self.name = "this";
      self.type = TypeDesc::user(td.name);
      params.push_back(std::move(self));
      Param key;
      key.name = idx.key_param;
      key.type = idx.key_type;
      params.push_back(std::move(key));
      UserMethod getter = makeSyntheticMethod(td.name, "__index_get", std::move(params), idx.value_type,
                                              std::move(idx.getter_body));
      td.methods.push_back(std::move(getter));
    }
    if (!idx.setter_body.empty()) {
      std::vector<Param> params;
      Param self;
      self.name = "this";
      self.type = TypeDesc::user(td.name);
      params.push_back(std::move(self));
      Param key;
      key.name = idx.key_param;
      key.type = idx.key_type;
      params.push_back(std::move(key));
      Param val;
      val.name = idx.value_param;
      val.type = idx.value_type;
      params.push_back(std::move(val));
      UserMethod setter =
          makeSyntheticMethod(td.name, "__index_set", std::move(params),
                              TypeDesc::prim(FarTypeId::I64), std::move(idx.setter_body));
      td.methods.push_back(std::move(setter));
    }
  }
  td.indexers.clear();

  for (auto& op : td.operators) {
    std::vector<Param> params;
    Param self;
    self.name = "this";
    self.type = TypeDesc::user(td.name);
    params.push_back(std::move(self));
    for (auto& p : op.params)
      params.push_back(std::move(p));
    UserMethod m = makeSyntheticMethod(td.name, userOpMethodName(op.op), std::move(params), op.return_type,
                                       std::move(op.body));
    td.methods.push_back(std::move(m));
  }
  td.operators.clear();
}

void ObjectRegistry::applyMixins(UserTypeDef& td, Program& program) {
  for (const auto& mixin_name : td.mixins) {
    UserTypeDef* mixin = nullptr;
    for (auto& ut : program.user_types) {
      if (ut.name == mixin_name) {
        mixin = &ut;
        break;
      }
    }
    if (!mixin)
      throw FarError("unknown mixin '" + mixin_name + "' for " + td.name);
    if (mixin->kind != UserTypeKind::Trait)
      throw FarError("mixin '" + mixin_name + "' must be a trait");
    (void)mixin;
  }
}

void ObjectRegistry::validateImplements(const UserTypeDef& td) const {
  for (const auto& iface_name : td.implements) {
    const UserTypeDef* iface = lookup(iface_name);
    if (!iface)
      throw FarError("unknown interface/trait '" + iface_name + "'");
    if (iface->kind != UserTypeKind::Interface && iface->kind != UserTypeKind::Trait)
      throw FarError("'" + iface_name + "' is not an interface or trait");
    for (const auto& req : iface->methods) {
      const UserMethod* impl = nullptr;
      for (const auto& m : td.methods) {
        if (m.name == req.name) {
          impl = &m;
          break;
        }
      }
      if (!impl)
        throw FarError(td.name + " missing required method '" + req.name + "' from " + iface_name);
    }
  }
}

bool ObjectRegistry::hasAttribute(const UserTypeDef& td, const std::string& attr) const {
  for (const auto& a : td.attrs) {
    if (a.name == attr)
      return true;
  }
  return false;
}

void ObjectRegistry::build(Program& program, bool materialize_methods) {
  this->program = &program;
  by_name.clear();
  ordered.clear();
  extensions.clear();
  for (auto& td : program.user_types) {
    if (td.kind == UserTypeKind::Enum || td.kind == UserTypeKind::FlagsEnum ||
        td.kind == UserTypeKind::Union) {
      int64_t next = 0;
      for (auto& v : td.variants) {
        if (v.value < 0)
          v.value = next++;
        else
          next = v.value + 1;
      }
    }
    applyMixins(td, program);
    lowerSugar(td, program);
    td.type_tag = 0x8000 + static_cast<int>(td.kind) * 256 +
                  static_cast<int>(std::max(td.fields.size(), td.variants.size()));
    by_name[td.name] = &td;
    ordered.push_back(&td);
    validateImplements(td);
    if (materialize_methods && td.type_params.empty()) {
      for (auto& m : td.methods) {
        Function fn;
        fn.name = userMangleMethod(td.name, m.name);
        bool exists = false;
        for (const auto& existing : program.synthetic_functions) {
          if (existing.name == fn.name) {
            exists = true;
            break;
          }
        }
        if (exists)
          continue;
        fn.llvm_name = fn.name;
        fn.params = cloneParams(m.params);
        fn.return_type = m.return_type;
        fn.body = std::move(m.body);
        fn.module_name = td.module_name;
        if (td.module_name.rfind("far.", 0) == 0)
          fn.allow_public_builtins = true;
        program.synthetic_functions.push_back(std::move(fn));
      }
    }
  }
  for (auto& ext : program.extensions) {
    for (auto& m : ext.methods) {
      m.is_extension = true;
      extensions[ext.target_type].push_back(&m);
      if (materialize_methods) {
        Function fn;
        fn.name = userMangleMethod(ext.target_type, m.name);
        fn.llvm_name = fn.name;
        fn.params = cloneParams(m.params);
        fn.return_type = m.return_type;
        fn.body = std::move(m.body);
        program.synthetic_functions.push_back(std::move(fn));
      }
    }
  }
  for (auto& td : program.synthetic_user_types) {
    by_name[td.mangled_name] = &td;
    ordered.push_back(&td);
  }
}

const UserTypeDef* ObjectRegistry::lookup(const std::string& name) const {
  auto it = by_name.find(name);
  return it != by_name.end() ? it->second : nullptr;
}

UserTypeDef* ObjectRegistry::lookupMut(const std::string& name) {
  auto it = by_name.find(name);
  return it != by_name.end() ? it->second : nullptr;
}

bool ObjectRegistry::isUserType(const std::string& name) const { return by_name.count(name) > 0; }

const UserMethod* ObjectRegistry::lookupMethod(const TypeDesc& ty, const std::string& name) const {
  if (!isUserDesc(ty))
    return nullptr;
  const UserTypeDef* td =
      program ? resolveUserType(ty, const_cast<ObjectRegistry&>(*this), *program) : lookup(ty.user_name);
  if (!td)
    return nullptr;
  for (const auto& m : td->methods) {
    if (m.is_constructor)
      continue;
    if (m.name == name)
      return &m;
  }
  for (const auto& mixin_name : td->mixins) {
    const UserTypeDef* mixin = lookup(mixin_name);
    if (!mixin)
      continue;
    for (const auto& m : mixin->methods) {
      if (m.name == name)
        return &m;
    }
  }
  for (const auto& iface_name : td->implements) {
    const UserTypeDef* iface = lookup(iface_name);
    if (!iface)
      continue;
    for (const auto& m : iface->methods) {
      if (m.name == name)
        return &m;
    }
  }
  return nullptr;
}

const UserMethod* ObjectRegistry::lookupExtension(const std::string& type_name,
                                                  const std::string& method) const {
  auto it = extensions.find(type_name);
  if (it == extensions.end())
    return nullptr;
  for (const UserMethod* m : it->second) {
    if (m->name == method)
      return m;
  }
  return nullptr;
}

const PropertyDef* ObjectRegistry::lookupProperty(const TypeDesc& ty, const std::string& name) const {
  if (!isUserDesc(ty))
    return nullptr;
  const UserTypeDef* td =
      program ? resolveUserType(ty, const_cast<ObjectRegistry&>(*this), *program) : nullptr;
  if (!td)
    return nullptr;
  for (const auto& p : td->properties) {
    if (p.name == name)
      return &p;
  }
  return nullptr;
}

const IndexerDef* ObjectRegistry::lookupIndexer(const TypeDesc& ty) const {
  if (!isUserDesc(ty))
    return nullptr;
  const UserTypeDef* td =
      program ? resolveUserType(ty, const_cast<ObjectRegistry&>(*this), *program) : nullptr;
  if (!td || td->indexers.empty())
    return nullptr;
  return &td->indexers[0];
}

const OperatorDef* ObjectRegistry::lookupOperator(const TypeDesc& ty, const std::string& op) const {
  if (!isUserDesc(ty))
    return nullptr;
  const UserTypeDef* td =
      program ? resolveUserType(ty, const_cast<ObjectRegistry&>(*this), *program) : nullptr;
  if (!td)
    return nullptr;
  for (const auto& o : td->operators) {
    if (o.op == op)
      return &o;
  }
  return nullptr;
}

int ObjectRegistry::lookupFieldIndex(const TypeDesc& ty, const std::string& field) const {
  if (!isUserDesc(ty))
    return -1;
  const UserTypeDef* td =
      program ? resolveUserType(ty, const_cast<ObjectRegistry&>(*this), *program) : nullptr;
  if (!td)
    return -1;
  for (size_t i = 0; i < td->fields.size(); ++i) {
    if (td->fields[i].name == field)
      return static_cast<int>(i);
  }
  return -1;
}

int ObjectRegistry::enumVariantValue(const std::string& type_name, const std::string& variant) const {
  const UserTypeDef* td = lookup(type_name);
  if (!td || (td->kind != UserTypeKind::Enum && td->kind != UserTypeKind::FlagsEnum &&
              td->kind != UserTypeKind::Union))
    return -1;
  for (const auto& v : td->variants) {
    if (v.name == variant)
      return static_cast<int>(v.value);
  }
  return -1;
}

}  // namespace far
