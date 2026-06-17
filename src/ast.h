#pragma once

#include "type_desc.h"
#include "types.h"

#include <cstdint>
#include <deque>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace far {

struct Expr;
struct Stmt;
struct Function;
struct UserMethod;

enum class Visibility {
  Public,
  Private,
  Protected,
  Internal,
};

enum class ImportKind {
  Normal,
  Internal,
  Protected,
};

struct ImportSymbol {
  std::string name;
  std::string alias;  // empty => bind as name
};

struct ImportDecl {
  std::string path;
  std::string alias;
  std::vector<ImportSymbol> symbols;
  ImportKind kind = ImportKind::Normal;
  bool from_import = false;
};

struct ModuleAlias {
  std::string module_name;
  std::unordered_map<std::string, std::string> symbols;
  /** Flattened static methods for function stdlib modules (alias.method -> facade class name). */
  std::unordered_map<std::string, std::string> flat_methods;
};

struct IntLit { int64_t value; bool unsigned_decimal = false; };
struct FloatLit { double value; bool is_float; };
struct StringLit { std::string value; };
struct CharLit { uint16_t value; };
struct Var { std::string name; };
struct BinOp { std::string op; std::unique_ptr<Expr> left; std::unique_ptr<Expr> right; };

struct CallArg {
  std::string name;  // empty = positional
  std::unique_ptr<Expr> value;
};

struct Call {
  std::string name;
  std::vector<CallArg> args;
  std::vector<TypeDesc> type_args;
  const Function* resolved = nullptr;
  const UserMethod* resolved_ctor = nullptr;
  std::string resolved_llvm_name;
  std::vector<Expr*> bound_exprs;
  std::vector<Expr*> variadic_exprs;
  bool is_hof_call = false;
};

struct Cast { TypeDesc target; std::unique_ptr<Expr> value; };
struct TypeConst { FarTypeId type; bool is_max; };
struct Spawn { std::unique_ptr<Expr> call; bool as_task = false; };
struct Parallel { std::string fn_name; std::unique_ptr<Expr> count; };
struct Index { std::unique_ptr<Expr> array; std::unique_ptr<Expr> index; };
struct Slice {
  std::unique_ptr<Expr> array;
  std::unique_ptr<Expr> start;
  std::unique_ptr<Expr> end;
};
struct ArrayLit { std::vector<std::unique_ptr<Expr>> elements; };
struct DictEntry {
  std::unique_ptr<Expr> key;
  std::unique_ptr<Expr> value;
};
struct DictLit { std::vector<DictEntry> entries; };
struct TupleLit { std::vector<std::unique_ptr<Expr>> elements; };
struct MemberAccess { std::unique_ptr<Expr> object; std::string member; };
struct MethodCall {
  std::unique_ptr<Expr> object;
  std::string method;
  std::vector<std::unique_ptr<Expr>> args;
  bool is_module_call = false;
  bool is_geom_call = false;
  bool is_type_construct = false;
  FarTypeId geom_agg_type = FarTypeId::Void;
  std::string resolved_fn;
  const Function* resolved = nullptr;
  const UserMethod* resolved_ctor = nullptr;
  std::string resolved_llvm_name;
};

struct Param {
  std::string name;
  TypeDesc type = TypeDesc::prim(FarTypeId::I64);
  std::unique_ptr<Expr> default_value;
  bool is_optional = false;
  bool is_variadic = false;
  bool type_explicit = false;
};

struct FnLit {
  std::vector<Param> params;
  TypeDesc return_type = TypeDesc::prim(FarTypeId::I64);
  std::vector<std::unique_ptr<Stmt>> body;
  std::unique_ptr<Expr> expr_body;  // single-expression lambda
  std::vector<std::string> captures;
  int id = -1;
};

struct AwaitExpr { std::unique_ptr<Expr> value; };

struct AssignExpr {
  std::string op;
  std::unique_ptr<Expr> target;
  std::unique_ptr<Expr> value;
};

struct TernaryExpr {
  std::unique_ptr<Expr> cond;
  std::unique_ptr<Expr> then_br;
  std::unique_ptr<Expr> else_br;
};

