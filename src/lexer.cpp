#include "lexer.h"

#include "error.h"

#include <cstdint>

#include <cctype>



namespace far {

namespace {

constexpr size_t kMaxStringLiteralLen = 64 * 1024 * 1024;
constexpr size_t kMaxIdentLen = 65536;
constexpr size_t kMaxInterpExprLen = 65536;
constexpr size_t kMaxNumLiteralLen = 4096;

static void checkLexSize(size_t n, size_t max, int line, int col, const char* what) {
  if (n > max)
    throw FarError(std::string(what) + " too long", line, col);
}

void validateUtf8Sequence(unsigned char lead, const unsigned char* cont, size_t cont_len, int line,
                          int col, const char* where) {
  uint32_t cp = 0;
  if (cont_len == 1) {
    cp = ((lead & 0x1F) << 6) | (cont[0] & 0x3F);
    if (cp < 0x80)
      throw FarError(std::string("overlong UTF-8 in ") + where, line, col);
  } else if (cont_len == 2) {
    cp = ((lead & 0x0F) << 12) | ((cont[0] & 0x3F) << 6) | (cont[1] & 0x3F);
    if (cp < 0x800)
      throw FarError(std::string("overlong UTF-8 in ") + where, line, col);
  } else if (cont_len == 3) {
    cp = ((lead & 0x07) << 18) | ((cont[0] & 0x3F) << 12) | ((cont[1] & 0x3F) << 6) |
         (cont[2] & 0x3F);
    if (cp < 0x10000 || cp > 0x10FFFF)
      throw FarError(std::string("invalid UTF-8 codepoint in ") + where, line, col);
  }
  if (cp >= 0xD800 && cp <= 0xDFFF)
    throw FarError(std::string("UTF-8 surrogate not allowed in ") + where, line, col);
}

}  // namespace



Lexer::Lexer(std::string source) : source_(std::move(source)) {
  if (source_.size() >= 3 && static_cast<unsigned char>(source_[0]) == 0xEF &&
      static_cast<unsigned char>(source_[1]) == 0xBB && static_cast<unsigned char>(source_[2]) == 0xBF)
    pos_ = 3;
}



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

    unsigned char ch = static_cast<unsigned char>(source_[pos_]);

