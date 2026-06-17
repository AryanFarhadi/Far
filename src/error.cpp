#include "error.h"

#include <algorithm>
#include <cctype>
#include <climits>
#include <cstdint>
#include <iomanip>
#include <iostream>

namespace far {

namespace {

thread_local std::vector<DiagnosticFrame> g_diag_stack;

constexpr size_t kMaxDiagLineLen = 200;

int lineNumberWidth(int line) {
  int w = 1;
  int n = std::max(line, 1);
  while (n >= 10) {
    n /= 10;
    ++w;
  }
  return std::max(w, 3);
}

std::string trimRight(std::string s) {
  while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r'))
    s.pop_back();
  return s;
}

std::string truncateDiagLine(const std::string& s) {
  std::string text = trimRight(s);
  if (text.size() <= kMaxDiagLineLen)
    return text;
  return text.substr(0, kMaxDiagLineLen - 3) + "...";
}

}  // namespace

std::string extractSourceLine(const std::string& source, int line) {
  if (line <= 0 || source.empty())
    return "";
  int cur = 1;
  size_t start = 0;
  for (size_t i = 0; i < source.size(); ++i) {
    if (source[i] == '\n') {
      if (cur == line)
        return source.substr(start, i - start);
      ++cur;
      start = i + 1;
    }
  }
  if (cur == line)
    return source.substr(start);
  return "";
}

void FarError::captureContext(FarError& err) {
  if (g_diag_stack.empty())
    return;
  const DiagnosticFrame& frame = g_diag_stack.back();
  if (err.file.empty())
    err.file = frame.file;
  if (err.line > 0 && err.source_line.empty())
    err.source_line = extractSourceLine(frame.source, err.line);
}

FarError::FarError(const std::string& msg, int line, int col)
    : std::runtime_error(msg), message(msg), line(line), col(col) {
  captureContext(*this);
}

DiagnosticScope::DiagnosticScope(std::string file, std::string source)
    : depth_(g_diag_stack.size()) {
  g_diag_stack.push_back(DiagnosticFrame{std::move(file), std::move(source)});
}

DiagnosticScope::~DiagnosticScope() {
  while (g_diag_stack.size() > depth_)
    g_diag_stack.pop_back();
}

void printDiagnostic(std::ostream& out, const FarError& err) {
  if (!err.file.empty() && err.line > 0) {
    out << err.file << ':' << err.line << ':' << std::max(err.col, 1) << ": error: " << err.message << '\n';
    if (!err.source_line.empty()) {
      const int width = lineNumberWidth(err.line);
      std::string text = truncateDiagLine(err.source_line);
      out << std::setw(width) << err.line << " | " << text << '\n';
      out << std::setw(width) << ' ' << " | ";
      int caret_col = std::max(err.col, 1);
      if (static_cast<size_t>(caret_col) > text.size())
        caret_col = static_cast<int>(text.size()) + 1;
      out << std::string(static_cast<size_t>(caret_col - 1), ' ') << '^' << '\n';
    }
    return;
  }

  if (!err.file.empty()) {
    out << err.file << ": error: " << err.message << '\n';
    return;
  }

  if (err.line > 0) {
    out << "line " << err.line << ':' << std::max(err.col, 1) << ": error: " << err.message << '\n';
    if (!err.source_line.empty()) {
      std::string text = truncateDiagLine(err.source_line);
      out << "    | " << text << '\n';
      int caret_col = std::max(err.col, 1);
      if (static_cast<size_t>(caret_col) > text.size())
        caret_col = static_cast<int>(text.size()) + 1;
      out << "    | " << std::string(static_cast<size_t>(caret_col - 1), ' ') << '^' << '\n';
    }
    return;
  }

  out << "error: " << err.message << '\n';
}

void printDiagnostic(std::ostream& out, const std::exception& err) {
  if (const FarError* fe = dynamic_cast<const FarError*>(&err)) {
    printDiagnostic(out, *fe);
    return;
  }
  out << "error: " << err.what() << '\n';
}

int64_t parseIntLiteral(const std::string& text, int line, int col, bool* unsigned_decimal) {
  if (unsigned_decimal)
    *unsigned_decimal = false;
  if (text.empty())
    throw FarError("invalid integer literal", line, col);
  size_t i = 0;
  bool neg = false;
  if (text[0] == '-') {
    neg = true;
    i = 1;
  }
  if (i >= text.size())
    throw FarError("invalid integer literal", line, col);
  uint64_t v = 0;
  for (; i < text.size(); ++i) {
    if (!std::isdigit(static_cast<unsigned char>(text[i])))
      throw FarError("invalid integer literal", line, col);
    unsigned digit = static_cast<unsigned>(text[i] - '0');
    if (v > (UINT64_MAX - digit) / 10)
      throw FarError("integer literal overflow", line, col);
    v = v * 10 + digit;
  }
  if (!neg) {
    if (unsigned_decimal && v > static_cast<uint64_t>(INT64_MAX))
      *unsigned_decimal = true;
    return static_cast<int64_t>(v);
  }
  if (v > static_cast<uint64_t>(INT64_MAX) + 1ULL)
    throw FarError("integer literal overflow", line, col);
  if (v == static_cast<uint64_t>(INT64_MAX) + 1ULL)
    return INT64_MIN;
  return -static_cast<int64_t>(v);
}

}  // namespace far