struct PrefixExpr {
  std::string op;
  std::unique_ptr<Expr> operand;
};

struct PostfixExpr {
  std::string op;
  std::unique_ptr<Expr> operand;
};

struct TypeUnaryExpr {
  std::string op;
  std::unique_ptr<Expr> value;
  TypeDesc type_arg;
  bool has_type = false;
};

struct IsExpr {
  std::unique_ptr<Expr> value;
  TypeDesc type;
};

struct AsExpr {
  std::unique_ptr<Expr> value;
  TypeDesc type;
};

struct EnumVariantExpr {
  std::string type_name;
  std::string variant;
  int64_t value = 0;
};

struct UnionVariantExpr {
  std::string type_name;
  std::string variant;
  int64_t value = 0;
  std::vector<std::unique_ptr<Expr>> args;
};

struct MacroSubstExpr {
  std::string param;
};

struct MacroInvokeExpr {
  std::string name;
  std::vector<std::unique_ptr<Expr>> args;
};

struct ComptimeExpr {
  std::unique_ptr<Expr> value;
  std::vector<std::unique_ptr<Stmt>> block;
  bool is_block = false;
};

struct Expr {
  enum Kind {
    Int,
    Float,
    String,
    Char,
    Variable,
    Binary,
    FnCall,
    CastExpr,
    TypeConstExpr,
    SpawnExpr,
    ParallelExpr,
    IndexExpr,
    SliceExpr,
    ArrayLitExpr,
    DictLitExpr,
    TupleLitExpr,
    MemberExpr,
    MethodExpr,
    FnLitExpr,
    AwaitExprK,
    AssignExprK,
    TernaryExprK,
    PrefixExprK,
    PostfixExprK,
    TypeUnaryExprK,
    IsExprK,
    AsExprK,
    EnumVariantExprK,
    UnionVariantExprK,
    MacroSubstExprK,
    MacroInvokeExprK,
    ComptimeExprK
  } kind;

  int line = 0;
  int col = 0;

  TypeDesc type = TypeDesc::prim(FarTypeId::I64);

  IntLit int_lit{};
  FloatLit float_lit{};
  StringLit string_lit{};
  CharLit char_lit{};
  Var var{};
  BinOp bin_op{};
  Call call{};
  Cast cast{};
  TypeConst type_const{};
  Spawn spawn{};
  Parallel parallel{};
  Index index{};
  Slice slice{};
  ArrayLit array_lit{};
  DictLit dict_lit{};
  TupleLit tuple_lit{};
  MemberAccess member{};
  MethodCall method_call{};
  FnLit fn_lit{};
  AwaitExpr await{};
  AssignExpr assign{};
  TernaryExpr ternary{};
  PrefixExpr prefix{};
  PostfixExpr postfix{};
  TypeUnaryExpr type_unary{};
  IsExpr is_expr{};
  AsExpr as_expr{};
  EnumVariantExpr enum_variant{};
  UnionVariantExpr union_variant{};
  MacroSubstExpr macro_subst{};
  MacroInvokeExpr macro_invoke{};
  ComptimeExpr comptime_expr{};

  static std::unique_ptr<Expr> makeInt(int64_t v) {
    auto e = std::make_unique<Expr>();
    e->kind = Int;
    e->type = defaultIntType();
    e->int_lit.value = v;
    return e;
  }

  static std::unique_ptr<Expr> makeFloat(double v, bool is_float = false) {
    auto e = std::make_unique<Expr>();
    e->kind = Float;
    e->type = TypeDesc::prim(is_float ? FarTypeId::F32 : FarTypeId::F64);
    e->float_lit.value = v;
    e->float_lit.is_float = is_float;
    return e;
  }

  static std::unique_ptr<Expr> makeString(std::string s) {
    auto e = std::make_unique<Expr>();
    e->kind = String;
    e->type = TypeDesc::prim(FarTypeId::String);
    e->string_lit.value = std::move(s);
    return e;
  }

