#pragma once

#include <string>

#include <unordered_map>

#include <vector>



namespace far {



enum class TokenKind {

  Ident, Int, Float, String, Char, InterpString, TypeName,

  Def, Fn, Async, Await, Yield, Lambda, Coroutine, If, Else, While, For, From, Return, Break, Continue,
  Import, Export, Package, Module, Namespace,
  Public, Private, Protected, Internal,
  Constexpr, Consteval, Macro, Comptime, Codegen,
  Is, As, In, Not, Typeof, TypeTag, Sizeof, Alignof, Reflect,
  Struct, Class, Record, Interface, Enum, Flags, Trait, Mixin, Extension, Operator, Prop,
  With, Implements, Get, Set, Self, At,
  Unsafe, Defer, Borrow, Move, Drop, StackAlloc,

  Spawn, Parallel, TaskKw, Actor, Static,
  Try, Catch, Finally, Throw, Panic, Assert, Exception,
  Match, Switch, Case, Default, Union,
  True, False,

  LParen, RParen, LBrace, RBrace, LBracket, RBracket, Comma, Semi, Arrow, FatArrow,

  Plus, Minus, Star, StarStar, Slash, FloorDiv, Percent, Tilde,
  Amp, Caret, Pipe, AmpAmp, PipePipe, PipeGt,
  PlusPlus, MinusMinus,
  LShift, RShift,

  Colon, Eq, EqEq, EqEqEq, Bang, BangEq, BangEqEq, Ellipsis, Dollar,

  PlusEq, MinusEq, StarEq, SlashEq, PercentEq, StarStarEq, FloorDivEq,
  AmpEq, PipeEq, CaretEq, LShiftEq, RShiftEq, QuestionEq,

  Lt, Gt, Lte, Gte, And, Or, Dot, DotDot, DotDotLt, Question,
  QuestionQuestion, QuestionDot, BangQuestion,

  Eof

};



struct Token {

  TokenKind kind;

  std::string value;

  int line;

  int col;

  // For InterpString: texts.size() == exprs.size() + 1
  std::vector<std::string> interp_texts;
  std::vector<std::string> interp_exprs;

};



class Lexer {

public:

  explicit Lexer(std::string source);

  std::vector<Token> tokenize();



private:

  std::string source_;

  size_t pos_ = 0;

  int line_ = 1;

  int col_ = 1;



  char peek(size_t offset = 0) const;

  char advance();

  void skipWhitespaceAndComments();

  Token readIdent(int line, int col);

  Token readInt(int line, int col);

  Token readFloat(int line, int col);

  Token readString(int line, int col);

  Token readChar(int line, int col);

  Token readInterpString(int line, int col);

  void appendUtf8Char(std::string& value, int line, int col);

  static char decodeEscape(char esc, int line, int col);

};



}  // namespace far

