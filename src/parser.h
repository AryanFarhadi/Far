#pragma once

#include "ast.h"

#include "lexer.h"

#include <unordered_set>
#include <vector>



namespace far {



class Parser {

public:

  explicit Parser(std::vector<Token> tokens);

  Program parse();



private:

  std::vector<Token> tokens_;

  size_t pos_ = 0;

  std::unordered_set<std::string> user_type_names_;
  std::unordered_set<std::string> enum_type_names_;
  std::unordered_set<std::string> union_type_names_;
  std::string current_type_name_;
  std::vector<TypeParam> current_type_params_;
  std::string current_namespace_;
  std::unordered_set<std::string> macro_names_;
  int parse_depth_ = 0;
  bool pending_type_closing_gt_ = false;
  bool saw_main_ = false;

  void pushParseDepth(int line, int col);
  bool matchTypeClosingGt();
  void expectTypeClosingGt();
  void popParseDepth();

  Visibility parseVisibility();
  MacroDef parseMacroDecl();
  std::string parseImportSegment();
  std::string parseQualifiedPath();
  std::string parseQualifiedSymbol();
  std::string qualifyName(const std::string& name);
  ImportDecl parseImportDecl();
  ImportDecl parseFromImportDecl();
  static std::string defaultImportAlias(const std::string& path);
  void parseExportList(std::vector<std::string>& out);
  void parseNamespaceDecl(Program& program);
  void parseTopLevelDecl(Program& program);



  const Token& current() const { return tokens_[pos_]; }

  bool check(TokenKind k) const { return current().kind == k; }

  bool match(TokenKind k);

  bool matchAny(std::initializer_list<TokenKind> kinds);

  const Token& expect(TokenKind k);

  void consumeOptionalSemi();

  bool isConstexprLetStart() const;
  std::unique_ptr<Stmt> parseConstexprLet();



  std::vector<Attribute> parseAttributes();
  UserTypeDef parseUserTypeDecl(UserTypeKind kind);
  ExtensionDef parseExtensionDecl();
  UserMethod parseUserMethod(bool require_body);
  UserMethod parseConstructorDecl();
  PropertyDef parsePropertyDecl();
  OperatorDef parseOperatorDecl();
  void parseEnumBody(UserTypeDef& td);
  void parseUnionBody(UserTypeDef& td);
  void parseTypeBody(UserTypeDef& td, bool allow_bodies);
  std::vector<TypeParam> parseTypeParams();
  std::string parseTypeIdent();
  std::unique_ptr<Pattern> parsePattern();
  std::unique_ptr<Stmt> parseMatchStmt();
  std::unique_ptr<Stmt> parseSwitchStmt();
  std::vector<std::unique_ptr<Stmt>> parseMatchArmBody();

  Function parseFunction();
  Param parseParam();
  std::vector<CallArg> parseCallArgs();
  std::unique_ptr<Expr> parseFnLitExpr(bool pipe_form = false);

  std::vector<std::unique_ptr<Stmt>> parseBlock();

  std::unique_ptr<Stmt> parseStmt();

  std::unique_ptr<Stmt> parseIfStmt();

  std::unique_ptr<Stmt> parseForStmt();

  std::unique_ptr<Stmt> parseTryStmt();

  std::unique_ptr<Expr> parseExpr(bool allow_range_continuation = false);
  std::unique_ptr<Expr> parseAssign(bool allow_range_continuation = false);
  std::unique_ptr<Expr> parsePipeline();
  std::unique_ptr<Expr> parseNullCoalesce();
  std::unique_ptr<Expr> parseTernary();
  std::unique_ptr<Expr> parseLogicalOr();
  std::unique_ptr<Expr> parseLogicalAnd();
  std::unique_ptr<Expr> parseIdentity();
  std::unique_ptr<Expr> parseComparison();
  std::unique_ptr<Expr> parseMembership();
  std::unique_ptr<Expr> parseIsAs();
  std::unique_ptr<Expr> parseBitOr();
  std::unique_ptr<Expr> parseBitXor();
  std::unique_ptr<Expr> parseBitAnd();
  std::unique_ptr<Expr> parseShift();
  std::unique_ptr<Expr> parseTerm();
  std::unique_ptr<Expr> parseFactor();
  std::unique_ptr<Expr> parsePower();
  std::unique_ptr<Expr> parseUnary();

  std::unique_ptr<Expr> parsePrimary();

  std::unique_ptr<Expr> parseInterpString(const Token& tok);

  std::unique_ptr<Expr> parseExprFromSource(const std::string& source);

  std::unique_ptr<Expr> parsePostfix(std::unique_ptr<Expr> left);


  TypeDesc parseType();
  TypeDesc finishParseType(const std::string& name);
  bool tryParseGenericTypeArgs(std::vector<TypeDesc>& out);

};



Program parseProgram(const std::string& source, bool require_main = true);
Program parseSource(const std::string& source);



}  // namespace far