  static std::unique_ptr<Expr> makeChar(uint16_t v) {
    auto e = std::make_unique<Expr>();
    e->kind = Char;
    e->type = TypeDesc::prim(FarTypeId::Char);
    e->char_lit.value = v;
    return e;
  }

  static std::unique_ptr<Expr> makeVar(std::string name, TypeDesc ty = TypeDesc::prim(FarTypeId::I64)) {
    auto e = std::make_unique<Expr>();
    e->kind = Variable;
    e->type = std::move(ty);
    e->var.name = std::move(name);
    return e;
  }

  static std::unique_ptr<Expr> makeBinOp(std::string op, std::unique_ptr<Expr> l, std::unique_ptr<Expr> r) {
    auto e = std::make_unique<Expr>();
    e->kind = Binary;
    e->type = TypeDesc::prim(FarTypeId::I64);
    e->bin_op.op = std::move(op);
    e->bin_op.left = std::move(l);
    e->bin_op.right = std::move(r);
    return e;
  }

  static std::unique_ptr<Expr> makeCall(std::string name, std::vector<std::unique_ptr<Expr>> args,
                                        TypeDesc ty = TypeDesc::prim(FarTypeId::I64)) {
    auto e = std::make_unique<Expr>();
    e->kind = FnCall;
    e->type = std::move(ty);
    e->call.name = std::move(name);
    for (auto& a : args) {
      CallArg ca;
      ca.value = std::move(a);
      e->call.args.push_back(std::move(ca));
    }
    return e;
  }

  static std::unique_ptr<Expr> makeCallArgs(std::string name, std::vector<CallArg> args,
                                            TypeDesc ty = TypeDesc::prim(FarTypeId::I64),
                                            std::vector<TypeDesc> type_args = {}) {
    auto e = std::make_unique<Expr>();
    e->kind = FnCall;
    e->type = std::move(ty);
    e->call.name = std::move(name);
    e->call.args = std::move(args);
    e->call.type_args = std::move(type_args);
    return e;
  }

  static std::unique_ptr<Expr> makeCast(TypeDesc target, std::unique_ptr<Expr> value) {
    auto e = std::make_unique<Expr>();
    e->kind = CastExpr;
    e->type = target;
    e->cast.target = std::move(target);
    e->cast.value = std::move(value);
    return e;
  }

  static std::unique_ptr<Expr> makeTypeConst(FarTypeId type, bool is_max) {
    auto e = std::make_unique<Expr>();
    e->kind = TypeConstExpr;
    e->type = TypeDesc::prim(FarTypeId::I64);
    e->type_const.type = type;
    e->type_const.is_max = is_max;
    if (is_max && isFloatType(type))
      e->type = TypeDesc::prim(type);
    return e;
  }

  static std::unique_ptr<Expr> makeSpawn(std::unique_ptr<Expr> call, bool as_task = false) {
    auto e = std::make_unique<Expr>();
    e->kind = SpawnExpr;
    e->type = as_task ? TypeDesc::task() : TypeDesc::prim(FarTypeId::I64);
    e->spawn.call = std::move(call);
    e->spawn.as_task = as_task;
    return e;
  }

  static std::unique_ptr<Expr> makeParallel(std::string fn_name, std::unique_ptr<Expr> count = nullptr) {
    auto e = std::make_unique<Expr>();
    e->kind = ParallelExpr;
    e->type = TypeDesc::prim(FarTypeId::I64);
    e->parallel.fn_name = std::move(fn_name);
    e->parallel.count = std::move(count);
    return e;
  }

  static std::unique_ptr<Expr> makeIndex(std::unique_ptr<Expr> array, std::unique_ptr<Expr> index) {
    auto e = std::make_unique<Expr>();
    e->kind = IndexExpr;
    e->type = TypeDesc::prim(FarTypeId::I64);
    e->index.array = std::move(array);
    e->index.index = std::move(index);
    return e;
  }

