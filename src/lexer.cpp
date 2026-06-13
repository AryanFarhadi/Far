#include "lexer.h"

#include "error.h"

#include <cstdint>

#include <cctype>



namespace far {



Lexer::Lexer(std::string source) : source_(std::move(source)) {}



char Lexer::peek(size_t offset) const {

  size_t idx = pos_ + offset;

  return idx < source_.size() ? source_[idx] : '\0';

}



char Lexer::advance() {

  char ch = source_[pos_++];

  if (ch == '\n') {

    line_++;

    col_ = 1;

  } else {

    col_++;

  }

  return ch;

}



void Lexer::skipWhitespaceAndComments() {

  while (pos_ < source_.size()) {

    char ch = source_[pos_];

    if (ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n') {

      advance();

    } else if (ch == '#') {

      while (pos_ < source_.size() && source_[pos_] != '\n')

        advance();

    } else {

      break;

    }

  }

}



Token Lexer::readIdent(int line, int col) {

  size_t start = pos_;

  while (pos_ < source_.size()) {

    char ch = source_[pos_];

    if (std::isalnum(static_cast<unsigned char>(ch)) || ch == '_')

      advance();

    else

      break;

  }

  std::string text = source_.substr(start, pos_ - start);

  static const std::unordered_map<std::string, TokenKind> keywords = {

      {"fun", TokenKind::Def},       {"def", TokenKind::Def},
      {"fn", TokenKind::Fn},
      {"async", TokenKind::Async},   {"await", TokenKind::Await},
      {"yield", TokenKind::Yield},   {"lambda", TokenKind::Lambda},
      {"coroutine", TokenKind::Coroutine},
      {"if", TokenKind::If},

      {"else", TokenKind::Else},     {"while", TokenKind::While},

      {"for", TokenKind::For},       {"from", TokenKind::From},
      {"return", TokenKind::Return},

      {"break", TokenKind::Break},   {"continue", TokenKind::Continue},

      {"import", TokenKind::Import},       {"export", TokenKind::Export},
      {"package", TokenKind::Package},     {"module", TokenKind::Module},
      {"namespace", TokenKind::Namespace},
      {"public", TokenKind::Public},       {"private", TokenKind::Private},
      {"protected", TokenKind::Protected}, {"internal", TokenKind::Internal},
      {"constexpr", TokenKind::Constexpr}, {"consteval", TokenKind::Consteval},
      {"macro", TokenKind::Macro},         {"comptime", TokenKind::Comptime},
      {"codegen", TokenKind::Codegen},
      {"spawn", TokenKind::Spawn},

      {"parallel", TokenKind::Parallel}, {"static", TokenKind::Static}, {"task", TokenKind::TaskKw},
      {"actor", TokenKind::Actor},
      {"try", TokenKind::Try},         {"catch", TokenKind::Catch},
      {"finally", TokenKind::Finally}, {"throw", TokenKind::Throw},
      {"panic", TokenKind::Panic},     {"assert", TokenKind::Assert},
      {"exception", TokenKind::Exception},
      {"match", TokenKind::Match},     {"switch", TokenKind::Switch},
      {"case", TokenKind::Case},       {"default", TokenKind::Default},
      {"union", TokenKind::Union},
      {"true", TokenKind::True},

      {"false", TokenKind::False},   {"and", TokenKind::And},

      {"or", TokenKind::Or},
      {"is", TokenKind::Is},       {"as", TokenKind::As},
      {"in", TokenKind::In},     {"not", TokenKind::Not},
      {"typeof", TokenKind::Typeof}, {"type_tag", TokenKind::TypeTag}, {"sizeof", TokenKind::Sizeof},
      {"alignof", TokenKind::Alignof},
      {"reflect", TokenKind::Reflect},
      {"struct", TokenKind::Struct},   {"class", TokenKind::Class},
      {"record", TokenKind::Record},   {"interface", TokenKind::Interface},
      {"enum", TokenKind::Enum},       {"flags", TokenKind::Flags},
      {"trait", TokenKind::Trait},     {"mixin", TokenKind::Mixin},
      {"extension", TokenKind::Extension}, {"operator", TokenKind::Operator},
      {"prop", TokenKind::Prop},       {"with", TokenKind::With},
      {"implements", TokenKind::Implements},
      {"unsafe", TokenKind::Unsafe},   {"defer", TokenKind::Defer},
      {"borrow", TokenKind::Borrow},   {"move", TokenKind::Move},
      {"drop", TokenKind::Drop},       {"stackalloc", TokenKind::StackAlloc},
      {"Box", TokenKind::TypeName},    {"Rc", TokenKind::TypeName},
      {"Arena", TokenKind::TypeName},  {"Pool", TokenKind::TypeName},
      {"Channel", TokenKind::TypeName}, {"Mutex", TokenKind::TypeName},
      {"Semaphore", TokenKind::TypeName}, {"Atomic", TokenKind::TypeName},
      {"ThreadPool", TokenKind::TypeName}, {"LockFreeQueue", TokenKind::TypeName},
      {"Task", TokenKind::TypeName},
      {"Option", TokenKind::TypeName}, {"Result", TokenKind::TypeName},
      {"Ok", TokenKind::TypeName},     {"Err", TokenKind::TypeName},
      {"Some", TokenKind::TypeName},   {"None", TokenKind::TypeName},
      {"i8", TokenKind::TypeName},    {"i16", TokenKind::TypeName},
      {"i32", TokenKind::TypeName},   {"i64", TokenKind::TypeName},
      {"i128", TokenKind::TypeName},  {"u8", TokenKind::TypeName},
      {"u16", TokenKind::TypeName},   {"u32", TokenKind::TypeName},
      {"u64", TokenKind::TypeName},   {"u128", TokenKind::TypeName},
      {"sbyte", TokenKind::TypeName}, {"short", TokenKind::TypeName},
      {"int", TokenKind::TypeName},   {"long", TokenKind::TypeName},
      {"longlong", TokenKind::TypeName}, {"int128", TokenKind::TypeName},
      {"byte", TokenKind::TypeName},  {"ushort", TokenKind::TypeName},
      {"uint", TokenKind::TypeName},  {"ulong", TokenKind::TypeName},
      {"ulonglong", TokenKind::TypeName}, {"uint128", TokenKind::TypeName},
      {"half", TokenKind::TypeName},  {"float", TokenKind::TypeName},
      {"double", TokenKind::TypeName}, {"quad", TokenKind::TypeName},
      {"float128", TokenKind::TypeName},
      {"f16", TokenKind::TypeName},   {"f32", TokenKind::TypeName},
      {"f64", TokenKind::TypeName},   {"f128", TokenKind::TypeName},
      {"bool", TokenKind::TypeName},  {"char", TokenKind::TypeName},
      {"string", TokenKind::TypeName}, {"str", TokenKind::TypeName},
      {"raw_string", TokenKind::TypeName},
      {"ptr", TokenKind::TypeName},   {"pointer", TokenKind::TypeName},
      {"ref", TokenKind::TypeName},
      {"any", TokenKind::TypeName},   {"void", TokenKind::TypeName},
      {"arr", TokenKind::TypeName},
      {"List", TokenKind::TypeName},      {"FixedArray", TokenKind::TypeName},
      {"Dict", TokenKind::TypeName},      {"Set", TokenKind::TypeName},
      {"Queue", TokenKind::TypeName},     {"Stack", TokenKind::TypeName},
      {"LinkedList", TokenKind::TypeName},{"Span", TokenKind::TypeName},
      {"Slice", TokenKind::TypeName},     {"Tuple", TokenKind::TypeName},
      {"Range", TokenKind::TypeName},
      {"vec2", TokenKind::TypeName},  {"vec3", TokenKind::TypeName},  {"vec4", TokenKind::TypeName},
      {"point", TokenKind::TypeName}, {"quaternion", TokenKind::TypeName},
      {"fvec2", TokenKind::TypeName}, {"fvec3", TokenKind::TypeName}, {"fvec4", TokenKind::TypeName},
      {"dvec2", TokenKind::TypeName}, {"dvec3", TokenKind::TypeName}, {"dvec4", TokenKind::TypeName},
      {"ivec2", TokenKind::TypeName}, {"ivec3", TokenKind::TypeName}, {"ivec4", TokenKind::TypeName},
      {"mat2", TokenKind::TypeName},  {"mat3", TokenKind::TypeName},  {"mat4", TokenKind::TypeName},
      {"dmat2", TokenKind::TypeName}, {"dmat3", TokenKind::TypeName}, {"dmat4", TokenKind::TypeName},
      {"quat", TokenKind::TypeName},  {"dquat", TokenKind::TypeName},
      {"color", TokenKind::TypeName}, {"color32", TokenKind::TypeName},
      {"transform", TokenKind::TypeName},
      {"rect", TokenKind::TypeName},
      {"bounds", TokenKind::TypeName},
      {"fpoint", TokenKind::TypeName}, {"dpoint", TokenKind::TypeName},
      {"frect", TokenKind::TypeName}, {"drect", TokenKind::TypeName}};

  auto it = keywords.find(text);

  TokenKind kind = it != keywords.end() ? it->second : TokenKind::Ident;

  return {kind, text, line, col};

}



Token Lexer::readInt(int line, int col) {

  size_t start = pos_;

  while (pos_ < source_.size() && std::isdigit(static_cast<unsigned char>(source_[pos_])))

    advance();

  if (peek() == '.') {

    size_t save = pos_;

    advance();

    if (std::isdigit(static_cast<unsigned char>(peek()))) {

      while (pos_ < source_.size() &&

             (std::isdigit(static_cast<unsigned char>(source_[pos_])) || source_[pos_] == '.'))

        advance();

      if (peek() == 'f' || peek() == 'F')

        advance();

      return {TokenKind::Float, source_.substr(start, pos_ - start), line, col};

    }

    pos_ = save;

  }

  return {TokenKind::Int, source_.substr(start, pos_ - start), line, col};

}



Token Lexer::readFloat(int line, int col) {

  size_t start = pos_;

  if (peek() == '.')

    advance();

  while (pos_ < source_.size() &&

         (std::isdigit(static_cast<unsigned char>(source_[pos_])) || source_[pos_] == '.'))

    advance();

  if (peek() == 'f' || peek() == 'F')

    advance();

  return {TokenKind::Float, source_.substr(start, pos_ - start), line, col};

}



char Lexer::decodeEscape(char esc, int line, int col) {
  switch (esc) {
    case 'n': return '\n';
    case 't': return '\t';
    case 'r': return '\r';
    case '0': return '\0';
    case '\\': return '\\';
    case '"': return '"';
    case '\'': return '\'';
    default:
      throw FarError(std::string("invalid escape sequence '\\") + esc + "'", line, col);
  }
}

Token Lexer::readString(int line, int col) {
  advance(); // opening "
  std::string value;
  while (pos_ < source_.size()) {
    char ch = source_[pos_];
    if (ch == '"') {
      advance();
      return {TokenKind::String, value, line, col};
    }
    if (ch == '\\') {
      advance();
      if (pos_ >= source_.size())
        throw FarError("unterminated string", line, col);
      value += decodeEscape(advance(), line, col);
      continue;
    }
    value += advance();
  }
  throw FarError("unterminated string", line, col);
}

Token Lexer::readChar(int line, int col) {
  advance(); // opening '
  if (pos_ >= source_.size())
    throw FarError("unterminated character literal", line, col);
  uint16_t value = 0;
  if (peek() == '\'')
    throw FarError("empty character literal", line, col);
  if (peek() == '\\') {
    advance();
    if (pos_ >= source_.size())
      throw FarError("unterminated character literal", line, col);
    value = static_cast<unsigned char>(decodeEscape(advance(), line, col));
  } else {
    value = static_cast<unsigned char>(advance());
    if (peek() != '\'')
      throw FarError("character literal must contain exactly one character", line, col);
  }
  if (peek() != '\'')
    throw FarError("unterminated character literal", line, col);
  advance(); // closing '
  return {TokenKind::Char, std::to_string(value), line, col};
}

Token Lexer::readInterpString(int line, int col) {
  advance(); // $
  advance(); // opening "
  Token tok;
  tok.kind = TokenKind::InterpString;
  tok.line = line;
  tok.col = col;
  std::string current;
  while (pos_ < source_.size()) {
    char ch = peek();
    if (ch == '"') {
      advance();
      tok.interp_texts.push_back(current);
      return tok;
    }
    if (ch == '{') {
      tok.interp_texts.push_back(current);
      current.clear();
      advance(); // {
      std::string expr;
      int depth = 1;
      while (pos_ < source_.size() && depth > 0) {
        char c = peek();
        if (c == '{') {
          depth++;
          expr += advance();
        } else if (c == '}') {
          depth--;
          if (depth > 0)
            expr += advance();
          else
            advance();
        } else if (c == '"') {
          throw FarError("unexpected '\"' inside interpolation expression", line, col);
        } else {
          expr += advance();
        }
      }
      if (depth > 0)
        throw FarError("unterminated interpolation expression", line, col);
      tok.interp_exprs.push_back(expr);
      continue;
    }
    if (ch == '\\') {
      advance();
      if (pos_ >= source_.size())
        throw FarError("unterminated interpolated string", line, col);
      current += decodeEscape(advance(), line, col);
      continue;
    }
    current += advance();
  }
  throw FarError("unterminated interpolated string", line, col);
}



std::vector<Token> Lexer::tokenize() {

  std::vector<Token> tokens;

  while (pos_ < source_.size()) {

    skipWhitespaceAndComments();

    if (pos_ >= source_.size())

      break;

    int start_line = line_, start_col = col_;

    char ch = source_[pos_];



    if (std::isalpha(static_cast<unsigned char>(ch)) || ch == '_') {

      tokens.push_back(readIdent(start_line, start_col));

    } else if (std::isdigit(static_cast<unsigned char>(ch))) {

      tokens.push_back(readInt(start_line, start_col));

    } else if (ch == '.' && std::isdigit(static_cast<unsigned char>(peek(1))) &&
               !tokens.empty() && tokens.back().kind == TokenKind::Ident) {

      advance();

      tokens.push_back({TokenKind::Dot, ".", start_line, start_col});

    } else if (ch == '.' && std::isdigit(static_cast<unsigned char>(peek(1)))) {

      tokens.push_back(readFloat(start_line, start_col));

    } else if (ch == '$' && peek(1) == '"') {

      tokens.push_back(readInterpString(start_line, start_col));

    } else if (ch == '"') {

      tokens.push_back(readString(start_line, start_col));

    } else if (ch == '\'') {

      tokens.push_back(readChar(start_line, start_col));

    } else if (ch == '(') {

      advance();

      tokens.push_back({TokenKind::LParen, "(", start_line, start_col});

    } else if (ch == ')') {

      advance();

      tokens.push_back({TokenKind::RParen, ")", start_line, start_col});

    } else if (ch == '{') {

      advance();

      tokens.push_back({TokenKind::LBrace, "{", start_line, start_col});

    } else if (ch == '}') {

      advance();

      tokens.push_back({TokenKind::RBrace, "}", start_line, start_col});

    } else if (ch == '[') {

      advance();

      tokens.push_back({TokenKind::LBracket, "[", start_line, start_col});

    } else if (ch == ']') {

      advance();

      tokens.push_back({TokenKind::RBracket, "]", start_line, start_col});

    } else if (ch == ',') {

      advance();

      tokens.push_back({TokenKind::Comma, ",", start_line, start_col});

    } else if (ch == ';') {

      advance();

      tokens.push_back({TokenKind::Semi, ";", start_line, start_col});

    } else if (ch == '+') {
      advance();
      if (peek() == '+') {
        advance();
        tokens.push_back({TokenKind::PlusPlus, "++", start_line, start_col});
      } else if (peek() == '=') {
        advance();
        tokens.push_back({TokenKind::PlusEq, "+=", start_line, start_col});
      } else {
        tokens.push_back({TokenKind::Plus, "+", start_line, start_col});
      }
    } else if (ch == '-') {
      advance();
      if (peek() == '>') {
        advance();
        tokens.push_back({TokenKind::Arrow, "->", start_line, start_col});
      } else if (peek() == '-') {
        advance();
        tokens.push_back({TokenKind::MinusMinus, "--", start_line, start_col});
      } else if (peek() == '=') {
        advance();
        tokens.push_back({TokenKind::MinusEq, "-=", start_line, start_col});
      } else {
        tokens.push_back({TokenKind::Minus, "-", start_line, start_col});
      }
    } else if (ch == '*') {
      advance();
      if (peek() == '*') {
        advance();
        if (peek() == '=') {
          advance();
          tokens.push_back({TokenKind::StarStarEq, "**=", start_line, start_col});
        } else {
          tokens.push_back({TokenKind::StarStar, "**", start_line, start_col});
        }
      } else if (peek() == '=') {
        advance();
        tokens.push_back({TokenKind::StarEq, "*=", start_line, start_col});
      } else {
        tokens.push_back({TokenKind::Star, "*", start_line, start_col});
      }
    } else if (ch == '/') {
      advance();
      if (peek() == '/') {
        advance();
        if (peek() == '=') {
          advance();
          tokens.push_back({TokenKind::FloorDivEq, "//=", start_line, start_col});
        } else {
          tokens.push_back({TokenKind::FloorDiv, "//", start_line, start_col});
        }
      } else if (peek() == '=') {
        advance();
        tokens.push_back({TokenKind::SlashEq, "/=", start_line, start_col});
      } else {
        tokens.push_back({TokenKind::Slash, "/", start_line, start_col});
      }
    } else if (ch == '~') {
      advance();
      tokens.push_back({TokenKind::Tilde, "~", start_line, start_col});
    } else if (ch == '%') {
      advance();
      if (peek() == '=') {
        advance();
        tokens.push_back({TokenKind::PercentEq, "%=", start_line, start_col});
      } else {
        tokens.push_back({TokenKind::Percent, "%", start_line, start_col});
      }
    } else if (ch == '&') {
      advance();
      if (peek() == '&') {
        advance();
        tokens.push_back({TokenKind::AmpAmp, "&&", start_line, start_col});
      } else if (peek() == '=') {
        advance();
        tokens.push_back({TokenKind::AmpEq, "&=", start_line, start_col});
      } else {
        tokens.push_back({TokenKind::Amp, "&", start_line, start_col});
      }
    } else if (ch == '^') {
      advance();
      if (peek() == '=') {
        advance();
        tokens.push_back({TokenKind::CaretEq, "^=", start_line, start_col});
      } else {
        tokens.push_back({TokenKind::Caret, "^", start_line, start_col});
      }
    } else if (ch == ':') {
      advance();
      tokens.push_back({TokenKind::Colon, ":", start_line, start_col});
    } else if (ch == '=') {
      advance();
      if (peek() == '=') {
        advance();
        if (peek() == '=') {
          advance();
          tokens.push_back({TokenKind::EqEqEq, "===", start_line, start_col});
        } else {
          tokens.push_back({TokenKind::EqEq, "==", start_line, start_col});
        }
      } else if (peek() == '>') {
        advance();
        tokens.push_back({TokenKind::FatArrow, "=>", start_line, start_col});
      } else {
        tokens.push_back({TokenKind::Eq, "=", start_line, start_col});
      }
    } else if (ch == '$') {
      advance();
      tokens.push_back({TokenKind::Dollar, "$", start_line, start_col});
    } else if (ch == '!') {
      advance();
      if (peek() == '=') {
        advance();
        if (peek() == '=') {
          advance();
          tokens.push_back({TokenKind::BangEqEq, "!==", start_line, start_col});
        } else {
          tokens.push_back({TokenKind::BangEq, "!=", start_line, start_col});
        }
      } else if (peek() == '?') {
        advance();
        tokens.push_back({TokenKind::BangQuestion, "!?", start_line, start_col});
      } else {
        tokens.push_back({TokenKind::Bang, "!", start_line, start_col});
      }
    } else if (ch == '<') {
      advance();
      if (peek() == '<') {
        advance();
        if (peek() == '=') {
          advance();
          tokens.push_back({TokenKind::LShiftEq, "<<=", start_line, start_col});
        } else {
          tokens.push_back({TokenKind::LShift, "<<", start_line, start_col});
        }
      } else if (peek() == '=') {
        advance();
        tokens.push_back({TokenKind::Lte, "<=", start_line, start_col});
      } else {
        tokens.push_back({TokenKind::Lt, "<", start_line, start_col});
      }
    } else if (ch == '>') {
      advance();
      if (peek() == '>') {
        advance();
        if (peek() == '=') {
          advance();
          tokens.push_back({TokenKind::RShiftEq, ">>=", start_line, start_col});
        } else {
          tokens.push_back({TokenKind::RShift, ">>", start_line, start_col});
        }
      } else if (peek() == '=') {
        advance();
        tokens.push_back({TokenKind::Gte, ">=", start_line, start_col});
      } else {
        tokens.push_back({TokenKind::Gt, ">", start_line, start_col});
      }
    } else if (ch == '|') {
      advance();
      if (peek() == '>') {
        advance();
        tokens.push_back({TokenKind::PipeGt, "|>", start_line, start_col});
      } else if (peek() == '|') {
        advance();
        tokens.push_back({TokenKind::PipePipe, "||", start_line, start_col});
      } else if (peek() == '=') {
        advance();
        tokens.push_back({TokenKind::PipeEq, "|=", start_line, start_col});
      } else {
        tokens.push_back({TokenKind::Pipe, "|", start_line, start_col});
      }
    } else if (ch == '.') {
      if (peek(1) == '.' && peek(2) == '.') {
        advance();
        advance();
        advance();
        tokens.push_back({TokenKind::Ellipsis, "...", start_line, start_col});
      } else if (peek(1) == '.' && peek(2) == '<') {
        advance();
        advance();
        advance();
        tokens.push_back({TokenKind::DotDotLt, "..<", start_line, start_col});
      } else if (peek(1) == '.') {
        advance();
        advance();
        tokens.push_back({TokenKind::DotDot, "..", start_line, start_col});
      } else {
        advance();
        tokens.push_back({TokenKind::Dot, ".", start_line, start_col});
      }
    } else if (ch == '@') {
      advance();
      tokens.push_back({TokenKind::At, "@", start_line, start_col});
    } else if (ch == '?') {
      advance();
      if (peek() == '?') {
        advance();
        if (peek() == '=') {
          advance();
          tokens.push_back({TokenKind::QuestionEq, std::string("??") + "=", start_line, start_col});
        } else {
          tokens.push_back({TokenKind::QuestionQuestion, "??", start_line, start_col});
        }
      } else if (peek() == '.') {
        advance();
        tokens.push_back({TokenKind::QuestionDot, "?.", start_line, start_col});
      } else {
        tokens.push_back({TokenKind::Question, "?", start_line, start_col});
      }
    } else {

      throw FarError(std::string("unexpected character '") + ch + "'", start_line, start_col);

    }

  }

  tokens.push_back({TokenKind::Eof, "", line_, col_});

  return tokens;

}



}  // namespace far

