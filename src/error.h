#pragma once

#include <iosfwd>
#include <stdexcept>
#include <string>
#include <vector>

namespace far {

class FarError : public std::runtime_error {
public:
  std::string message;
  std::string file;
  int line;
  int col;
  std::string source_line;

  FarError(const std::string& msg, int line = 0, int col = 0);

private:
  static void captureContext(FarError& err);
};

struct DiagnosticFrame {
  std::string file;
  std::string source;
};

class DiagnosticScope {
public:
  DiagnosticScope(std::string file, std::string source);
  ~DiagnosticScope();

private:
  size_t depth_;
};

std::string extractSourceLine(const std::string& source, int line);
void printDiagnostic(std::ostream& out, const FarError& err);
void printDiagnostic(std::ostream& out, const std::exception& err);

int64_t parseIntLiteral(const std::string& text, int line, int col, bool* unsigned_decimal = nullptr);

}  // namespace far