  static std::unique_ptr<Expr> makeSlice(std::unique_ptr<Expr> array, std::unique_ptr<Expr> start,
                                         std::unique_ptr<Expr> end) {
    auto e = std::make_unique<Expr>();
    e->kind = SliceExpr;
    e->type = TypeDesc::slice(TypeDesc::prim(FarTypeId::I64));
    e->slice.array = std::move(array);
    e->slice.start = std::move(start);
    e->slice.end = std::move(end);
    return e;
  }

  static std::unique_ptr<Expr> makeArrayLit(std::vector<std::unique_ptr<Expr>> elements) {
    auto e = std::make_unique<Expr>();
    e->kind = ArrayLitExpr;
    e->type = TypeDesc::array(defaultIntType());
    e->array_lit.elements = std::move(elements);
    return e;
  }

  static std::unique_ptr<Expr> makeDictLit(std::vector<DictEntry> entries) {
    auto e = std::make_unique<Expr>();
    e->kind = DictLitExpr;
    e->type = TypeDesc::dict(defaultIntType(), defaultIntType());
    e->dict_lit.entries = std::move(entries);
    return e;
  }

  static std::unique_ptr<Expr> makeTupleLit(std::vector<std::unique_ptr<Expr>> elements) {
    auto e = std::make_unique<Expr>();
    e->kind = TupleLitExpr;
    std::vector<TypeDesc> fields;
    for (const auto& el : elements)
      fields.push_back(el->type);
    e->type = TypeDesc::tuple(std::move(fields));
    e->tuple_lit.elements = std::move(elements);
    return e;
  }

  static std::unique_ptr<Expr> makeMember(std::unique_ptr<Expr> object, std::string member, TypeDesc ty) {
    auto e = std::make_unique<Expr>();
    e->kind = MemberExpr;
    e->type = std::move(ty);
    e->member.object = std::move(object);
    e->member.member = std::move(member);
    return e;
  }

  static std::unique_ptr<Expr> makeMethodCall(std::unique_ptr<Expr> object, std::string method,
                                              std::vector<std::unique_ptr<Expr>> args, TypeDesc ty) {
    auto e = std::make_unique<Expr>();
    e->kind = MethodExpr;
    e->type = std::move(ty);
    e->method_call.object = std::move(object);
    e->method_call.method = std::move(method);
    e->method_call.args = std::move(args);
    return e;
  }

  static std::unique_ptr<Expr> makeFnLit(FnLit lit, TypeDesc ty) {
    auto e = std::make_unique<Expr>();
    e->kind = FnLitExpr;
    e->type = std::move(ty);
    e->fn_lit = std::move(lit);
    return e;
  }

  static std::unique_ptr<Expr> makeAwait(std::unique_ptr<Expr> value, TypeDesc ty) {
    auto e = std::make_unique<Expr>();
    e->kind = AwaitExprK;
    e->type = std::move(ty);
    e->await.value = std::move(value);
    return e;
  }

  static std::unique_ptr<Expr> makeAssign(std::string op, std::unique_ptr<Expr> target,
                                          std::unique_ptr<Expr> value) {
    auto e = std::make_unique<Expr>();
    e->kind = AssignExprK;
    e->type = value ? value->type : TypeDesc::prim(FarTypeId::I64);
    e->assign.op = std::move(op);
    e->assign.target = std::move(target);
    e->assign.value = std::move(value);
    return e;
  }

  static std::unique_ptr<Expr> makeTernary(std::unique_ptr<Expr> cond, std::unique_ptr<Expr> then_br,
                                           std::unique_ptr<Expr> else_br, TypeDesc ty) {
    auto e = std::make_unique<Expr>();
    e->kind = TernaryExprK;
    e->type = std::move(ty);
    e->ternary.cond = std::move(cond);
    e->ternary.then_br = std::move(then_br);
    e->ternary.else_br = std::move(else_br);
    return e;
  }

  static std::unique_ptr<Expr> makePrefix(std::string op, std::unique_ptr<Expr> operand) {
    auto e = std::make_unique<Expr>();
    e->kind = PrefixExprK;
    e->type = operand ? operand->type : TypeDesc::prim(FarTypeId::I64);
    e->prefix.op = std::move(op);
    e->prefix.operand = std::move(operand);
    return e;
  }

