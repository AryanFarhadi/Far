#include "error.h"

#include <algorithm>
#include <iomanip>
#include <iostream>

namespace far {

namespace {

thread_local std::vector<DiagnosticFrame> g_diag_stack;

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
      std::string text = trimRight(err.source_line);
      out << std::setw(width) << err.line << " | " << text << '\n';
      out << std::setw(width) << ' ' << " | ";
      int caret_col = std::max(err.col, 1);
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
      out << "    | " << trimRight(err.source_line) << '\n';
      out << "    | " << std::string(static_cast<size_t>(std::max(err.col, 1) - 1), ' ') << '^' << '\n';
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

}  // namespace far