    if (std::isalnum(ch) || ch == '_') {

      advance();

    } else if (ch >= 0x80) {

      size_t len = 0;

      if ((ch & 0xE0) == 0xC0)
        len = 2;
      else if ((ch & 0xF0) == 0xE0)
        len = 3;
      else if ((ch & 0xF8) == 0xF0)
        len = 4;
      else
        throw FarError("invalid UTF-8 in identifier", line, col);

      if (pos_ + len > source_.size())
        throw FarError("invalid UTF-8 in identifier", line, col);

      bool ok = true;

      for (size_t i = 1; i < len; ++i) {
        unsigned char next = static_cast<unsigned char>(source_[pos_ + i]);
        if ((next & 0xC0) != 0x80) {
          ok = false;
          break;
        }
      }

      if (!ok)
        throw FarError("invalid UTF-8 in identifier", line, col);

      unsigned char cont[3] = {0, 0, 0};
      for (size_t i = 1; i < len; ++i)
        cont[i - 1] = static_cast<unsigned char>(source_[pos_ + i]);
      validateUtf8Sequence(ch, cont, len - 1, line, col, "identifier");

      for (size_t i = 0; i < len; ++i)
        advance();

    } else {

      break;

    }

  }

  if (pos_ - start > kMaxIdentLen)
    throw FarError("identifier too long", line, col);

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

  while (pos_ < source_.size() && std::isdigit(static_cast<unsigned char>(source_[pos_]))) {
    if (pos_ - start >= kMaxNumLiteralLen)
      throw FarError("integer literal too long", line, col);
    advance();
  }

  if (peek() == '.') {

    size_t save = pos_;

    advance();

    if (std::isdigit(static_cast<unsigned char>(peek()))) {

      bool seen_dot = true;

      while (pos_ < source_.size()) {

        if (source_[pos_] == '.') {

          if (seen_dot)
            throw FarError("invalid float literal", line, col);

          seen_dot = true;

          advance();

        } else if (std::isdigit(static_cast<unsigned char>(source_[pos_]))) {
          if (pos_ - start >= kMaxNumLiteralLen)
            throw FarError("float literal too long", line, col);
          advance();

        } else {

          break;

        }

      }

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

  bool seen_dot = (source_[start] == '.');

  while (pos_ < source_.size()) {

    if (source_[pos_] == '.') {

      if (seen_dot)
        throw FarError("invalid float literal", line, col);

      seen_dot = true;

      advance();

    } else if (std::isdigit(static_cast<unsigned char>(source_[pos_]))) {
      if (pos_ - start >= kMaxNumLiteralLen)
        throw FarError("float literal too long", line, col);
      advance();

    } else {

      break;

    }

  }

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

char Lexer::decodeStringEscape(char esc, int line, int col) {
  if (esc == '0')
    throw FarError("null byte escape \\0 is not allowed in string literals", line, col);
  return decodeEscape(esc, line, col);
}

void Lexer::appendUtf8Char(std::string& value, int line, int col) {
  if (pos_ >= source_.size())
    throw FarError("unexpected end of input", line, col);
  unsigned char uch = static_cast<unsigned char>(source_[pos_]);
  if (uch >= 0x80) {
    if ((uch & 0xC0) == 0x80)
      throw FarError("invalid UTF-8 in string", line, col);
    size_t len = 1;
    if ((uch & 0xE0) == 0xC0)
      len = 2;
    else if ((uch & 0xF0) == 0xE0)
      len = 3;
    else if ((uch & 0xF8) == 0xF0)
      len = 4;
    else
      throw FarError("invalid UTF-8 in string", line, col);
    if (pos_ + len > source_.size())
      throw FarError("unterminated string", line, col);
    for (size_t i = 1; i < len; ++i) {
      unsigned char next = static_cast<unsigned char>(source_[pos_ + i]);
      if ((next & 0xC0) != 0x80)
        throw FarError("invalid UTF-8 in string", line, col);
    }
    unsigned char cont[3] = {0, 0, 0};
    for (size_t i = 1; i < len; ++i)
      cont[i - 1] = static_cast<unsigned char>(source_[pos_ + i]);
    validateUtf8Sequence(uch, cont, len - 1, line, col, "string");
    for (size_t i = 0; i < len; ++i)
      value += advance();
    return;
  }
  value += advance();
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
      value += decodeStringEscape(advance(), line, col);
      checkLexSize(value.size(), kMaxStringLiteralLen, line, col, "string literal");
      continue;
    }
    appendUtf8Char(value, line, col);
    checkLexSize(value.size(), kMaxStringLiteralLen, line, col, "string literal");
  }
  throw FarError("unterminated string", line, col);
}