  static std::unique_ptr<Expr> makePostfix(std::unique_ptr<Expr> operand, std::string op) {
    auto e = std::make_unique<Expr>();
    e->kind = PostfixExprK;
    e->type = operand ? operand->type : TypeDesc::prim(FarTypeId::I64);
    e->postfix.op = std::move(op);
    e->postfix.operand = std::move(operand);
    return e;
  }

  static std::unique_ptr<Expr> makeTypeUnary(std::string op, std::unique_ptr<Expr> value) {
    auto e = std::make_unique<Expr>();
    e->kind = TypeUnaryExprK;
    e->type = TypeDesc::prim(FarTypeId::I64);
    e->type_unary.op = std::move(op);
    e->type_unary.value = std::move(value);
    return e;
  }

  static std::unique_ptr<Expr> makeTypeUnaryType(std::string op, TypeDesc ty) {
    auto e = std::make_unique<Expr>();
    e->kind = TypeUnaryExprK;
    e->type = TypeDesc::prim(FarTypeId::I64);
    e->type_unary.op = std::move(op);
    e->type_unary.type_arg = std::move(ty);
    e->type_unary.has_type = true;
    return e;
  }

  static std::unique_ptr<Expr> makeIs(std::unique_ptr<Expr> value, TypeDesc ty) {
    auto e = std::make_unique<Expr>();
    e->kind = IsExprK;
    e->type = TypeDesc::prim(FarTypeId::Bool);
    e->is_expr.value = std::move(value);
    e->is_expr.type = std::move(ty);
    return e;
  }

  static std::unique_ptr<Expr> makeAs(std::unique_ptr<Expr> value, TypeDesc ty) {
    auto e = std::make_unique<Expr>();
    e->kind = AsExprK;
    e->type = std::move(ty);
    e->as_expr.value = std::move(value);
    e->as_expr.type = e->type;
    return e;
  }

  static std::unique_ptr<Expr> makeEnumVariant(std::string type_name, std::string variant, int64_t value) {
    auto e = std::make_unique<Expr>();
    e->kind = EnumVariantExprK;
    e->type = TypeDesc::prim(FarTypeId::I64);
    e->enum_variant.type_name = std::move(type_name);
    e->enum_variant.variant = std::move(variant);
    e->enum_variant.value = value;
    return e;
  }

  static std::unique_ptr<Expr> makeUnionVariant(std::string type_name, std::string variant, int64_t value,
                                                std::vector<std::unique_ptr<Expr>> args) {
    auto e = std::make_unique<Expr>();
    e->kind = UnionVariantExprK;
    e->type = TypeDesc::user(type_name);
    e->union_variant.type_name = std::move(type_name);
    e->union_variant.variant = std::move(variant);
    e->union_variant.value = value;
    e->union_variant.args = std::move(args);
    return e;
  }
};

struct Let {
  std::string name;
  std::unique_ptr<Expr> value;
  TypeDesc type = TypeDesc::prim(FarTypeId::I64);
  bool explicit_type = false;
  bool is_constexpr = false;
};
struct Return { std::unique_ptr<Expr> value; bool has_value; };
struct YieldStmt { std::unique_ptr<Expr> value; bool has_value; };
struct ExprStmt { std::unique_ptr<Expr> expr; };
struct Print { std::unique_ptr<Expr> value; };
struct IfClause { std::unique_ptr<Expr> condition; std::vector<std::unique_ptr<Stmt>> body; };
struct If { std::vector<IfClause> clauses; std::vector<std::unique_ptr<Stmt>> else_body; };
struct While { std::unique_ptr<Expr> condition; std::vector<std::unique_ptr<Stmt>> body; };
struct For {
  std::unique_ptr<Stmt> init;
  std::unique_ptr<Expr> cond;
  std::unique_ptr<Stmt> step;
  std::vector<std::unique_ptr<Stmt>> body;
  bool is_range = false;
  bool range_exclusive = false;
  std::string range_var;
  bool is_parallel = false;
  std::string parallel_var;
  std::unique_ptr<Expr> range_start;
  std::unique_ptr<Expr> range_end;
  std::string parallel_fn;
  std::vector<std::string> parallel_captures;
  bool is_foreach = false;
  std::string foreach_var;
  std::unique_ptr<Expr> foreach_iter;
};

