#include "parser.h"

#include "aggregate.h"
#include "builtins.h"
#include "memory.h"
#include "concurrency.h"
#include "errors.h"
#include "collections.h"
#include "error.h"
#include "type_desc.h"
#include "types.h"

#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <unordered_map>
#include <unordered_set>



namespace far {

static bool isPrim(const TypeDesc& td, FarTypeId id) {
  return isPrimitiveDesc(td) && td.primitive == id;
}

static const char* tokenName(TokenKind k) {

  switch (k) {

    case TokenKind::Ident: return "IDENT";

    case TokenKind::Int: return "INT";

    case TokenKind::Float: return "FLOAT";

    case TokenKind::String: return "STRING";

    case TokenKind::Char: return "CHAR";

    case TokenKind::InterpString: return "INTERP_STRING";

    case TokenKind::TypeName: return "TYPENAME";

    case TokenKind::Def: return "FUN";

    case TokenKind::If: return "IF";

    case TokenKind::Else: return "ELSE";

    case TokenKind::While: return "WHILE";

    case TokenKind::For: return "FOR";

    case TokenKind::Return: return "RETURN";

    case TokenKind::Break: return "BREAK";

    case TokenKind::Continue: return "CONTINUE";

    case TokenKind::Import: return "IMPORT";

    case TokenKind::From: return "FROM";

    case TokenKind::Export: return "EXPORT";
    case TokenKind::Package: return "PACKAGE";
    case TokenKind::Module: return "MODULE";
    case TokenKind::Namespace: return "NAMESPACE";
    case TokenKind::Public: return "PUBLIC";

    case TokenKind::Static: return "STATIC";
    case TokenKind::Private: return "PRIVATE";
    case TokenKind::Protected: return "PROTECTED";
    case TokenKind::Internal: return "INTERNAL";
    case TokenKind::Constexpr: return "CONSTEXPR";
    case TokenKind::Consteval: return "CONSTEVAL";
    case TokenKind::Macro: return "MACRO";
    case TokenKind::Comptime: return "COMPTIME";
    case TokenKind::Codegen: return "CODEGEN";
    case TokenKind::Dollar: return "$";

    case TokenKind::Spawn: return "SPAWN";

    case TokenKind::Parallel: return "PARALLEL";

    case TokenKind::TaskKw: return "TASK";

    case TokenKind::Actor: return "ACTOR";

    case TokenKind::Try: return "TRY";
    case TokenKind::Catch: return "CATCH";
    case TokenKind::Finally: return "FINALLY";
    case TokenKind::Throw: return "THROW";
    case TokenKind::Panic: return "PANIC";
    case TokenKind::Assert: return "ASSERT";
    case TokenKind::Exception: return "EXCEPTION";
    case TokenKind::Match: return "MATCH";
    case TokenKind::Switch: return "SWITCH";
    case TokenKind::Case: return "CASE";
    case TokenKind::Default: return "DEFAULT";
    case TokenKind::Union: return "UNION";

    case TokenKind::True: return "TRUE";

    case TokenKind::False: return "FALSE";

    case TokenKind::LParen: return "LPAREN";

    case TokenKind::RParen: return "RPAREN";

    case TokenKind::LBrace: return "LBRACE";

    case TokenKind::RBrace: return "RBRACE";

    case TokenKind::LBracket: return "LBRACKET";

    case TokenKind::RBracket: return "RBRACKET";

    case TokenKind::Comma: return "COMMA";

    case TokenKind::Semi: return "SEMI";

    case TokenKind::Arrow: return "ARROW";

    case TokenKind::Plus: return "PLUS";

    case TokenKind::Minus: return "MINUS";

    case TokenKind::Star: return "STAR";

    case TokenKind::StarStar: return "STARSTAR";

    case TokenKind::Slash: return "SLASH";

    case TokenKind::FloorDiv: return "FLOORDIV";

    case TokenKind::Tilde: return "TILDE";

    case TokenKind::Percent: return "PERCENT";

    case TokenKind::Colon: return "COLON";

    case TokenKind::Eq: return "EQ";

    case TokenKind::EqEq: return "EQEQ";

    case TokenKind::Bang: return "BANG";

    case TokenKind::BangEq: return "BANGEQ";

    case TokenKind::Lt: return "LT";

    case TokenKind::Gt: return "GT";

    case TokenKind::Lte: return "LTE";

    case TokenKind::Gte: return "GTE";

    case TokenKind::And: return "AND";

    case TokenKind::Or: return "OR";

    case TokenKind::Dot: return "DOT";
    case TokenKind::DotDot: return "DOTDOT";
    case TokenKind::DotDotLt: return "DOTDOTLT";
    case TokenKind::Question: return "?";
    case TokenKind::QuestionQuestion: return "??";
    case TokenKind::QuestionDot: return "?.";
    case TokenKind::QuestionEq: return "\?\?=";
    case TokenKind::BangQuestion: return "!?";
    case TokenKind::Fn: return "FN";
    case TokenKind::Async: return "ASYNC";
    case TokenKind::Await: return "AWAIT";
    case TokenKind::Yield: return "YIELD";
    case TokenKind::Lambda: return "LAMBDA";
    case TokenKind::Coroutine: return "COROUTINE";
    case TokenKind::Is: return "IS";
    case TokenKind::As: return "AS";
    case TokenKind::In: return "IN";
    case TokenKind::Not: return "NOT";
    case TokenKind::Typeof: return "TYPEOF";
    case TokenKind::TypeTag: return "TYPE_TAG";
    case TokenKind::Sizeof: return "SIZEOF";
    case TokenKind::Alignof: return "ALIGNOF";
    case TokenKind::Reflect: return "REFLECT";
    case TokenKind::Struct: return "STRUCT";
    case TokenKind::Class: return "CLASS";
    case TokenKind::Record: return "RECORD";
    case TokenKind::Interface: return "INTERFACE";
    case TokenKind::Enum: return "ENUM";
    case TokenKind::Flags: return "FLAGS";
    case TokenKind::Trait: return "TRAIT";
    case TokenKind::Mixin: return "MIXIN";
    case TokenKind::Extension: return "EXTENSION";
    case TokenKind::Operator: return "OPERATOR";
    case TokenKind::Prop: return "PROP";
    case TokenKind::With: return "WITH";
    case TokenKind::Implements: return "IMPLEMENTS";
    case TokenKind::Get: return "GET";
    case TokenKind::Set: return "SET";
    case TokenKind::Self: return "SELF";
    case TokenKind::At: return "@";
    case TokenKind::Unsafe: return "UNSAFE";
    case TokenKind::Defer: return "DEFER";
    case TokenKind::Borrow: return "BORROW";
    case TokenKind::Move: return "MOVE";
    case TokenKind::Drop: return "DROP";
    case TokenKind::StackAlloc: return "STACKALLOC";
    case TokenKind::FatArrow: return "=>";
    case TokenKind::Amp: return "AMP";
    case TokenKind::Caret: return "CARET";
    case TokenKind::Pipe: return "PIPE";
    case TokenKind::AmpAmp: return "&&";
    case TokenKind::PipePipe: return "||";
    case TokenKind::PipeGt: return "|>";
    case TokenKind::PlusPlus: return "++";
    case TokenKind::MinusMinus: return "--";
    case TokenKind::LShift: return "<<";
    case TokenKind::RShift: return ">>";
    case TokenKind::EqEqEq: return "===";
    case TokenKind::BangEqEq: return "!==";
    case TokenKind::Ellipsis: return "...";
    case TokenKind::PlusEq: return "+=";
    case TokenKind::MinusEq: return "-=";
    case TokenKind::StarEq: return "*=";
    case TokenKind::SlashEq: return "/=";
    case TokenKind::PercentEq: return "%=";
    case TokenKind::StarStarEq: return "**=";
    case TokenKind::FloorDivEq: return "//=";
    case TokenKind::AmpEq: return "&=";
    case TokenKind::PipeEq: return "|=";
    case TokenKind::CaretEq: return "^=";
    case TokenKind::LShiftEq: return "<<=";
    case TokenKind::RShiftEq: return ">>=";

    case TokenKind::Eof: return "EOF";

  }

  return "?";

}



Parser::Parser(std::vector<Token> tokens) : tokens_(std::move(tokens)) {}



bool Parser::match(TokenKind k) {

  if (check(k)) {

    pos_++;

    return true;

  }

  return false;

}



bool Parser::matchAny(std::initializer_list<TokenKind> kinds) {

  for (TokenKind k : kinds) {

    if (match(k))

      return true;

  }

  return false;

}



const Token& Parser::expect(TokenKind k) {

  const Token& tok = current();

  if (tok.kind != k)

    throw FarError(std::string("expected ") + tokenName(k) + ", got " + tokenName(tok.kind), tok.line, tok.col);

  pos_++;

  return tok;

}



void Parser::consumeOptionalSemi() {

  match(TokenKind::Semi);

}



Visibility Parser::parseVisibility() {
  if (match(TokenKind::Public))
    return Visibility::Public;
  if (match(TokenKind::Private))
    return Visibility::Private;
  if (match(TokenKind::Protected))
    return Visibility::Protected;
  if (match(TokenKind::Internal))
    return Visibility::Internal;
  return Visibility::Internal;
}

std::string Parser::parseImportSegment() {
  if (match(TokenKind::Ident) || match(TokenKind::TypeName) || match(TokenKind::Internal) ||
      match(TokenKind::Protected) || match(TokenKind::Public) || match(TokenKind::Private) ||
      match(TokenKind::Module) || match(TokenKind::Package) || match(TokenKind::Export) ||
      match(TokenKind::Namespace) || match(TokenKind::Match) || match(TokenKind::Try) ||
      match(TokenKind::Typeof))
    return tokens_[pos_ - 1].value;
  const Token& tok = current();
  if (tok.kind == TokenKind::Fn)
    throw FarError("expected import path segment, got keyword 'fun'", tok.line, tok.col);
  std::string got = tokenName(tok.kind);
  if (tok.kind == TokenKind::Ident || tok.kind == TokenKind::TypeName)
    got = tok.value;
  throw FarError("expected import path segment, got " + got, tok.line, tok.col);
}

std::string Parser::parseQualifiedPath() {
  std::string path = parseImportSegment();
  while (match(TokenKind::Dot))
    path += "." + parseImportSegment();
  return path;
}

std::string Parser::parseQualifiedSymbol() {
  std::string sym = parseImportSegment();
  while (match(TokenKind::Dot))
    sym += "." + parseImportSegment();
  return sym;
}

std::string Parser::qualifyName(const std::string& name) {
  if (current_namespace_.empty())
    return name;
  return current_namespace_ + "." + name;
}

ImportDecl Parser::parseImportDecl() {
  ImportDecl imp;
  imp.path = parseImportSegment();
  while (match(TokenKind::Dot)) {
    std::string seg = parseImportSegment();
    if (seg == "internal") {
      imp.kind = ImportKind::Internal;
      break;
    }
    if (seg == "protected") {
      imp.kind = ImportKind::Protected;
      break;
    }
    imp.path += "." + seg;
  }
  if (match(TokenKind::As))
    imp.alias = expect(TokenKind::Ident).value;
  if (match(TokenKind::LBrace)) {
    if (!check(TokenKind::RBrace)) {
      do {
        ImportSymbol sym;
        sym.name = parseQualifiedSymbol();
        if (match(TokenKind::As))
          sym.alias = expect(TokenKind::Ident).value;
        imp.symbols.push_back(std::move(sym));
      } while (match(TokenKind::Comma));
    }
    expect(TokenKind::RBrace);
  } else if (imp.alias.empty() && imp.symbols.empty()) {
    imp.alias = defaultImportAlias(imp.path);
  }
  return imp;
}

std::string Parser::defaultImportAlias(const std::string& path) {
  auto pos = path.rfind('.');
  return pos == std::string::npos ? path : path.substr(pos + 1);
}

ImportDecl Parser::parseFromImportDecl() {
  ImportDecl imp;
  imp.from_import = true;
  imp.path = parseQualifiedPath();
  expect(TokenKind::Import);
  if (match(TokenKind::Star)) {
    return imp;
  }
  do {
    ImportSymbol sym;
    sym.name = parseQualifiedSymbol();
    if (match(TokenKind::As))
      sym.alias = expect(TokenKind::Ident).value;
    imp.symbols.push_back(std::move(sym));
  } while (match(TokenKind::Comma));
  return imp;
}

void Parser::parseExportList(std::vector<std::string>& out) {
  do {
    out.push_back(parseQualifiedSymbol());
  } while (match(TokenKind::Comma));
}

void Parser::parseNamespaceDecl(Program& program) {
  std::string ns = expect(TokenKind::Ident).value;
  expect(TokenKind::LBrace);
  std::string saved = current_namespace_;
  current_namespace_ = current_namespace_.empty() ? ns : current_namespace_ + "." + ns;
  while (!check(TokenKind::RBrace) && !check(TokenKind::Eof))
    parseTopLevelDecl(program);
  current_namespace_ = saved;
  expect(TokenKind::RBrace);
}

void Parser::parseTopLevelDecl(Program& program) {
  if (match(TokenKind::Export)) {
    parseExportList(program.exports);
    consumeOptionalSemi();
    return;
  }
  if (match(TokenKind::Namespace)) {
    parseNamespaceDecl(program);
    return;
  }
  Visibility vis = parseVisibility();
  auto attrs = parseAttributes();
  if (matchAny({TokenKind::Struct, TokenKind::Class, TokenKind::Record, TokenKind::Interface,
                TokenKind::Enum, TokenKind::Flags, TokenKind::Trait, TokenKind::Actor,
                TokenKind::Exception, TokenKind::Union})) {
    UserTypeKind kind = UserTypeKind::Struct;
    if (tokens_[pos_ - 1].kind == TokenKind::Class)
      kind = UserTypeKind::Class;
    else if (tokens_[pos_ - 1].kind == TokenKind::Record)
      kind = UserTypeKind::Record;
    else if (tokens_[pos_ - 1].kind == TokenKind::Interface)
      kind = UserTypeKind::Interface;
    else if (tokens_[pos_ - 1].kind == TokenKind::Enum)
      kind = UserTypeKind::Enum;
    else if (tokens_[pos_ - 1].kind == TokenKind::Flags)
      kind = UserTypeKind::FlagsEnum;
    else if (tokens_[pos_ - 1].kind == TokenKind::Trait)
      kind = UserTypeKind::Trait;
    else if (tokens_[pos_ - 1].kind == TokenKind::Actor)
      kind = UserTypeKind::Actor;
    else if (tokens_[pos_ - 1].kind == TokenKind::Exception)
      kind = UserTypeKind::Exception;
    else if (tokens_[pos_ - 1].kind == TokenKind::Union)
      kind = UserTypeKind::Union;
    auto td = parseUserTypeDecl(kind);
    td.visibility = vis;
    td.attrs = std::move(attrs);
    user_type_names_.insert(td.name);
    if (td.kind == UserTypeKind::Enum || td.kind == UserTypeKind::FlagsEnum)
      enum_type_names_.insert(td.name);
    if (td.kind == UserTypeKind::Union)
      union_type_names_.insert(td.name);
    program.user_types.push_back(std::move(td));
  } else if (match(TokenKind::Extension)) {
    auto ext = parseExtensionDecl();
    ext.attrs = std::move(attrs);
    program.extensions.push_back(std::move(ext));
  } else if (check(TokenKind::Def) || check(TokenKind::Async) || check(TokenKind::Coroutine) ||
             check(TokenKind::Constexpr) || check(TokenKind::Consteval) || check(TokenKind::Codegen)) {
    (void)attrs;
    Function fn = parseFunction();
    fn.visibility = vis;
    program.functions.push_back(std::move(fn));
  } else {
    throw FarError("expected top-level declaration", current().line, current().col);
  }
}

Program Parser::parse() {

  Program program;

  while (!check(TokenKind::Eof)) {
    if (match(TokenKind::From)) {
      program.imports.push_back(parseFromImportDecl());
      consumeOptionalSemi();
      continue;
    }
    if (match(TokenKind::Import)) {
      program.imports.push_back(parseImportDecl());
      consumeOptionalSemi();
      continue;
    }
    if (match(TokenKind::Package)) {
      program.package_name = parseQualifiedPath();
      consumeOptionalSemi();
      continue;
    }
    if (match(TokenKind::Module)) {
      if (match(TokenKind::Ident) || match(TokenKind::TypeName))
        program.module_name = tokens_[pos_ - 1].value;
      else
        throw FarError("expected module name", current().line, current().col);
      consumeOptionalSemi();
      continue;
    }
    if (match(TokenKind::Export)) {
      parseExportList(program.exports);
      consumeOptionalSemi();
      continue;
    }
    if (match(TokenKind::Namespace)) {
      parseNamespaceDecl(program);
      continue;
    }
    if (match(TokenKind::Macro)) {
      MacroDef m = parseMacroDecl();
      macro_names_.insert(m.name);
      program.macros.push_back(std::move(m));
      continue;
    }
    if (check(TokenKind::Comptime) && pos_ + 1 < tokens_.size() &&
        tokens_[pos_ + 1].kind == TokenKind::LBrace) {
      match(TokenKind::Comptime);
      expect(TokenKind::LBrace);
      auto block = std::make_unique<Stmt>();
      block->kind = Stmt::ComptimeBlockK;
      block->comptime_block = parseBlock();
      expect(TokenKind::RBrace);
      program.comptime_stmts.push_back(std::move(block));
      continue;
    }
    if (check(TokenKind::Codegen) && pos_ + 1 < tokens_.size() &&
        tokens_[pos_ + 1].kind == TokenKind::LBrace) {
      match(TokenKind::Codegen);
      expect(TokenKind::LBrace);
      auto block = std::make_unique<Stmt>();
      block->kind = Stmt::CodegenBlockK;
      block->codegen_block = parseBlock();
      expect(TokenKind::RBrace);
      program.codegen_stmts.push_back(std::move(block));
      continue;
    }
    if (check(TokenKind::Constexpr) && pos_ + 1 < tokens_.size() &&
        tokens_[pos_ + 1].kind == TokenKind::Ident && pos_ + 2 < tokens_.size() &&
        tokens_[pos_ + 2].kind == TokenKind::Eq) {
      match(TokenKind::Constexpr);
      auto s = std::make_unique<Stmt>();
      s->kind = Stmt::LetStmt;
      s->let.name = expect(TokenKind::Ident).value;
      expect(TokenKind::Eq);
      s->let.value = parseExpr();
      s->let.is_constexpr = true;
      consumeOptionalSemi();
      program.comptime_stmts.push_back(std::move(s));
      continue;
    }
    if (check(TokenKind::Public) || check(TokenKind::Private) || check(TokenKind::Protected) ||
        check(TokenKind::Internal) || check(TokenKind::At) || check(TokenKind::Def) ||
        check(TokenKind::Constexpr) || check(TokenKind::Consteval) || check(TokenKind::Codegen) ||
        check(TokenKind::Async) || check(TokenKind::Coroutine) || check(TokenKind::Struct) ||
        check(TokenKind::Class) || check(TokenKind::Record) || check(TokenKind::Interface) ||
        check(TokenKind::Enum) || check(TokenKind::Flags) || check(TokenKind::Trait) ||
        check(TokenKind::Actor) || check(TokenKind::Exception) || check(TokenKind::Union) ||
        check(TokenKind::Extension)) {
      parseTopLevelDecl(program);
      continue;
    }
    break;
  }

  return program;

}



Param Parser::parseParam() {
  Param p;
  if (match(TokenKind::Ellipsis)) {
    p.is_variadic = true;
    p.name = expect(TokenKind::Ident).value;
    if (match(TokenKind::Colon))
      p.type = parseType();
    else
      p.type = TypeDesc::array(TypeDesc::prim(FarTypeId::I64));
    return p;
  }
  p.name = expect(TokenKind::Ident).value;
  if (match(TokenKind::Question)) {
    p.is_optional = true;
    if (match(TokenKind::Colon)) {
      p.type = parseType();
      p.type_explicit = true;
    }
  } else if (match(TokenKind::Colon)) {
    p.type = parseType();
    p.type_explicit = true;
    if (match(TokenKind::Question))
      p.is_optional = true;
  }
  if (match(TokenKind::Eq)) {
    p.default_value = parseExpr();
    p.is_optional = true;
  }
  if (!p.type_explicit && p.name == "self" && !current_type_name_.empty()) {
    p.type = TypeDesc::user(current_type_name_);
    for (const auto& tp : current_type_params_)
      p.type.args.push_back(TypeDesc::typeVar(tp.name));
  }
  return p;
}

std::vector<Attribute> Parser::parseAttributes() {
  std::vector<Attribute> attrs;
  while (match(TokenKind::At)) {
    Attribute a;
    if (check(TokenKind::Ident))
      a.name = expect(TokenKind::Ident).value;
    else if (check(TokenKind::Eof))
      throw FarError("expected attribute name after @", current().line, current().col);
    else {
      a.name = current().value;
      pos_++;
    }
    if (match(TokenKind::LParen)) {
      if (!check(TokenKind::RParen)) {
        do {
          AttrArg aa;
          if (check(TokenKind::Ident) && pos_ + 1 < tokens_.size() &&
              tokens_[pos_ + 1].kind == TokenKind::Eq) {
            aa.name = expect(TokenKind::Ident).value;
            expect(TokenKind::Eq);
            aa.value = parseExpr();
          } else {
            aa.value = parseExpr();
          }
          a.args.push_back(std::move(aa));
        } while (match(TokenKind::Comma));
      }
      expect(TokenKind::RParen);
    }
    attrs.push_back(std::move(a));
  }
  return attrs;
}

static std::string typeBaseName(const std::string& type_name) {
  const size_t dot = type_name.rfind('.');
  return dot == std::string::npos ? type_name : type_name.substr(dot + 1);
}

static bool isConstructorName(const std::string& type_name, const std::string& ident) {
  return ident == type_name || ident == typeBaseName(type_name);
}

UserMethod Parser::parseConstructorDecl() {
  UserMethod m;
  m.is_constructor = true;
  m.name = expect(TokenKind::Ident).value;
  if (!current_type_name_.empty() && !isConstructorName(current_type_name_, m.name))
    throw FarError("constructor name must match class '" + typeBaseName(current_type_name_) + "'",
                   current().line, current().col);
  expect(TokenKind::LParen);
  if (!check(TokenKind::RParen)) {
    do {
      m.params.push_back(parseParam());
    } while (match(TokenKind::Comma));
  }
  expect(TokenKind::RParen);
  if (match(TokenKind::Arrow))
    throw FarError("constructor cannot have a return type", current().line, current().col);
  expect(TokenKind::LBrace);
  m.body = parseBlock();
  expect(TokenKind::RBrace);
  return m;
}

UserMethod Parser::parseUserMethod(bool require_body) {
  UserMethod m;
  if (match(TokenKind::Static))
    m.is_static = true;
  expect(TokenKind::Def);
  if (check(TokenKind::Ident))
    m.name = expect(TokenKind::Ident).value;
  else if (check(TokenKind::TypeName))
    m.name = expect(TokenKind::TypeName).value;
  else
    throw FarError("expected method name", current().line, current().col);
  if (!m.is_static && !current_type_name_.empty() && isConstructorName(current_type_name_, m.name))
    throw FarError("constructors must not use 'fun'; use " + typeBaseName(current_type_name_) +
                       "(...) { ... }",
                   current().line, current().col);
  expect(TokenKind::LParen);
  if (!check(TokenKind::RParen)) {
    do {
      m.params.push_back(parseParam());
    } while (match(TokenKind::Comma));
  }
  expect(TokenKind::RParen);
  if (match(TokenKind::Arrow))
    m.return_type = parseType();
  if (match(TokenKind::LBrace)) {
    m.body = parseBlock();
    expect(TokenKind::RBrace);
  } else if (require_body) {
    throw FarError("method requires body", current().line, current().col);
  } else {
    consumeOptionalSemi();
  }
  return m;
}

PropertyDef Parser::parsePropertyDecl() {
  expect(TokenKind::Prop);
  PropertyDef p;
  p.name = expect(TokenKind::Ident).value;
  expect(TokenKind::Colon);
  p.type = parseType();
  expect(TokenKind::LBrace);
  while (!check(TokenKind::RBrace) && !check(TokenKind::Eof)) {
    if (check(TokenKind::Ident) && current().value == "get") {
      pos_++;
      expect(TokenKind::LBrace);
      p.getter = parseBlock();
      expect(TokenKind::RBrace);
    } else if (check(TokenKind::Ident) && current().value == "set") {
      pos_++;
      if (match(TokenKind::LParen)) {
        if (!check(TokenKind::RParen))
          p.setter_param = expect(TokenKind::Ident).value;
        expect(TokenKind::RParen);
      }
      expect(TokenKind::LBrace);
      p.setter = parseBlock();
      expect(TokenKind::RBrace);
    } else {
      throw FarError("property requires get or set block", current().line, current().col);
    }
  }
  expect(TokenKind::RBrace);
  return p;
}

OperatorDef Parser::parseOperatorDecl() {
  expect(TokenKind::Operator);
  OperatorDef op;
  if (match(TokenKind::LBracket)) {
    expect(TokenKind::RBracket);
    op.op = "[]";
  } else if (matchAny({TokenKind::Plus, TokenKind::Minus, TokenKind::Star, TokenKind::Slash,
                       TokenKind::Percent, TokenKind::StarStar, TokenKind::FloorDiv, TokenKind::EqEq,
                       TokenKind::BangEq, TokenKind::Lt, TokenKind::Gt, TokenKind::Lte, TokenKind::Gte,
                       TokenKind::Amp, TokenKind::Pipe, TokenKind::Caret, TokenKind::LShift,
                       TokenKind::RShift})) {
    op.op = tokens_[pos_ - 1].value;
  } else {
    throw FarError("expected operator symbol", current().line, current().col);
  }
  expect(TokenKind::LParen);
  if (!check(TokenKind::RParen)) {
    do {
      op.params.push_back(parseParam());
    } while (match(TokenKind::Comma));
  }
  expect(TokenKind::RParen);
  if (match(TokenKind::Arrow))
    op.return_type = parseType();
  expect(TokenKind::LBrace);
  op.body = parseBlock();
  expect(TokenKind::RBrace);
  return op;
}

void Parser::parseEnumBody(UserTypeDef& td) {
  expect(TokenKind::LBrace);
  int64_t next_val = 0;
  while (!check(TokenKind::RBrace) && !check(TokenKind::Eof)) {
    EnumVariant v;
    v.name = expect(TokenKind::Ident).value;
    if (match(TokenKind::Eq)) {
      if (!match(TokenKind::Int))
        throw FarError("enum value must be integer", current().line, current().col);
      v.value = std::stoll(tokens_[pos_ - 1].value);
      next_val = v.value + 1;
    } else {
      v.value = next_val++;
    }
    td.variants.push_back(std::move(v));
    match(TokenKind::Comma);
  }
  expect(TokenKind::RBrace);
}

std::string Parser::parseTypeIdent() {
  if (match(TokenKind::Ident) || match(TokenKind::TypeName))
    return tokens_[pos_ - 1].value;
  throw FarError("expected identifier", current().line, current().col);
}

std::vector<TypeParam> Parser::parseTypeParams() {
  std::vector<TypeParam> params;
  if (!match(TokenKind::Lt))
    return params;
  do {
    TypeParam tp;
    tp.name = parseTypeIdent();
    if (match(TokenKind::Colon))
      tp.constraint = parseTypeIdent();
    params.push_back(std::move(tp));
  } while (match(TokenKind::Comma));
  expect(TokenKind::Gt);
  return params;
}

void Parser::parseUnionBody(UserTypeDef& td) {
  expect(TokenKind::LBrace);
  int64_t next_val = 0;
  while (!check(TokenKind::RBrace) && !check(TokenKind::Eof)) {
    EnumVariant v;
    v.name = expect(TokenKind::Ident).value;
    if (match(TokenKind::LParen)) {
      if (!check(TokenKind::RParen)) {
        do {
          UserField f;
          f.name = expect(TokenKind::Ident).value;
          expect(TokenKind::Colon);
          f.type = parseType();
          v.fields.push_back(std::move(f));
        } while (match(TokenKind::Comma));
      }
      expect(TokenKind::RParen);
    }
    if (match(TokenKind::Eq)) {
      if (!match(TokenKind::Int))
        throw FarError("union variant value must be integer", current().line, current().col);
      v.value = std::stoll(tokens_[pos_ - 1].value);
      next_val = v.value + 1;
    } else {
      v.value = next_val++;
    }
    td.variants.push_back(std::move(v));
    match(TokenKind::Comma);
  }
  expect(TokenKind::RBrace);
}

void Parser::parseTypeBody(UserTypeDef& td, bool allow_bodies) {
  if (td.kind == UserTypeKind::Enum || td.kind == UserTypeKind::FlagsEnum) {
    parseEnumBody(td);
    return;
  }
  if (td.kind == UserTypeKind::Union) {
    parseUnionBody(td);
    return;
  }
  expect(TokenKind::LBrace);
  while (!check(TokenKind::RBrace) && !check(TokenKind::Eof)) {
    Visibility vis = parseVisibility();
    if (match(TokenKind::Prop)) {
      pos_--;
      PropertyDef prop = parsePropertyDecl();
      prop.visibility = vis;
      td.properties.push_back(std::move(prop));
    } else if (match(TokenKind::Operator)) {
      pos_--;
      OperatorDef op = parseOperatorDecl();
      if (op.op == "[]") {
        IndexerDef idx;
        if (!op.params.empty()) {
          idx.key_type = op.params[0].type;
          idx.key_param = op.params[0].name;
        }
        idx.value_type = op.return_type;
        idx.getter_body = std::move(op.body);
        td.indexers.push_back(std::move(idx));
      } else {
        td.operators.push_back(std::move(op));
      }
    } else if (check(TokenKind::Def) || check(TokenKind::Static)) {
      bool req_body = td.kind != UserTypeKind::Interface && td.kind != UserTypeKind::Trait;
      UserMethod m = parseUserMethod(req_body);
      m.visibility = vis;
      td.methods.push_back(std::move(m));
    } else if (check(TokenKind::Ident) && pos_ + 1 < tokens_.size() &&
               tokens_[pos_ + 1].kind == TokenKind::LParen) {
      if (!isConstructorName(td.name, current().value)) {
        const std::string bad = current().value;
        throw FarError("constructor name must match class '" + typeBaseName(td.name) + "' (found '" +
                           bad + "')",
                       current().line, current().col);
      }
      UserMethod m = parseConstructorDecl();
      m.visibility = vis;
      td.methods.push_back(std::move(m));
    } else {
      UserField f;
      f.visibility = vis;
      f.name = expect(TokenKind::Ident).value;
      expect(TokenKind::Colon);
      f.type = parseType();
      if (match(TokenKind::Eq))
        f.default_value = parseExpr();
      td.fields.push_back(std::move(f));
      consumeOptionalSemi();
    }
  }
  expect(TokenKind::RBrace);
}

UserTypeDef Parser::parseUserTypeDecl(UserTypeKind kind) {
  UserTypeDef td;
  td.kind = kind;
  td.name = qualifyName(parseTypeIdent());
  user_type_names_.insert(td.name);
  current_type_name_ = td.name;
  td.type_params = parseTypeParams();
  current_type_params_ = td.type_params;
  if (kind == UserTypeKind::Record && match(TokenKind::LParen)) {
    if (!check(TokenKind::RParen)) {
      do {
        UserField f;
        f.name = expect(TokenKind::Ident).value;
        expect(TokenKind::Colon);
        f.type = parseType();
        td.fields.push_back(std::move(f));
      } while (match(TokenKind::Comma));
    }
    expect(TokenKind::RParen);
    current_type_name_.clear();
    current_type_params_.clear();
    return td;
  }
  if (match(TokenKind::With)) {
    do {
      td.mixins.push_back(expect(TokenKind::Ident).value);
    } while (match(TokenKind::Comma));
  }
  if (match(TokenKind::Implements)) {
    do {
      td.implements.push_back(expect(TokenKind::Ident).value);
    } while (match(TokenKind::Comma));
  }
  parseTypeBody(td, kind != UserTypeKind::Interface);
  current_type_name_.clear();
  current_type_params_.clear();
  return td;
}

ExtensionDef Parser::parseExtensionDecl() {
  ExtensionDef ext;
  ext.target_type = expect(TokenKind::Ident).value;
  current_type_name_ = ext.target_type;
  expect(TokenKind::LBrace);
  while (!check(TokenKind::RBrace) && !check(TokenKind::Eof)) {
    ext.methods.push_back(parseUserMethod(true));
  }
  expect(TokenKind::RBrace);
  current_type_name_.clear();
  return ext;
}

std::vector<CallArg> Parser::parseCallArgs() {
  std::vector<CallArg> args;
  if (check(TokenKind::RParen))
    return args;
  do {
    CallArg ca;
    if (check(TokenKind::Ident) && pos_ + 1 < tokens_.size() && tokens_[pos_ + 1].kind == TokenKind::Eq) {
      ca.name = expect(TokenKind::Ident).value;
      expect(TokenKind::Eq);
      ca.value = parseExpr();
    } else {
      ca.value = parseExpr();
    }
    args.push_back(std::move(ca));
  } while (match(TokenKind::Comma));
  return args;
}

std::unique_ptr<Expr> Parser::parseFnLitExpr(bool pipe_form) {
  if (!pipe_form) {
    if (!match(TokenKind::Fn) && !match(TokenKind::Lambda))
      throw FarError("expected fn or lambda", current().line, current().col);
  }
  FnLit lit;
  expect(TokenKind::LParen);
  if (!check(TokenKind::RParen)) {
    do {
      lit.params.push_back(parseParam());
    } while (match(TokenKind::Comma));
  }
  expect(TokenKind::RParen);
  if (match(TokenKind::Arrow))
    lit.return_type = parseType();
  if (match(TokenKind::LBrace)) {
    lit.body = parseBlock();
    expect(TokenKind::RBrace);
  } else {
    lit.expr_body = parseExpr();
  }
  std::vector<TypeDesc> ptypes;
  for (const auto& p : lit.params) {
    if (!p.is_variadic)
      ptypes.push_back(p.type);
  }
  TypeDesc ret = lit.return_type;
  return Expr::makeFnLit(std::move(lit), TypeDesc::function(std::move(ptypes), ret));
}

MacroDef Parser::parseMacroDecl() {
  std::string name = expect(TokenKind::Ident).value;
  expect(TokenKind::Bang);
  expect(TokenKind::LParen);
  MacroDef m;
  m.name = std::move(name);
  if (!check(TokenKind::RParen)) {
    do {
      m.params.push_back(expect(TokenKind::Ident).value);
    } while (match(TokenKind::Comma));
  }
  expect(TokenKind::RParen);
  expect(TokenKind::Eq);
  m.body = parseExpr();
  consumeOptionalSemi();
  return m;
}

Function Parser::parseFunction() {
  bool is_constexpr = match(TokenKind::Constexpr);
  bool is_consteval = match(TokenKind::Consteval);
  bool is_codegen = match(TokenKind::Codegen);
  bool is_async = match(TokenKind::Async);
  bool is_coro = match(TokenKind::Coroutine);
  expect(TokenKind::Def);
  Function fn;
  fn.is_constexpr = is_constexpr;
  fn.is_consteval = is_consteval;
  fn.is_codegen = is_codegen;
  fn.is_async = is_async;
  fn.is_coroutine = is_coro;
  fn.name = qualifyName(expect(TokenKind::Ident).value);
  fn.type_params = parseTypeParams();
  expect(TokenKind::LParen);
  if (!check(TokenKind::RParen)) {
    do {
      fn.params.push_back(parseParam());
    } while (match(TokenKind::Comma));
  }
  expect(TokenKind::RParen);
  if (match(TokenKind::Arrow)) {
    fn.return_type_explicit = true;
    fn.return_type = parseType();
  }
  expect(TokenKind::LBrace);
  fn.body = parseBlock();
  for (const auto& st : fn.body) {
    if (st->kind == Stmt::YieldStmtK) {
      fn.is_generator = true;
      break;
    }
  }
  if (fn.is_coroutine)
    fn.is_generator = true;
  expect(TokenKind::RBrace);
  return fn;
}



std::vector<std::unique_ptr<Stmt>> Parser::parseBlock() {

  std::vector<std::unique_ptr<Stmt>> stmts;

  while (!check(TokenKind::RBrace) && !check(TokenKind::Eof))

    stmts.push_back(parseStmt());

  return stmts;

}



std::unique_ptr<Stmt> Parser::parseIfStmt() {

  expect(TokenKind::If);

  auto s = std::make_unique<Stmt>();

  s->kind = Stmt::IfStmt;

  expect(TokenKind::LParen);

  IfClause first;

  first.condition = parseExpr();

  expect(TokenKind::RParen);

  expect(TokenKind::LBrace);

  first.body = parseBlock();

  expect(TokenKind::RBrace);

  s->if_stmt.clauses.push_back(std::move(first));



  while (match(TokenKind::Else)) {

    if (match(TokenKind::If)) {

      IfClause elif;

      expect(TokenKind::LParen);

      elif.condition = parseExpr();

      expect(TokenKind::RParen);

      expect(TokenKind::LBrace);

      elif.body = parseBlock();

      expect(TokenKind::RBrace);

      s->if_stmt.clauses.push_back(std::move(elif));

    } else {

      expect(TokenKind::LBrace);

      s->if_stmt.else_body = parseBlock();

      expect(TokenKind::RBrace);

      break;

    }

  }

  return s;

}



std::unique_ptr<Stmt> Parser::parseForStmt() {

  bool is_parallel = match(TokenKind::Parallel);

  expect(TokenKind::For);

  auto s = std::make_unique<Stmt>();

  s->kind = Stmt::ForStmt;

  // for i in 0..n { }  — range form (no parens)

  if (check(TokenKind::Ident) && pos_ + 1 < tokens_.size() &&
      tokens_[pos_ + 1].kind == TokenKind::In) {

    std::string var = expect(TokenKind::Ident).value;

    expect(TokenKind::In);

    auto first = parseExpr();

    if (check(TokenKind::DotDot) || check(TokenKind::DotDotLt)) {
      auto start = std::move(first);

      if (!match(TokenKind::DotDotLt))
        expect(TokenKind::DotDot);

      auto end = parseExpr();

      expect(TokenKind::LBrace);

      s->for_stmt.body = parseBlock();

      expect(TokenKind::RBrace);

      if (is_parallel) {
        s->for_stmt.is_parallel = true;
        s->for_stmt.parallel_var = var;
        s->for_stmt.range_start = std::move(start);
        s->for_stmt.range_end = std::move(end);
        return s;
      }

      auto init = std::make_unique<Stmt>();

      init->kind = Stmt::LetStmt;

      init->let.name = var;

      init->let.value = std::move(start);

      auto step = std::make_unique<Stmt>();

      step->kind = Stmt::LetStmt;

      step->let.name = var;

      step->let.value = Expr::makeBinOp("+", Expr::makeVar(var), Expr::makeInt(1));

      s->for_stmt.init = std::move(init);

      s->for_stmt.cond = Expr::makeBinOp("<", Expr::makeVar(var), std::move(end));

      s->for_stmt.step = std::move(step);

      return s;
    }

    if (is_parallel)
      throw FarError("parallel for requires range form: parallel for i in start..end { }");

    expect(TokenKind::LBrace);

    s->for_stmt.body = parseBlock();

    expect(TokenKind::RBrace);

    s->for_stmt.is_foreach = true;

    s->for_stmt.foreach_var = var;

    s->for_stmt.foreach_iter = std::move(first);

    return s;

  }

  if (is_parallel)
    throw FarError("parallel for requires range form: parallel for i in start..end { }");

  expect(TokenKind::LParen);

  // C-style: for (init; cond; step)

  if (!check(TokenKind::Semi)) {

    if (check(TokenKind::Ident) && pos_ + 1 < tokens_.size() && tokens_[pos_ + 1].kind == TokenKind::Eq) {

      auto init = std::make_unique<Stmt>();

      init->kind = Stmt::LetStmt;

      init->let.name = expect(TokenKind::Ident).value;

      expect(TokenKind::Eq);

      init->let.value = parseExpr();

      s->for_stmt.init = std::move(init);

    } else {

      auto init = std::make_unique<Stmt>();

      init->kind = Stmt::ExprStmtK;

      init->expr_stmt.expr = parseExpr();

      s->for_stmt.init = std::move(init);

    }

  }

  expect(TokenKind::Semi);



  if (!check(TokenKind::Semi))

    s->for_stmt.cond = parseExpr();

  expect(TokenKind::Semi);



  if (!check(TokenKind::RParen)) {

    if (check(TokenKind::Ident) && pos_ + 1 < tokens_.size() && tokens_[pos_ + 1].kind == TokenKind::Eq) {

      auto step = std::make_unique<Stmt>();

      step->kind = Stmt::LetStmt;

      step->let.name = expect(TokenKind::Ident).value;

      expect(TokenKind::Eq);

      step->let.value = parseExpr();

      s->for_stmt.step = std::move(step);

    } else {

      auto step = std::make_unique<Stmt>();

      step->kind = Stmt::ExprStmtK;

      step->expr_stmt.expr = parseExpr();

      s->for_stmt.step = std::move(step);

    }

  }

  expect(TokenKind::RParen);

  expect(TokenKind::LBrace);

  s->for_stmt.body = parseBlock();

  expect(TokenKind::RBrace);

  return s;

}

std::unique_ptr<Stmt> Parser::parseTryStmt() {
  expect(TokenKind::Try);
  expect(TokenKind::LBrace);
  auto s = std::make_unique<Stmt>();
  s->kind = Stmt::TryStmtK;
  s->try_stmt.try_body = parseBlock();
  expect(TokenKind::RBrace);
  if (match(TokenKind::Catch)) {
    s->try_stmt.has_catch = true;
    expect(TokenKind::LParen);
    s->try_stmt.catch_var = expect(TokenKind::Ident).value;
    expect(TokenKind::RParen);
    expect(TokenKind::LBrace);
    s->try_stmt.catch_body = parseBlock();
    expect(TokenKind::RBrace);
  }
  if (match(TokenKind::Finally)) {
    s->try_stmt.has_finally = true;
    expect(TokenKind::LBrace);
    s->try_stmt.finally_body = parseBlock();
    expect(TokenKind::RBrace);
  }
  if (!s->try_stmt.has_catch && !s->try_stmt.has_finally)
    throw FarError("try requires catch and/or finally");
  return s;
}

std::unique_ptr<Pattern> Parser::parsePattern() {
  if (match(TokenKind::Int)) {
    auto p = std::make_unique<Pattern>();
    p->kind = PatKind::Literal;
    p->literal = std::stoll(tokens_[pos_ - 1].value);
    return p;
  }
  if (match(TokenKind::LParen)) {
    auto p = std::make_unique<Pattern>();
    p->kind = PatKind::TupleDestructure;
    if (!check(TokenKind::RParen)) {
      do {
        p->fields.push_back(parsePattern());
      } while (match(TokenKind::Comma));
    }
    expect(TokenKind::RParen);
    return p;
  }
  if (!check(TokenKind::Ident))
    throw FarError("expected pattern", current().line, current().col);
  std::string name = current().value;
  if (name == "_") {
    expect(TokenKind::Ident);
    auto p = std::make_unique<Pattern>();
    p->kind = PatKind::Wildcard;
    return p;
  }
  if (user_type_names_.count(name) && pos_ + 1 < tokens_.size()) {
    TokenKind nk = tokens_[pos_ + 1].kind;
    if (nk == TokenKind::Dot) {
      expect(TokenKind::Ident);
      expect(TokenKind::Dot);
      std::string variant = expect(TokenKind::Ident).value;
      if (enum_type_names_.count(name)) {
        auto p = std::make_unique<Pattern>();
        p->kind = PatKind::EnumVariant;
        p->type_name = name;
        p->variant = variant;
        return p;
      }
      if (union_type_names_.count(name)) {
        auto p = std::make_unique<Pattern>();
        p->kind = PatKind::UnionVariant;
        p->type_name = name;
        p->variant = variant;
        if (match(TokenKind::LParen)) {
          if (!check(TokenKind::RParen)) {
            do {
              p->fields.push_back(parsePattern());
            } while (match(TokenKind::Comma));
          }
          expect(TokenKind::RParen);
        }
        return p;
      }
      throw FarError("unknown enum/union type '" + name + "'", current().line, current().col);
    }
    if (nk == TokenKind::LParen) {
      expect(TokenKind::Ident);
      expect(TokenKind::LParen);
      auto p = std::make_unique<Pattern>();
      p->kind = PatKind::StructDestructure;
      p->type_name = name;
      if (!check(TokenKind::RParen)) {
        do {
          p->fields.push_back(parsePattern());
        } while (match(TokenKind::Comma));
      }
      expect(TokenKind::RParen);
      return p;
    }
    if (nk == TokenKind::LBrace) {
      expect(TokenKind::Ident);
      expect(TokenKind::LBrace);
      auto p = std::make_unique<Pattern>();
      p->kind = PatKind::StructDestructure;
      p->type_name = name;
      while (!check(TokenKind::RBrace) && !check(TokenKind::Eof)) {
        std::string fname = expect(TokenKind::Ident).value;
        p->field_names.push_back(fname);
        if (match(TokenKind::Colon))
          p->fields.push_back(parsePattern());
        else {
          auto bp = std::make_unique<Pattern>();
          bp->kind = PatKind::Bind;
          bp->bind_name = fname;
          p->fields.push_back(std::move(bp));
        }
        match(TokenKind::Comma);
      }
      expect(TokenKind::RBrace);
      return p;
    }
    if (nk == TokenKind::FatArrow || nk == TokenKind::Comma || nk == TokenKind::RBrace ||
        nk == TokenKind::Colon) {
      expect(TokenKind::Ident);
      auto p = std::make_unique<Pattern>();
      p->kind = PatKind::TypeTest;
      p->type_name = name;
      p->type_test = TypeDesc::user(name);
      return p;
    }
  }
  expect(TokenKind::Ident);
  auto p = std::make_unique<Pattern>();
  p->kind = PatKind::Bind;
  p->bind_name = name;
  return p;
}

std::vector<std::unique_ptr<Stmt>> Parser::parseMatchArmBody() {
  if (check(TokenKind::LBrace)) {
    expect(TokenKind::LBrace);
    auto body = parseBlock();
    expect(TokenKind::RBrace);
    return body;
  }
  std::vector<std::unique_ptr<Stmt>> body;
  body.push_back(parseStmt());
  return body;
}

std::unique_ptr<Stmt> Parser::parseMatchStmt() {
  expect(TokenKind::Match);
  auto s = std::make_unique<Stmt>();
  s->kind = Stmt::MatchStmtK;
  s->match_stmt.scrutinee = parseExpr();
  expect(TokenKind::LBrace);
  while (!check(TokenKind::RBrace) && !check(TokenKind::Eof)) {
    MatchArm arm;
    arm.pat = parsePattern();
    expect(TokenKind::FatArrow);
    arm.body = parseMatchArmBody();
    s->match_stmt.arms.push_back(std::move(arm));
  }
  expect(TokenKind::RBrace);
  return s;
}

std::unique_ptr<Stmt> Parser::parseSwitchStmt() {
  expect(TokenKind::Switch);
  auto s = std::make_unique<Stmt>();
  s->kind = Stmt::MatchStmtK;
  s->match_stmt.is_switch = true;
  s->match_stmt.scrutinee = parseExpr();
  expect(TokenKind::LBrace);
  while (!check(TokenKind::RBrace) && !check(TokenKind::Eof)) {
    MatchArm arm;
    if (match(TokenKind::Default)) {
      arm.pat = std::make_unique<Pattern>();
      arm.pat->kind = PatKind::Wildcard;
    } else {
      expect(TokenKind::Case);
      arm.pat = parsePattern();
    }
    expect(TokenKind::Colon);
    arm.body = parseMatchArmBody();
    s->match_stmt.arms.push_back(std::move(arm));
  }
  expect(TokenKind::RBrace);
  return s;
}

std::unique_ptr<Stmt> Parser::parseStmt() {

  if (match(TokenKind::Return)) {

    auto s = std::make_unique<Stmt>();

    s->kind = Stmt::ReturnStmt;

    s->ret.has_value = !check(TokenKind::Semi) && !check(TokenKind::RBrace);

    if (s->ret.has_value)

      s->ret.value = parseExpr();

    consumeOptionalSemi();

    return s;

  }

  if (match(TokenKind::Yield)) {

    auto s = std::make_unique<Stmt>();

    s->kind = Stmt::YieldStmtK;

    s->yield.has_value = !check(TokenKind::Semi) && !check(TokenKind::RBrace);

    if (s->yield.has_value)

      s->yield.value = parseExpr();

    consumeOptionalSemi();

    return s;

  }

  if (check(TokenKind::If))

    return parseIfStmt();

  if (check(TokenKind::For) || check(TokenKind::Parallel))

    return parseForStmt();

  if (match(TokenKind::While)) {

    auto s = std::make_unique<Stmt>();

    s->kind = Stmt::WhileStmt;

    expect(TokenKind::LParen);

    s->while_stmt.condition = parseExpr();

    expect(TokenKind::RParen);

    expect(TokenKind::LBrace);

    s->while_stmt.body = parseBlock();

    expect(TokenKind::RBrace);

    return s;

  }

  if (match(TokenKind::Break)) {

    auto s = std::make_unique<Stmt>();

    s->kind = Stmt::BreakStmt;

    consumeOptionalSemi();

    return s;

  }

  if (match(TokenKind::Continue)) {

    auto s = std::make_unique<Stmt>();

    s->kind = Stmt::ContinueStmt;

    consumeOptionalSemi();

    return s;

  }

  if (check(TokenKind::Try))
    return parseTryStmt();

  if (check(TokenKind::Match))
    return parseMatchStmt();

  if (check(TokenKind::Switch))
    return parseSwitchStmt();

  if (match(TokenKind::Throw)) {
    auto s = std::make_unique<Stmt>();
    s->kind = Stmt::ThrowStmtK;
    s->throw_stmt.value = parseExpr();
    consumeOptionalSemi();
    return s;
  }

  if (match(TokenKind::Defer)) {
    auto s = std::make_unique<Stmt>();
    s->kind = Stmt::DeferStmtK;
    s->defer.expr = parseExpr();
    consumeOptionalSemi();
    return s;
  }

  if (match(TokenKind::Unsafe)) {
    auto s = std::make_unique<Stmt>();
    s->kind = Stmt::UnsafeStmtK;
    expect(TokenKind::LBrace);
    s->unsafe.body = parseBlock();
    expect(TokenKind::RBrace);
    return s;
  }

  if (check(TokenKind::Constexpr) && pos_ + 1 < tokens_.size() &&
      tokens_[pos_ + 1].kind == TokenKind::Ident && pos_ + 2 < tokens_.size() &&
      tokens_[pos_ + 2].kind == TokenKind::Eq) {
    match(TokenKind::Constexpr);
    auto s = std::make_unique<Stmt>();
    s->kind = Stmt::LetStmt;
    s->let.name = expect(TokenKind::Ident).value;
    expect(TokenKind::Eq);
    s->let.value = parseExpr();
    s->let.is_constexpr = true;
    consumeOptionalSemi();
    return s;
  }

  if ((check(TokenKind::Ident) || check(TokenKind::TypeName)) && pos_ + 1 < tokens_.size()) {

    TokenKind next = tokens_[pos_ + 1].kind;

    if (next == TokenKind::Eq || next == TokenKind::Colon) {

      auto s = std::make_unique<Stmt>();

      s->kind = Stmt::LetStmt;

      s->let.name = check(TokenKind::Ident) ? expect(TokenKind::Ident).value : expect(TokenKind::TypeName).value;

      if (match(TokenKind::Colon)) {

        s->let.type = parseType();

        s->let.explicit_type = true;

        expect(TokenKind::Eq);

        s->let.value = parseExpr();

      } else {

        expect(TokenKind::Eq);

        s->let.value = parseExpr();

        if (s->let.value)

          s->let.type = s->let.value->type;

      }

      consumeOptionalSemi();

      return s;

    }

  }



  auto expr = parseAssign();

  if (expr->kind == Expr::FnCall && expr->call.name == "print") {

    auto s = std::make_unique<Stmt>();

    s->kind = Stmt::PrintStmt;

    if (expr->call.args.empty())

      s->print.value = Expr::makeInt(0);

    else

      s->print.value = std::move(expr->call.args[0].value);

    consumeOptionalSemi();

    return s;

  }

  auto s = std::make_unique<Stmt>();

  s->kind = Stmt::ExprStmtK;

  s->expr_stmt.expr = std::move(expr);

  consumeOptionalSemi();

  return s;

}



static bool isAssignOpToken(TokenKind k) {
  switch (k) {
    case TokenKind::Eq:
    case TokenKind::PlusEq:
    case TokenKind::MinusEq:
    case TokenKind::StarEq:
    case TokenKind::SlashEq:
    case TokenKind::PercentEq:
    case TokenKind::StarStarEq:
    case TokenKind::FloorDivEq:
    case TokenKind::AmpEq:
    case TokenKind::PipeEq:
    case TokenKind::CaretEq:
    case TokenKind::LShiftEq:
    case TokenKind::RShiftEq:
    case TokenKind::QuestionEq:
      return true;
    default:
      return false;
  }
}

static bool isAssignableExpr(const Expr& e) {
  return e.kind == Expr::Variable || e.kind == Expr::IndexExpr || e.kind == Expr::MemberExpr ||
         (e.kind == Expr::PrefixExprK && e.prefix.op == "*");
}

static std::unique_ptr<Expr> desugarPipeline(std::unique_ptr<Expr> left, std::unique_ptr<Expr> right) {
  if (right->kind == Expr::FnCall) {
    CallArg ca;
    ca.value = std::move(left);
    right->call.args.insert(right->call.args.begin(), std::move(ca));
    return right;
  }
  if (right->kind == Expr::Variable) {
    std::vector<std::unique_ptr<Expr>> args;
    args.push_back(std::move(left));
    return Expr::makeCall(right->var.name, std::move(args));
  }
  return Expr::makeBinOp("|>", std::move(left), std::move(right));
}

std::unique_ptr<Expr> Parser::parseExpr() { return parseAssign(); }

std::unique_ptr<Expr> Parser::parseAssign() {
  auto left = parsePipeline();
  if (isAssignOpToken(current().kind)) {
    std::string op = tokens_[pos_].value;
    if (!isAssignableExpr(*left))
      throw FarError("assignment requires variable or index target", current().line, current().col);
    pos_++;
    auto right = parseAssign();
    return Expr::makeAssign(std::move(op), std::move(left), std::move(right));
  }
  return left;
}

std::unique_ptr<Expr> Parser::parsePipeline() {
  auto left = parseNullCoalesce();
  while (match(TokenKind::PipeGt)) {
    auto right = parseUnary();
    left = desugarPipeline(std::move(left), std::move(right));
  }
  return left;
}

std::unique_ptr<Expr> Parser::parseNullCoalesce() {
  auto left = parseTernary();
  while (match(TokenKind::QuestionQuestion)) {
    auto right = parseTernary();
    left = Expr::makeBinOp("??", std::move(left), std::move(right));
  }
  return left;
}

std::unique_ptr<Expr> Parser::parseTernary() {
  auto left = parseLogicalOr();
  if (match(TokenKind::Question)) {
    auto then_br = parseTernary();
    expect(TokenKind::Colon);
    auto else_br = parseTernary();
    TypeDesc ty = then_br ? then_br->type : TypeDesc::prim(FarTypeId::I64);
    return Expr::makeTernary(std::move(left), std::move(then_br), std::move(else_br), ty);
  }
  return left;
}

std::unique_ptr<Expr> Parser::parseLogicalOr() {
  auto left = parseLogicalAnd();
  while (matchAny({TokenKind::Or, TokenKind::PipePipe})) {
    std::string op = tokens_[pos_ - 1].value;
    if (op == "||")
      op = "||";
    else
      op = "or";
    auto right = parseLogicalAnd();
    left = Expr::makeBinOp(op, std::move(left), std::move(right));
  }
  return left;
}

std::unique_ptr<Expr> Parser::parseLogicalAnd() {
  auto left = parseIdentity();
  while (matchAny({TokenKind::And, TokenKind::AmpAmp})) {
    std::string op = tokens_[pos_ - 1].value;
    if (op == "&&")
      op = "&&";
    else
      op = "and";
    auto right = parseIdentity();
    left = Expr::makeBinOp(op, std::move(left), std::move(right));
  }
  return left;
}

std::unique_ptr<Expr> Parser::parseIdentity() {
  auto left = parseComparison();
  while (matchAny({TokenKind::EqEqEq, TokenKind::BangEqEq})) {
    std::string op = tokens_[pos_ - 1].value;
    auto right = parseComparison();
    left = Expr::makeBinOp(op, std::move(left), std::move(right));
  }
  return left;
}

std::unique_ptr<Expr> Parser::parseComparison() {
  auto left = parseMembership();
  while (matchAny({TokenKind::EqEq, TokenKind::BangEq, TokenKind::Lt, TokenKind::Gt, TokenKind::Lte,
                  TokenKind::Gte})) {
    std::string op = tokens_[pos_ - 1].value;
    auto right = parseMembership();
    left = Expr::makeBinOp(op, std::move(left), std::move(right));
  }
  return left;
}

std::unique_ptr<Expr> Parser::parseMembership() {
  auto left = parseIsAs();
  while (true) {
    if (match(TokenKind::In)) {
      auto right = parseIsAs();
      left = Expr::makeBinOp("in", std::move(left), std::move(right));
    } else if (match(TokenKind::Not) && check(TokenKind::In)) {
      expect(TokenKind::In);
      auto right = parseIsAs();
      left = Expr::makeBinOp("not in", std::move(left), std::move(right));
    } else {
      break;
    }
  }
  return left;
}

std::unique_ptr<Expr> Parser::parseIsAs() {
  auto left = parseBitOr();
  if (match(TokenKind::Is)) {
    TypeDesc ty = parseType();
    return Expr::makeIs(std::move(left), ty);
  }
  if (match(TokenKind::As)) {
    TypeDesc ty = parseType();
    return Expr::makeAs(std::move(left), ty);
  }
  return left;
}

std::unique_ptr<Expr> Parser::parseBitOr() {
  auto left = parseBitXor();
  while (match(TokenKind::Pipe)) {
    auto right = parseBitXor();
    left = Expr::makeBinOp("|", std::move(left), std::move(right));
  }
  return left;
}

std::unique_ptr<Expr> Parser::parseBitXor() {
  auto left = parseBitAnd();
  while (match(TokenKind::Caret)) {
    auto right = parseBitAnd();
    left = Expr::makeBinOp("^", std::move(left), std::move(right));
  }
  return left;
}

std::unique_ptr<Expr> Parser::parseBitAnd() {
  auto left = parseShift();
  while (match(TokenKind::Amp)) {
    auto right = parseShift();
    left = Expr::makeBinOp("&", std::move(left), std::move(right));
  }
  return left;
}

std::unique_ptr<Expr> Parser::parseShift() {
  auto left = parseTerm();
  while (matchAny({TokenKind::LShift, TokenKind::RShift})) {
    std::string op = tokens_[pos_ - 1].value;
    auto right = parseTerm();
    left = Expr::makeBinOp(op, std::move(left), std::move(right));
  }
  return left;
}

std::unique_ptr<Expr> Parser::parseTerm() {
  auto left = parseFactor();
  while (matchAny({TokenKind::Plus, TokenKind::Minus})) {
    std::string op = tokens_[pos_ - 1].value;
    auto right = parseFactor();
    left = Expr::makeBinOp(op, std::move(left), std::move(right));
    if (op == "+" && (isPrim(left->bin_op.left->type, FarTypeId::String) ||
                      isPrim(left->bin_op.right->type, FarTypeId::String)))
      left->type = TypeDesc::prim(FarTypeId::String);
  }
  return left;
}

std::unique_ptr<Expr> Parser::parseFactor() {
  auto left = parsePower();
  while (matchAny({TokenKind::Star, TokenKind::Slash, TokenKind::FloorDiv, TokenKind::Percent})) {
    std::string op = tokens_[pos_ - 1].value;
    auto right = parsePower();
    left = Expr::makeBinOp(op, std::move(left), std::move(right));
  }
  return left;
}

std::unique_ptr<Expr> Parser::parsePower() {
  auto left = parseUnary();
  if (match(TokenKind::StarStar)) {
    auto right = parsePower();
    return Expr::makeBinOp("**", std::move(left), std::move(right));
  }
  return left;
}

std::unique_ptr<Expr> Parser::parseUnary() {
  if (match(TokenKind::PlusPlus))
    return Expr::makePrefix("++", parseUnary());
  if (match(TokenKind::MinusMinus))
    return Expr::makePrefix("--", parseUnary());
  if (match(TokenKind::Tilde))
    return Expr::makePrefix("~", parseUnary());
  if (match(TokenKind::Minus))
    return Expr::makeBinOp("-", Expr::makeInt(0), parseUnary());
  if (match(TokenKind::Bang))
    return Expr::makeBinOp("!", Expr::makeInt(0), parseUnary());
  if (match(TokenKind::Star))
    return Expr::makePrefix("*", parseUnary());
  if (match(TokenKind::Amp))
    return Expr::makePrefix("&", parseUnary());
  if (match(TokenKind::Typeof)) {
    if (check(TokenKind::LParen)) {
      expect(TokenKind::LParen);
      std::unique_ptr<Expr> e;
      if (check(TokenKind::TypeName) || check(TokenKind::Fn) ||
          (check(TokenKind::Ident) &&
           (user_type_names_.count(current().value) || isTypeName(current().value))))
        e = Expr::makeTypeUnaryType("typeof", parseType());
      else
        e = Expr::makeTypeUnary("typeof", parseIsAs());
      expect(TokenKind::RParen);
      return e;
    }
    return Expr::makeTypeUnaryType("typeof", parseType());
  }
  if (match(TokenKind::TypeTag)) {
    if (check(TokenKind::LParen)) {
      expect(TokenKind::LParen);
      std::unique_ptr<Expr> e;
      if (check(TokenKind::TypeName) || check(TokenKind::Fn) ||
          (check(TokenKind::Ident) &&
           (user_type_names_.count(current().value) || isTypeName(current().value))))
        e = Expr::makeTypeUnaryType("type_tag", parseType());
      else
        e = Expr::makeTypeUnary("type_tag", parseIsAs());
      expect(TokenKind::RParen);
      return e;
    }
    return Expr::makeTypeUnaryType("type_tag", parseType());
  }
  if (match(TokenKind::Sizeof)) {
    expect(TokenKind::LParen);
    std::unique_ptr<Expr> e;
    if (check(TokenKind::TypeName) || check(TokenKind::Ident) || check(TokenKind::Fn))
      e = Expr::makeTypeUnaryType("sizeof", parseType());
    else {
      e = Expr::makeTypeUnary("sizeof", parseExpr());
    }
    expect(TokenKind::RParen);
    return e;
  }
  if (match(TokenKind::Alignof)) {
    expect(TokenKind::LParen);
    auto e = Expr::makeTypeUnaryType("alignof", parseType());
    expect(TokenKind::RParen);
    return e;
  }
  if (match(TokenKind::StackAlloc)) {
    expect(TokenKind::Lt);
    TypeDesc elem = parseType();
    expect(TokenKind::Gt);
    expect(TokenKind::LParen);
    auto count = parseExpr();
    expect(TokenKind::RParen);
    auto e = Expr::makeTypeUnary("stackalloc", std::move(count));
    e->type_unary.type_arg = std::move(elem);
    e->type_unary.has_type = true;
    return e;
  }
  if (match(TokenKind::Comptime)) {
    auto inner = parseExpr();
    auto e = std::make_unique<Expr>();
    e->kind = Expr::ComptimeExprK;
    e->comptime_expr.value = std::move(inner);
    return e;
  }
  if (match(TokenKind::Dollar)) {
    expect(TokenKind::LParen);
    auto e = std::make_unique<Expr>();
    e->kind = Expr::MacroSubstExprK;
    e->macro_subst.param = expect(TokenKind::Ident).value;
    expect(TokenKind::RParen);
    return e;
  }
  if (match(TokenKind::Borrow)) {
    expect(TokenKind::LParen);
    auto inner = parseExpr();
    expect(TokenKind::RParen);
    std::vector<std::unique_ptr<Expr>> args;
    args.push_back(std::move(inner));
    return Expr::makeCall("borrow", std::move(args));
  }
  if (match(TokenKind::Move)) {
    expect(TokenKind::LParen);
    auto inner = parseExpr();
    expect(TokenKind::RParen);
    std::vector<std::unique_ptr<Expr>> args;
    args.push_back(std::move(inner));
    return Expr::makeCall("move", std::move(args));
  }
  if (match(TokenKind::Drop)) {
    expect(TokenKind::LParen);
    auto inner = parseExpr();
    expect(TokenKind::RParen);
    std::vector<std::unique_ptr<Expr>> args;
    args.push_back(std::move(inner));
    return Expr::makeCall("drop", std::move(args));
  }
  if (match(TokenKind::Await)) {
    auto inner = parseUnary();
    return Expr::makeAwait(std::move(inner), TypeDesc::prim(FarTypeId::I64));
  }
  if (match(TokenKind::Spawn)) {
    auto call = parsePrimary();
    if (call->kind != Expr::FnCall)
      throw FarError("spawn requires a function call like spawn worker(0, n)");
    return Expr::makeSpawn(std::move(call));
  }
  if (match(TokenKind::TaskKw)) {
    auto call = parsePrimary();
    if (call->kind != Expr::FnCall)
      throw FarError("task requires a function call like task worker(0, n)");
    return Expr::makeSpawn(std::move(call), true);
  }
  if (match(TokenKind::Panic)) {
    expect(TokenKind::LParen);
    auto msg = parseExpr();
    expect(TokenKind::RParen);
    std::vector<std::unique_ptr<Expr>> args;
    args.push_back(std::move(msg));
    return Expr::makeCall("panic", std::move(args));
  }
  if (match(TokenKind::Assert)) {
    expect(TokenKind::LParen);
    auto cond = parseExpr();
    std::vector<std::unique_ptr<Expr>> args;
    args.push_back(std::move(cond));
    if (match(TokenKind::Comma))
      args.push_back(parseExpr());
    expect(TokenKind::RParen);
    return Expr::makeCall("assert", std::move(args));
  }
  if (match(TokenKind::Parallel)) {
    if (!match(TokenKind::LParen)) {
      std::string fn = expect(TokenKind::Ident).value;
      return Expr::makeParallel(std::move(fn));
    }
    if (check(TokenKind::Ident) && pos_ + 1 < tokens_.size() && tokens_[pos_ + 1].kind == TokenKind::RParen) {
      std::string fn = expect(TokenKind::Ident).value;
      expect(TokenKind::RParen);
      return Expr::makeParallel(std::move(fn));
    }
    auto count = parseExpr();
    expect(TokenKind::Comma);
    if (!check(TokenKind::Ident))
      throw FarError("parallel(n, func) requires a function name", current().line, current().col);
    std::string fn = expect(TokenKind::Ident).value;
    expect(TokenKind::RParen);
    return Expr::makeParallel(std::move(fn), std::move(count));
  }
  return parsePostfix(parsePrimary());
}



std::unique_ptr<Expr> Parser::parsePostfix(std::unique_ptr<Expr> left) {

  while (true) {
    if (match(TokenKind::LBracket)) {
      if (check(TokenKind::Colon)) {
        expect(TokenKind::Colon);
        std::unique_ptr<Expr> end;
        if (!check(TokenKind::RBracket))
          end = parseExpr();
        expect(TokenKind::RBracket);
        left = Expr::makeSlice(std::move(left), nullptr, std::move(end));
      } else {
        auto first = parseExpr();
        if (match(TokenKind::Colon)) {
          std::unique_ptr<Expr> end;
          if (!check(TokenKind::RBracket))
            end = parseExpr();
          expect(TokenKind::RBracket);
          left = Expr::makeSlice(std::move(left), std::move(first), std::move(end));
        } else {
          expect(TokenKind::RBracket);
          left = Expr::makeIndex(std::move(left), std::move(first));
        }
      }
      continue;
    }
    if (match(TokenKind::QuestionDot)) {
      std::string member = expect(TokenKind::Ident).value;
      left = Expr::makeBinOp("?.", std::move(left), Expr::makeString(member));
      continue;
    }
    if (match(TokenKind::PlusPlus)) {
      left = Expr::makePostfix(std::move(left), "++");
      continue;
    }
    if (match(TokenKind::MinusMinus)) {
      left = Expr::makePostfix(std::move(left), "--");
      continue;
    }
    if (match(TokenKind::BangQuestion)) {
      left = Expr::makePostfix(std::move(left), "!?");
      continue;
    }
    if (match(TokenKind::Dot)) {
      std::string member;
      if (match(TokenKind::Int))
        member = "." + tokens_[pos_ - 1].value;
      else if (match(TokenKind::Float)) {
        std::string f = tokens_[pos_ - 1].value;
        if (!f.empty() && (f.back() == 'f' || f.back() == 'F'))
          f.pop_back();
        member = "." + f;
      } else if (match(TokenKind::Await))
        member = "await";
      else if (match(TokenKind::Spawn))
        member = "spawn";
      else if (match(TokenKind::Async))
        member = "async";
      else if (match(TokenKind::TaskKw))
        member = "task";
      else if (check(TokenKind::Ident) || check(TokenKind::TypeName))
        member = tokens_[pos_++].value;
      else
        throw FarError("expected member name", current().line, current().col);
      if (match(TokenKind::LParen)) {
        std::vector<std::unique_ptr<Expr>> args;
        if (!check(TokenKind::RParen)) {
          args.push_back(parseExpr());
          while (match(TokenKind::Comma))
            args.push_back(parseExpr());
        }
        expect(TokenKind::RParen);
        left = Expr::makeMethodCall(std::move(left), std::move(member), std::move(args),
                                    TypeDesc::prim(FarTypeId::I64));
      } else {
        left = Expr::makeMember(std::move(left), std::move(member), TypeDesc::prim(FarTypeId::I64));
      }
      continue;
    }
    break;
  }

  return left;

}



std::unique_ptr<Expr> Parser::parsePrimary() {

  std::unique_ptr<Expr> e;

  if (check(TokenKind::Fn) || check(TokenKind::Lambda))
    return parseFnLitExpr(false);

  if (match(TokenKind::Pipe)) {
    FnLit lit;
    if (!check(TokenKind::RParen) && !check(TokenKind::Pipe)) {
      do {
        Param p;
        p.name = expect(TokenKind::Ident).value;
        if (match(TokenKind::Colon))
          p.type = parseType();
        lit.params.push_back(std::move(p));
      } while (match(TokenKind::Comma));
    }
    expect(TokenKind::Pipe);
    lit.expr_body = parseExpr();
    std::vector<TypeDesc> ptypes;
    for (const auto& p : lit.params)
      ptypes.push_back(p.type);
    TypeDesc ret = lit.return_type;
    return Expr::makeFnLit(std::move(lit), TypeDesc::function(std::move(ptypes), ret));
  }

  if (match(TokenKind::Int))

    e = Expr::makeInt(std::stoll(tokens_[pos_ - 1].value));

  else if (match(TokenKind::Float)) {

    std::string text = tokens_[pos_ - 1].value;

    bool is_float = false;

    if (!text.empty() && (text.back() == 'f' || text.back() == 'F')) {

      is_float = true;

      text.pop_back();

    }

    e = Expr::makeFloat(std::stod(text), is_float);

  } else if (match(TokenKind::String))

    e = Expr::makeString(tokens_[pos_ - 1].value);

  else if (match(TokenKind::Char))

    e = Expr::makeChar(static_cast<uint16_t>(std::stoul(tokens_[pos_ - 1].value)));

  else if (match(TokenKind::InterpString))

    e = parseInterpString(tokens_[pos_ - 1]);

  else if (match(TokenKind::True)) {

    e = Expr::makeInt(1);

    e->type = TypeDesc::prim(FarTypeId::Bool);

  } else if (match(TokenKind::False)) {

    e = Expr::makeInt(0);

    e->type = TypeDesc::prim(FarTypeId::Bool);

  } else if (match(TokenKind::TypeName)) {

    std::string type_name = tokens_[pos_ - 1].value;

    if (check(TokenKind::LParen) && lookupErrConstructor(type_name)) {
      expect(TokenKind::LParen);
      auto call_args = parseCallArgs();
      expect(TokenKind::RParen);
      e = Expr::makeCallArgs(type_name, std::move(call_args));
    } else {

    TypeDesc tid = finishParseType(type_name);

    if (match(TokenKind::LParen)) {

      if (const ConstructorInfo* ctor = lookupConstructor(type_name)) {

        std::vector<std::unique_ptr<Expr>> args;

        if (!check(TokenKind::RParen)) {

          args.push_back(parseExpr());

          while (match(TokenKind::Comma))

            args.push_back(parseExpr());

        }

        expect(TokenKind::RParen);

        e = Expr::makeCall(ctor->name, std::move(args), TypeDesc::prim(ctor->ret));

      } else if (lookupCollConstructor(type_name)) {

        std::vector<std::unique_ptr<Expr>> args;

        if (!check(TokenKind::RParen)) {

          args.push_back(parseExpr());

          while (match(TokenKind::Comma))

            args.push_back(parseExpr());

        }

        expect(TokenKind::RParen);

        e = Expr::makeCall(type_name, std::move(args), tid);

      } else if (user_type_names_.count(type_name)) {
        auto call_args = parseCallArgs();
        expect(TokenKind::RParen);
        e = Expr::makeCallArgs(type_name, std::move(call_args), tid, tid.args);
      } else if (lookupMemConstructor(type_name)) {

        std::vector<TypeDesc> targs;
        if (!tid.args.empty())
          targs.push_back(tid.args[0]);
        auto call_args = parseCallArgs();
        expect(TokenKind::RParen);
        e = Expr::makeCallArgs(type_name, std::move(call_args), tid, std::move(targs));

      } else if (lookupConcConstructor(type_name)) {

        std::vector<TypeDesc> targs;
        if (!tid.args.empty())
          targs.push_back(tid.args[0]);
        auto call_args = parseCallArgs();
        expect(TokenKind::RParen);
        e = Expr::makeCallArgs(type_name, std::move(call_args), tid, std::move(targs));

      } else {

        auto arg = parseExpr();

        expect(TokenKind::RParen);

        if (const BuiltinInfo* builtin = lookupBuiltin(type_name)) {
          std::vector<std::unique_ptr<Expr>> args;
          args.push_back(std::move(arg));
          e = Expr::makeCall(type_name, std::move(args), TypeDesc::prim(builtin->ret));
        } else {
          e = Expr::makeCast(tid, std::move(arg));
        }

      }

    } else if (check(TokenKind::Dot)) {
      size_t saved = pos_;
      expect(TokenKind::Dot);
      if (check(TokenKind::Ident)) {
        const std::string member = current().value;
        if (member == "min") {
          expect(TokenKind::Ident);
          e = Expr::makeTypeConst(primitiveOf(tid), false);
        } else if (member == "max") {
          expect(TokenKind::Ident);
          e = Expr::makeTypeConst(primitiveOf(tid), true);
        } else {
          pos_ = saved;
          e = Expr::makeVar(type_name);
        }
      } else {
        pos_ = saved;
        e = Expr::makeVar(type_name);
      }

    } else {

      e = Expr::makeVar(type_name);

    }

    }

  } else if (match(TokenKind::LBrace)) {

    std::vector<DictEntry> entries;

    if (!check(TokenKind::RBrace)) {

      do {

        DictEntry entry;
        entry.key = parseExpr();
        expect(TokenKind::Colon);
        entry.value = parseExpr();
        entries.push_back(std::move(entry));

      } while (match(TokenKind::Comma));

    }

    expect(TokenKind::RBrace);

    e = Expr::makeDictLit(std::move(entries));

  } else if (match(TokenKind::LBracket)) {

    std::vector<std::unique_ptr<Expr>> elems;

    if (!check(TokenKind::RBracket)) {

      elems.push_back(parseExpr());

      while (match(TokenKind::Comma))

        elems.push_back(parseExpr());

    }

    expect(TokenKind::RBracket);

    e = Expr::makeArrayLit(std::move(elems));

  } else if (check(TokenKind::Ident) && pos_ + 1 < tokens_.size() &&
             tokens_[pos_ + 1].kind == TokenKind::FatArrow) {
    FnLit lit;
    Param p;
    p.name = expect(TokenKind::Ident).value;
    lit.params.push_back(std::move(p));
    expect(TokenKind::FatArrow);
    lit.expr_body = parsePipeline();
    std::vector<TypeDesc> ptypes;
    for (const auto& ap : lit.params)
      ptypes.push_back(ap.type);
    return Expr::makeFnLit(std::move(lit), TypeDesc::function(std::move(ptypes), lit.return_type));
  } else if (check(TokenKind::Ident) && enum_type_names_.count(current().value) && pos_ + 1 < tokens_.size() &&
             tokens_[pos_ + 1].kind == TokenKind::Dot) {
    std::string en = expect(TokenKind::Ident).value;
    expect(TokenKind::Dot);
    std::string variant = expect(TokenKind::Ident).value;
    e = Expr::makeEnumVariant(en, variant, 0);
  } else if (check(TokenKind::Ident) && union_type_names_.count(current().value) && pos_ + 1 < tokens_.size() &&
             tokens_[pos_ + 1].kind == TokenKind::Dot) {
    std::string un = expect(TokenKind::Ident).value;
    expect(TokenKind::Dot);
    std::string variant = expect(TokenKind::Ident).value;
    std::vector<std::unique_ptr<Expr>> args;
    if (match(TokenKind::LParen)) {
      for (auto& ca : parseCallArgs())
        args.push_back(std::move(ca.value));
      expect(TokenKind::RParen);
    }
    e = Expr::makeUnionVariant(un, variant, 0, std::move(args));
  } else if (match(TokenKind::Ident) || match(TokenKind::TypeName)) {

    const Token name_tok = tokens_[pos_ - 1];
    std::string name = name_tok.value;
    if (macro_names_.count(name) && match(TokenKind::Bang)) {
      expect(TokenKind::LParen);
      std::vector<std::unique_ptr<Expr>> args;
      if (!check(TokenKind::RParen)) {
        do {
          args.push_back(parseExpr());
        } while (match(TokenKind::Comma));
      }
      expect(TokenKind::RParen);
      auto me = std::make_unique<Expr>();
      me->kind = Expr::MacroInvokeExprK;
      me->macro_invoke.name = name;
      me->macro_invoke.args = std::move(args);
      return me;
    }
    std::vector<TypeDesc> targs;
    tryParseGenericTypeArgs(targs);

    if (match(TokenKind::LParen)) {
      auto args = parseCallArgs();
      expect(TokenKind::RParen);
      TypeDesc ret_ty = user_type_names_.count(name) ? TypeDesc::user(name, targs) : TypeDesc::prim(FarTypeId::I64);
      e = Expr::makeCallArgs(std::move(name), std::move(args), std::move(ret_ty), std::move(targs));
      e->line = name_tok.line;
      e->col = name_tok.col;
    } else if (user_type_names_.count(name)) {
      e = Expr::makeVar(name);
      e->line = name_tok.line;
      e->col = name_tok.col;
    } else {

      e = Expr::makeVar(std::move(name));
      e->line = name_tok.line;
      e->col = name_tok.col;

    }

  } else if (match(TokenKind::LParen)) {

    if (check(TokenKind::RParen)) {

      expect(TokenKind::RParen);

      e = Expr::makeTupleLit({});

    } else {

      e = parseExpr();

      if (match(TokenKind::Comma)) {

        std::vector<std::unique_ptr<Expr>> elems;

        elems.push_back(std::move(e));

        do {

          elems.push_back(parseExpr());

        } while (match(TokenKind::Comma));

        expect(TokenKind::RParen);

        e = Expr::makeTupleLit(std::move(elems));

      } else {

        expect(TokenKind::RParen);

      }

    }

  } else {

    const Token& tok = current();

    throw FarError(std::string("unexpected token ") + tokenName(tok.kind), tok.line, tok.col);

  }

  return parsePostfix(std::move(e));

}



static std::string readFileText(const std::string& path) {

  std::ifstream in(path, std::ios::binary);

  if (!in)

    throw FarError("cannot open import: " + path);

  std::ostringstream ss;

  ss << in.rdbuf();

  return ss.str();

}



Program parseProgram(const std::string& source, bool require_main) {

  Lexer lexer(source);

  Parser parser(lexer.tokenize());

  Program program = parser.parse();

  if (require_main) {

    bool has_main = false;

    for (const auto& fn : program.functions) {

      if (fn.name == "main") {

        has_main = true;

        break;

      }

    }

    if (!has_main)

      throw FarError("program must define fun main()");

  }

  return program;

}



TypeDesc Parser::finishParseType(const std::string& name) {
  if (name == "fn") {
    expect(TokenKind::LParen);
    std::vector<TypeDesc> params;
    if (!check(TokenKind::RParen)) {
      params.push_back(parseType());
      while (match(TokenKind::Comma))
        params.push_back(parseType());
    }
    expect(TokenKind::RParen);
    TypeDesc ret = TypeDesc::prim(FarTypeId::I64);
    if (match(TokenKind::Arrow))
      ret = parseType();
    return TypeDesc::function(std::move(params), ret);
  }
  if (name == "List") {
    expect(TokenKind::Lt);
    TypeDesc elem = parseType();
    expect(TokenKind::Gt);
    TypeDesc base = TypeDesc::list(elem);
    while (match(TokenKind::LBracket) && match(TokenKind::RBracket))
      base = TypeDesc::array(base);
    if (match(TokenKind::Question)) {
    }
    return base;
  }
  if (name == "Dict") {
    expect(TokenKind::Lt);
    TypeDesc key = parseType();
    expect(TokenKind::Comma);
    TypeDesc val = parseType();
    expect(TokenKind::Gt);
    return TypeDesc::dict(key, val);
  }
  if (name == "ptr" || name == "ref") {
    if (check(TokenKind::Lt)) {
      expect(TokenKind::Lt);
      TypeDesc inner = parseType();
      expect(TokenKind::Gt);
      return name == "ptr" ? TypeDesc::pointer(inner) : TypeDesc::borrowRef(inner);
    }
    return TypeDesc::prim(name == "ptr" ? FarTypeId::Ptr : FarTypeId::Ref);
  }
  if (lookupConstructor(name)) {
    FarTypeId agg = parseTypeName(name);
    if (agg != FarTypeId::Void)
      return TypeDesc::prim(agg);
  }
  if (user_type_names_.count(name)) {
    TypeDesc td = TypeDesc::user(name);
    if (check(TokenKind::Lt)) {
      expect(TokenKind::Lt);
      if (!check(TokenKind::Gt)) {
        do {
          td.args.push_back(parseType());
        } while (match(TokenKind::Comma));
      }
      expect(TokenKind::Gt);
    }
    return td;
  }
  if (name == "Box" || name == "Rc" || name == "Pool") {
    expect(TokenKind::Lt);
    TypeDesc elem = parseType();
    expect(TokenKind::Gt);
    if (name == "Box")
      return TypeDesc::box(elem);
    if (name == "Rc")
      return TypeDesc::rc(elem);
    return TypeDesc::memPool(elem);
  }
  if (name == "Arena")
    return TypeDesc::arena();
  if (name == "Channel") {
    expect(TokenKind::Lt);
    TypeDesc elem = parseType();
    expect(TokenKind::Gt);
    return TypeDesc::channel(elem);
  }
  if (name == "Mutex")
    return TypeDesc::mutex();
  if (name == "Semaphore")
    return TypeDesc::semaphore();
  if (name == "Atomic") {
    expect(TokenKind::Lt);
    TypeDesc elem = parseType();
    expect(TokenKind::Gt);
    return TypeDesc::atomic(elem);
  }
  if (name == "ThreadPool")
    return TypeDesc::threadPool();
  if (name == "LockFreeQueue") {
    expect(TokenKind::Lt);
    TypeDesc elem = parseType();
    expect(TokenKind::Gt);
    return TypeDesc::lockFreeQueue(elem);
  }
  if (name == "Task")
    return TypeDesc::task();
  if (name == "Set" || name == "Queue" || name == "Stack" || name == "LinkedList" || name == "Span" ||
      name == "Slice") {
    expect(TokenKind::Lt);
    TypeDesc elem = parseType();
    expect(TokenKind::Gt);
    TypeDesc base;
    if (name == "Set")
      base = TypeDesc::set(elem);
    else if (name == "Queue")
      base = TypeDesc::queue(elem);
    else if (name == "Stack")
      base = TypeDesc::stack(elem);
    else if (name == "LinkedList")
      base = TypeDesc::linkedList(elem);
    else if (name == "Span")
      base = TypeDesc::span(elem);
    else
      base = TypeDesc::slice(elem);
    return base;
  }
  if (name == "FixedArray") {
    expect(TokenKind::Lt);
    TypeDesc elem = parseType();
    expect(TokenKind::Comma);
    TypeDesc td = TypeDesc::fixedArray(elem, 0);
    if (match(TokenKind::Int)) {
      td.const_n = std::stoll(tokens_[pos_ - 1].value);
    } else if (match(TokenKind::Comptime)) {
      td.comptime_size = std::shared_ptr<Expr>(parseExpr().release());
    } else {
      throw FarError("FixedArray requires compile-time size", current().line, current().col);
    }
    expect(TokenKind::Gt);
    return td;
  }
  if (name == "Tuple") {
    expect(TokenKind::Lt);
    std::vector<TypeDesc> fields;
    fields.push_back(parseType());
    while (match(TokenKind::Comma))
      fields.push_back(parseType());
    expect(TokenKind::Gt);
    return TypeDesc::tuple(std::move(fields));
  }
  if (name == "Range")
    return TypeDesc::range();
  if (name == "Option") {
    expect(TokenKind::Lt);
    TypeDesc elem = parseType();
    expect(TokenKind::Gt);
    return TypeDesc::optional(elem);
  }
  if (name == "Result") {
    expect(TokenKind::Lt);
    TypeDesc ok = parseType();
    expect(TokenKind::Comma);
    TypeDesc err = parseType();
    expect(TokenKind::Gt);
    return TypeDesc::result(std::move(ok), std::move(err));
  }
  if (name == "arr")
    return TypeDesc::array(TypeDesc::prim(FarTypeId::I64));
  if (name == "Self" && !current_type_name_.empty()) {
    TypeDesc td = TypeDesc::user(current_type_name_);
    for (const auto& tp : current_type_params_)
      td.args.push_back(TypeDesc::typeVar(tp.name));
    return td;
  }
  TypeDesc base = TypeDesc::prim(parseTypeName(name));
  while (match(TokenKind::LBracket) && match(TokenKind::RBracket))
    base = TypeDesc::array(base);
  if (match(TokenKind::Question))
    return TypeDesc::optional(base);
  return base;
}

bool Parser::tryParseGenericTypeArgs(std::vector<TypeDesc>& out) {
  if (!check(TokenKind::Lt))
    return false;
  size_t save = pos_;
  match(TokenKind::Lt);
  try {
    out.push_back(parseType());
    while (match(TokenKind::Comma))
      out.push_back(parseType());
    if (!match(TokenKind::Gt))
      throw FarError("expected >", current().line, current().col);
    return true;
  } catch (...) {
    pos_ = save;
    out.clear();
    return false;
  }
}

TypeDesc Parser::parseType() {
  if (match(TokenKind::Fn))
    return finishParseType("fn");
  if (match(TokenKind::TypeName))
    return finishParseType(tokens_[pos_ - 1].value);
  if (match(TokenKind::Ident)) {
    std::string n = tokens_[pos_ - 1].value;
    if (user_type_names_.count(n) || n == "Self")
      return finishParseType(n);
    if (isTypeName(n))
      return finishParseType(n);
    return TypeDesc::typeVar(n);
  }
  const Token& tok = current();
  throw FarError(std::string("expected type name, got ") + tokenName(tok.kind), tok.line, tok.col);
}



std::unique_ptr<Expr> Parser::parseExprFromSource(const std::string& source) {

  Lexer lexer(source);

  Parser parser(lexer.tokenize());

  return parser.parseExpr();

}



std::unique_ptr<Expr> Parser::parseInterpString(const Token& tok) {

  std::unique_ptr<Expr> result;

  auto append = [&](std::unique_ptr<Expr> part) {

    if (!part)

      return;

    if (!result)

      result = std::move(part);

    else

      result = Expr::makeBinOp("+", std::move(result), std::move(part));

  };

  for (size_t i = 0; i < tok.interp_exprs.size(); ++i) {

    if (!tok.interp_texts[i].empty())

      append(Expr::makeString(tok.interp_texts[i]));

    append(parseExprFromSource(tok.interp_exprs[i]));

  }

  if (!tok.interp_texts.empty() && !tok.interp_texts.back().empty())

    append(Expr::makeString(tok.interp_texts.back()));

  if (!result)

    return Expr::makeString("");

  return result;

}



Program parseSource(const std::string& source) {

  return parseProgram(source, true);

}



}  // namespace far