Token Lexer::readChar(int line, int col) {
  advance(); // opening '
  if (pos_ >= source_.size())
    throw FarError("unterminated character literal", line, col);
  uint32_t value = 0;
  if (peek() == '\'')
    throw FarError("empty character literal", line, col);
  if (peek() == '\\') {
    advance();
    if (pos_ >= source_.size())
      throw FarError("unterminated character literal", line, col);
    value = static_cast<unsigned char>(decodeEscape(advance(), line, col));
  } else {
    unsigned char ch = static_cast<unsigned char>(peek());
    size_t len = 1;
    if (ch >= 0x80) {
      if ((ch & 0xE0) == 0xC0)
        len = 2;
      else if ((ch & 0xF0) == 0xE0)
        len = 3;
      else if ((ch & 0xF8) == 0xF0)
        len = 4;
      else
        throw FarError("invalid UTF-8 in character literal", line, col);
      if (pos_ + len > source_.size())
        throw FarError("unterminated character literal", line, col);
      for (size_t i = 1; i < len; ++i) {
        unsigned char next = static_cast<unsigned char>(source_[pos_ + i]);
        if ((next & 0xC0) != 0x80)
          throw FarError("invalid UTF-8 in character literal", line, col);
      }
      if (len == 2) {
        value = ((ch & 0x1F) << 6) | (static_cast<unsigned char>(source_[pos_ + 1]) & 0x3F);
      } else if (len == 3) {
        value = ((ch & 0x0F) << 12) |
                ((static_cast<unsigned char>(source_[pos_ + 1]) & 0x3F) << 6) |
                (static_cast<unsigned char>(source_[pos_ + 2]) & 0x3F);
      } else {
        value = ((ch & 0x07) << 18) |
                ((static_cast<unsigned char>(source_[pos_ + 1]) & 0x3F) << 12) |
                ((static_cast<unsigned char>(source_[pos_ + 2]) & 0x3F) << 6) |
                (static_cast<unsigned char>(source_[pos_ + 3]) & 0x3F);
      }
      for (size_t i = 0; i < len; ++i)
        advance();
    } else {
      value = static_cast<unsigned char>(advance());
    }
    if (value > 0xFFFF)
      throw FarError("character literal codepoint out of range", line, col);
    if (value >= 0xD800 && value <= 0xDFFF)
      throw FarError("character literal cannot be a UTF-16 surrogate", line, col);
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
    if (ch == '}') {
      if (pos_ + 1 < source_.size() && source_[pos_ + 1] == '}') {
        advance();
        advance();
        current += '}';
        checkLexSize(current.size(), kMaxStringLiteralLen, line, col, "interpolated string literal");
        continue;
      }
    }
    if (ch == '{') {
      if (pos_ + 1 < source_.size() && source_[pos_ + 1] == '{') {
        advance();
        advance();
        current += '{';
        checkLexSize(current.size(), kMaxStringLiteralLen, line, col, "interpolated string literal");
        continue;
      }
      tok.interp_texts.push_back(current);
      current.clear();
      advance(); // {
      std::string expr;
      int depth = 1;
      const int kMaxInterpDepth = 512;
      while (pos_ < source_.size() && depth > 0) {
        char c = peek();
        if (c == '{') {
          if (depth >= kMaxInterpDepth)
            throw FarError("interpolation nesting depth exceeded", line, col);
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
        checkLexSize(expr.size(), kMaxInterpExprLen, line, col, "interpolation expression");
      }
      if (depth > 0)
        throw FarError("unterminated interpolation expression", line, col);
      checkLexSize(expr.size(), kMaxInterpExprLen, line, col, "interpolation expression");
      tok.interp_exprs.push_back(expr);
      continue;
    }
    if (ch == '\\') {
      advance();
      if (pos_ >= source_.size())
        throw FarError("unterminated interpolated string", line, col);
      current += decodeStringEscape(advance(), line, col);
      checkLexSize(current.size(), kMaxStringLiteralLen, line, col, "interpolated string literal");
      continue;
    }
    appendUtf8Char(current, line, col);
    checkLexSize(current.size(), kMaxStringLiteralLen, line, col, "interpolated string literal");
  }
  throw FarError("unterminated interpolated string", line, col);
}



std::vector<Token> Lexer::tokenize() {

  std::vector<Token> tokens;
  const size_t kMaxTokens = source_.size() + 16;

  while (pos_ < source_.size()) {

    if (tokens.size() >= kMaxTokens)
      throw FarError("lexer token limit exceeded", line_, col_);

    skipWhitespaceAndComments();

    if (pos_ >= source_.size())

      break;

    int start_line = line_, start_col = col_;

    char ch = source_[pos_];



    if (std::isalpha(static_cast<unsigned char>(ch)) || ch == '_' ||
        static_cast<unsigned char>(ch) >= 0x80) {

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