struct DeferStmt {
  std::unique_ptr<Expr> expr;
};

struct UnsafeStmt {
  std::vector<std::unique_ptr<Stmt>> body;
};

struct TryStmt {
  std::vector<std::unique_ptr<Stmt>> try_body;
  bool has_catch = false;
  std::string catch_var;
  TypeDesc catch_type = TypeDesc::prim(FarTypeId::I64);
  bool catch_type_explicit = false;
  int64_t catch_type_tag = 0;
  std::vector<std::unique_ptr<Stmt>> catch_body;
  bool has_finally = false;
  std::vector<std::unique_ptr<Stmt>> finally_body;
};

struct ThrowStmt {
  std::unique_ptr<Expr> value;
};

enum class PatKind {
  Wildcard,
  Bind,
  Literal,
  EnumVariant,
  UnionVariant,
  TypeTest,
  StructDestructure,
  TupleDestructure,
};

struct Pattern {
  PatKind kind = PatKind::Wildcard;
  std::string bind_name;
  int64_t literal = 0;
  bool literal_is_float = false;
  double float_literal = 0.0;
  std::string type_name;
  std::string variant;
  int64_t variant_value = -1;
  TypeDesc type_test;
  std::vector<std::unique_ptr<Pattern>> fields;
  std::vector<std::string> field_names;
};

struct MatchArm {
  std::unique_ptr<Pattern> pat;
  std::vector<std::unique_ptr<Stmt>> body;
};

struct MatchStmt {
  std::unique_ptr<Expr> scrutinee;
  std::vector<MatchArm> arms;
  bool is_switch = false;
};

struct Stmt {
  enum Kind {
    LetStmt,
    ReturnStmt,
    YieldStmtK,
    ExprStmtK,
    PrintStmt,
    IfStmt,
    WhileStmt,
    ForStmt,
    BreakStmt,
    ContinueStmt,
    DeferStmtK,
    UnsafeStmtK,
    TryStmtK,
    ThrowStmtK,
    MatchStmtK,
    ComptimeBlockK,
    CodegenBlockK,
  } kind;
  Let let{};
  Return ret{};
  YieldStmt yield{};
  ExprStmt expr_stmt{};
  Print print{};
  If if_stmt{};
  While while_stmt{};
  For for_stmt{};
  DeferStmt defer{};
  UnsafeStmt unsafe{};
  TryStmt try_stmt{};
  ThrowStmt throw_stmt{};
  MatchStmt match_stmt{};
  std::vector<std::unique_ptr<Stmt>> comptime_block;
  std::vector<std::unique_ptr<Stmt>> codegen_block;
};

struct MacroDef {
  std::string name;
  std::vector<std::string> params;
  std::unique_ptr<Expr> body;
};

enum class UserTypeKind {
  Struct,
  Class,
  Record,
  Interface,
  Enum,
  FlagsEnum,
  Trait,
  Actor,
  Exception,
  Union,
};

struct AttrArg {
  std::string name;
  std::unique_ptr<Expr> value;
};

struct Attribute {
  std::string name;
  std::vector<AttrArg> args;
};

struct UserField {
  std::string name;
  TypeDesc type = TypeDesc::prim(FarTypeId::I64);
  std::unique_ptr<Expr> default_value;
  std::vector<Attribute> attrs;
  Visibility visibility = Visibility::Internal;
};

struct UserMethod {
  std::string name;
  std::vector<Param> params;
  TypeDesc return_type = TypeDesc::prim(FarTypeId::I64);
  std::vector<std::unique_ptr<Stmt>> body;
  bool is_static = false;
  bool is_extension = false;
  bool is_constructor = false;
  std::vector<Attribute> attrs;
  Visibility visibility = Visibility::Internal;
};

struct PropertyDef {
  std::string name;
  TypeDesc type = TypeDesc::prim(FarTypeId::I64);
  std::vector<std::unique_ptr<Stmt>> getter;
  std::vector<std::unique_ptr<Stmt>> setter;
  std::string setter_param = "v";
  std::vector<Attribute> attrs;
  Visibility visibility = Visibility::Internal;
};

struct IndexerDef {
  TypeDesc key_type = TypeDesc::prim(FarTypeId::I64);
  TypeDesc value_type = TypeDesc::prim(FarTypeId::I64);
  std::string key_param = "key";
  std::vector<std::unique_ptr<Stmt>> getter_body;
  std::vector<std::unique_ptr<Stmt>> setter_body;
  std::string value_param = "v";
};

struct OperatorDef {
  std::string op;
  std::vector<Param> params;
  TypeDesc return_type = TypeDesc::prim(FarTypeId::I64);
  std::vector<std::unique_ptr<Stmt>> body;
};

struct EnumVariant {
  std::string name;
  int64_t value = -1;
  std::vector<UserField> fields;
};

struct TypeParam {
  std::string name;
  std::string constraint;
};

struct UserTypeDef {
  std::string name;
  std::string mangled_name;
  std::string module_name;
  Visibility visibility = Visibility::Internal;
  UserTypeKind kind = UserTypeKind::Struct;
  std::vector<TypeParam> type_params;
  std::vector<TypeDesc> mono_type_args;
  const UserTypeDef* body_source = nullptr;
  std::vector<std::string> implements;
  std::vector<std::string> mixins;
  std::vector<UserField> fields;
  std::vector<UserMethod> methods;
  std::vector<PropertyDef> properties;
  std::vector<IndexerDef> indexers;
  std::vector<OperatorDef> operators;
  std::vector<EnumVariant> variants;
  std::vector<Attribute> attrs;
  int type_tag = 0;
};

struct ExtensionDef {
  std::string target_type;
  std::vector<UserMethod> methods;
  std::vector<Attribute> attrs;
};

struct Function {
  std::string name;
  std::string llvm_name;
  std::string module_name;
  Visibility visibility = Visibility::Internal;
  std::vector<TypeParam> type_params;
  std::vector<Param> params;
  TypeDesc return_type = TypeDesc::prim(FarTypeId::I64);
  bool return_type_explicit = false;
  std::vector<std::unique_ptr<Stmt>> body;
  std::vector<Attribute> attrs;
  bool is_async = false;
  bool is_generator = false;
  bool is_coroutine = false;
  bool is_lambda = false;
  int lambda_id = -1;
  std::vector<std::string> captures;
  std::vector<TypeDesc> mono_type_args;  // concrete types for generic instance
  const Function* body_source = nullptr;  // generic instance shares body AST
  const std::vector<std::unique_ptr<Stmt>>* shared_body = nullptr;
  bool link_public = true;  // false when imported only through a module alias
  bool allow_public_builtins = false;  // stdlib static method bodies may call legacy builtins
  bool is_constexpr = false;
  bool is_consteval = false;
  bool is_codegen = false;
  bool is_comptime_materialized = false;
};

struct Program {
  std::string package_name;
  std::string module_name;
  std::vector<std::string> exports;
  std::vector<ImportDecl> imports;
  std::unordered_map<std::string, ModuleAlias> module_aliases;
  std::vector<UserTypeDef> user_types;
  std::deque<UserTypeDef> synthetic_user_types;
  std::vector<ExtensionDef> extensions;
  std::vector<Function> functions;
  std::deque<Function> synthetic_functions;  // lambdas, monomorphized generics
  std::vector<MacroDef> macros;
  std::vector<std::unique_ptr<Stmt>> comptime_stmts;
  std::vector<std::unique_ptr<Stmt>> codegen_stmts;
};

}  // namespace far

#include "aggregate.h"

namespace far {

inline bool isAggregateDesc(const TypeDesc& td) {
  return isPrimitiveDesc(td) && isAggregateType(td.primitive);
}

inline FarTypeId aggregateDescId(const TypeDesc& td) { return primitiveOf(td); }

}  // namespace far
