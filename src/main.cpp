#include "codegen.h"
#include "error.h"
#include "modules.h"
#include "parser.h"
#include "target.h"
#include "typecheck.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#ifdef _WIN32
#define popen _popen
#define pclose _pclose
#include <fcntl.h>
#include <io.h>
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#ifdef far
#undef far
#endif
#endif

namespace fs = std::filesystem;

static far::FarTarget g_target = far::hostTarget();
static fs::path g_exe_dir;

static std::string readFile(const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  if (!in)
    throw std::runtime_error("cannot open file: " + path);
  std::ostringstream ss;
  ss << in.rdbuf();
  return ss.str();
}

static void writeFile(const std::string& path, const std::string& content) {
  std::ofstream out(path, std::ios::binary);
  if (!out)
    throw std::runtime_error("cannot write file: " + path);
  out << content;
}

static std::string quoteShellArg(const std::string& arg) {
  if (arg.find_first_of(" \t\"") == std::string::npos)
    return arg;
  std::string out = "\"";
  for (char ch : arg) {
    if (ch == '"')
      out += "\\\"";
    else
      out += ch;
  }
  out += "\"";
  return out;
}

static int runCommand(const std::string& cmd) {
#ifdef _WIN32
  static bool path_patched = false;
  if (!path_patched) {
    static const char* kLlvmBins[] = {
        "C:\\Program Files\\LLVM\\bin",
        "C:\\Program Files (x86)\\LLVM\\bin",
    };
    std::string path;
    for (const char* bin : kLlvmBins) {
      if (fs::exists(bin)) {
        path = std::string(bin) + ";";
        break;
      }
    }
    if (const char* cur = std::getenv("PATH"))
      path += cur;
    _putenv_s("PATH", path.c_str());
    path_patched = true;
  }
#endif
  return std::system(cmd.c_str());
}

#ifdef _WIN32
static std::string windowsUmLibDir() {
  std::vector<fs::path> roots;
  if (const char* env = std::getenv("WindowsSdkDir")) {
    if (env[0] != '\0')
      roots.emplace_back(env);
  }
  roots.emplace_back("D:/Windows SDK");
  roots.emplace_back("C:/Program Files (x86)/Windows Kits/10");
  for (const auto& root : roots) {
    fs::path lib = root / "Lib";
    if (!fs::exists(lib))
      continue;
    std::vector<fs::path> versions;
    for (const auto& entry : fs::directory_iterator(lib)) {
      if (entry.is_directory())
        versions.push_back(entry.path());
    }
    std::sort(versions.begin(), versions.end());
    for (auto it = versions.rbegin(); it != versions.rend(); ++it) {
      fs::path um = *it / "um" / "x64";
      if (fs::exists(um / "ws2_32.lib") || fs::exists(um / "WS2_32.Lib"))
        return um.string();
    }
  }
  return "";
}
#endif

static fs::path exeDir() {
  if (!g_exe_dir.empty())
    return g_exe_dir;
  return fs::current_path();
}

static void initExeDir(const char* argv0) {
  if (!argv0 || !argv0[0])
    return;
  fs::path p = fs::absolute(fs::path(argv0));
  if (fs::exists(p))
    g_exe_dir = p.parent_path();
}

static std::string resolveClang() {
  const char* env = std::getenv("FAR_CLANG");
  if (env && env[0] != '\0')
    return env;
#ifdef _WIN32
  static const char* kCandidates[] = {
      "C:\\Program Files\\LLVM\\bin\\clang.exe",
      "C:\\Program Files (x86)\\LLVM\\bin\\clang.exe",
  };
  for (const char* cand : kCandidates) {
    if (!fs::exists(cand))
      continue;
    char shortbuf[MAX_PATH];
    DWORD n = GetShortPathNameA(cand, shortbuf, MAX_PATH);
    if (n > 0 && n < MAX_PATH)
      return std::string(shortbuf);
    return cand;
  }
#endif
  return "clang";
}

static std::string trimTrailingWhitespace(std::string s) {
  while (!s.empty() && (s.back() == '\n' || s.back() == '\r' || s.back() == ' ' || s.back() == '\t'))
    s.pop_back();
  return s;
}

static std::string queryEffectiveTriple(const std::string& clang, const std::string& base_triple) {
  if (base_triple.empty())
    return base_triple;
  std::string cmd = quoteShellArg(clang) + " --target=" + base_triple + " -print-effective-triple";
#ifdef _WIN32
  cmd += " 2>nul";
#else
  cmd += " 2>/dev/null";
#endif
  FILE* pipe = popen(cmd.c_str(), "r");
  if (!pipe)
    return base_triple;
  char buf[256];
  std::string out;
  while (fgets(buf, sizeof(buf), pipe))
    out += buf;
  pclose(pipe);
  out = trimTrailingWhitespace(out);
  return out.empty() ? base_triple : out;
}

static void normalizeTargetTriple(const std::string& clang) {
  if (g_target.triple.empty())
    return;
  g_target.triple = queryEffectiveTriple(clang, g_target.triple);
}

static fs::path programOutputPath(const fs::path& sourcePath) {
  if (g_target.exe_suffix.empty())
    return sourcePath.parent_path() / sourcePath.stem();
  return sourcePath.parent_path() / (sourcePath.stem().string() + g_target.exe_suffix);
}

static fs::path runtimeObject(const std::string& clang) {
  fs::path rt_c = exeDir() / "runtime" / "far_rt.c";
  fs::path rt_net = exeDir() / "runtime" / "far_net.c";
  fs::path rt_o = exeDir() / "runtime" / g_target.object_cache_name;
  auto source_time = fs::last_write_time(rt_c);
  if (fs::exists(rt_net)) {
    auto net_time = fs::last_write_time(rt_net);
    if (net_time > source_time)
      source_time = net_time;
  }
  if (!fs::exists(rt_o) || source_time > fs::last_write_time(rt_o)) {
    std::string cmd = quoteShellArg(clang) + " -c \"" + rt_c.string() + "\" -o \"" + rt_o.string() + "\"";
    if (!g_target.runtime_cflags.empty())
      cmd += " " + g_target.runtime_cflags;
    if (!g_target.triple.empty())
      cmd += " --target=" + g_target.triple;
#ifdef _WIN32
    if (g_target.triple.find("windows-msvc") != std::string::npos)
      cmd += " -fms-runtime-lib=static";
#endif
    if (runCommand(cmd) != 0)
      throw std::runtime_error("failed to compile runtime/far_rt.c");
  }
  return rt_o;
}

static far::Program parseFile(const fs::path& sourcePath, const std::string& source) {
  far::DiagnosticScope diag(sourcePath.string(), source);
  far::Program program = far::parseProgram(source, true);
  return far::resolveImports(std::move(program), sourcePath.parent_path().string());
}

static int compileSource(const std::string& source, const fs::path& sourcePath, const fs::path& output,
                         const std::string& clang) {
  far::DiagnosticScope diag(sourcePath.string(), source);
  far::Program program = parseFile(sourcePath, source);
  far::typecheckProgram(program);
  std::string ir = far::generateIR(program, g_target);

  fs::path tmp = fs::temp_directory_path() / "far_build";
  fs::create_directories(tmp);
  fs::path ll = tmp / "out.ll";
  fs::path rt_o = runtimeObject(clang);

  writeFile(ll.string(), ir);

  std::string cmd = quoteShellArg(clang) + " -O2 \"" + ll.string() + "\" \"" + rt_o.string() + "\" -o \"" + output.string() + "\"";
  if (!g_target.triple.empty())
    cmd += " --target=" + g_target.triple;
#ifdef _WIN32
  if (g_target.triple.find("windows-msvc") != std::string::npos) {
    cmd += " -fuse-ld=lld-link -fms-runtime-lib=static";
    std::string libdir = windowsUmLibDir();
    if (!libdir.empty()) {
      cmd += " \"" + libdir + "/ws2_32.lib\" \"" + libdir + "/winhttp.lib\"";
    }
  }
#endif
  if (!g_target.link_flags.empty())
    cmd += " " + g_target.link_flags;
  if (runCommand(cmd) != 0)
    return 1;
  return 0;
}

static void typecheckSource(const fs::path& sourcePath, const std::string& source) {
  far::DiagnosticScope diag(sourcePath.string(), source);
  far::Program program = parseFile(sourcePath, source);
  far::typecheckProgram(program);
}

static bool needsRebuild(const fs::path& source, const fs::path& output, const fs::path& compiler) {
  if (!fs::exists(output))
    return true;
  if (fs::last_write_time(source) > fs::last_write_time(output))
    return true;
  fs::path rt_c = exeDir() / "runtime" / "far_rt.c";
  if (fs::exists(rt_c) && fs::last_write_time(rt_c) > fs::last_write_time(output))
    return true;
  fs::path rt_o = exeDir() / "runtime" / g_target.object_cache_name;
  if (fs::exists(rt_o) && fs::last_write_time(rt_o) > fs::last_write_time(output))
    return true;
  if (fs::exists(compiler) && fs::last_write_time(compiler) > fs::last_write_time(output))
    return true;
  return false;
}

static std::string trimText(const std::string& text) {
  size_t start = 0;
  while (start < text.size() && std::isspace(static_cast<unsigned char>(text[start])))
    ++start;
  size_t end = text.size();
  while (end > start && std::isspace(static_cast<unsigned char>(text[end - 1])))
    --end;
  return text.substr(start, end - start);
}

static std::string normalizeNewlines(const std::string& text) {
  std::string out;
  out.reserve(text.size());
  for (size_t i = 0; i < text.size(); ++i) {
    const unsigned char c = static_cast<unsigned char>(text[i]);
    if (c == '\r') {
      if (i + 1 < text.size() && text[i + 1] == '\n')
        ++i;
      out += '\n';
    } else {
      out += text[i];
    }
  }
  return out;
}

static std::string collapseBlankLines(const std::string& text) {
  std::string out;
  out.reserve(text.size());
  int newlines = 0;
  for (char c : text) {
    if (c == '\n') {
      ++newlines;
      if (newlines == 1)
        out += '\n';
    } else {
      newlines = 0;
      out += c;
    }
  }
  while (!out.empty() && out.back() == '\n')
    out.pop_back();
  return out;
}

static std::string formatSource(const std::string& text) {
  return trimText(collapseBlankLines(normalizeNewlines(text)));
}

static void writeFormattedStdout(const std::string& text) {
#ifdef _WIN32
  _setmode(_fileno(stdout), _O_BINARY);
#endif
  std::cout.write(text.data(), static_cast<std::streamsize>(text.size()));
  std::cout.put('\n');
  std::cout.flush();
}

static int lintSource(const std::string& source) {
  int issues = 0;
  for (char c : source) {
    if (c == '\t')
      ++issues;
  }
  return issues;
}

static std::string docForSymbol(const std::string& source, const std::string& symbol) {
  std::string needle = "fun " + symbol;
  size_t pos = source.find(needle);
  if (pos == std::string::npos)
    return "Documentation for " + symbol;
  size_t line_start = source.rfind('\n', pos);
  line_start = line_start == std::string::npos ? 0 : line_start + 1;
  if (line_start > 0) {
    size_t prev = source.rfind('\n', line_start - 2);
    prev = prev == std::string::npos ? 0 : prev + 1;
    if (prev < line_start) {
      std::string line = source.substr(prev, line_start - prev - 1);
      line = trimText(line);
      if (!line.empty() && (line[0] == '#' || (line.rfind("//", 0) == 0)))
        return trimText(line[0] == '#' ? line.substr(1) : line.substr(2));
    }
  }
  return "Documentation for " + symbol;
}

static int replLoop() {
  std::cout << "Far REPL (enter blank line to quit)\n";
  std::string line;
  while (true) {
    std::cout << "far> ";
    if (!std::getline(std::cin, line))
      break;
    line = trimText(line);
    if (line.empty())
      break;
    const std::string& p = line;
    long long acc = 0;
    int sign = 1;
    size_t i = 0;
    if (i < p.size() && p[i] == '-') {
      sign = -1;
      ++i;
    }
    while (i < p.size() && std::isdigit(static_cast<unsigned char>(p[i]))) {
      acc = acc * 10 + (p[i] - '0');
      ++i;
    }
    std::cout << acc * sign << "\n";
  }
  return 0;
}

static void applyTargetFromEnv() {
  const char* env = std::getenv("FAR_TARGET");
  if (env && env[0] != '\0')
    g_target = far::resolveTarget(env);
}

static void parseGlobalFlags(int& argc, char** argv) {
  for (int i = 1; i < argc; ++i) {
    if (std::string(argv[i]) == "--target" && i + 1 < argc) {
      g_target = far::resolveTarget(argv[i + 1]);
      for (int j = i; j + 2 < argc; ++j)
        argv[j] = argv[j + 2];
      argc -= 2;
      --i;
    }
  }
}

static void usage() {
  std::cerr << "Far compiler (native C++ / LLVM)\n\n"
            << "  far run [--target <alias>] <file.far>\n"
            << "  far check <file.far>\n"
            << "  far compile [--target <alias>] <file.far> -o <output>\n"
            << "  far emit-ir [--target <alias>] <file.far> [-o out.ll]\n"
            << "  far repl\n"
            << "  far fmt <file.far>\n"
            << "  far lint <file.far>\n"
            << "  far doc <symbol> <file.far>\n"
            << "  far perf\n\n"
            << "Targets: windows-x64, linux-x64, linux-arm64 (default: host)\n"
            << "Env: FAR_TARGET=<alias>, FAR_CLANG=<clang.exe>\n";
}

int main(int argc, char** argv) {
  if (argc < 2) {
    usage();
    return 1;
  }

  applyTargetFromEnv();
  parseGlobalFlags(argc, argv);
  initExeDir(argv[0]);

  std::string command = argv[1];
  std::string clang = resolveClang();
  normalizeTargetTriple(clang);

  try {
    if (command == "run") {
      if (argc < 3) {
        usage();
        return 1;
      }
      fs::path sourcePath = fs::absolute(argv[2]);
      fs::path exe = programOutputPath(sourcePath);
      fs::path compiler = fs::absolute(argv[0]);

      std::string source = readFile(sourcePath.string());
      typecheckSource(sourcePath, source);

      if (needsRebuild(sourcePath, exe, compiler)) {
        if (compileSource(source, sourcePath, exe, clang) != 0)
          return 1;
      }
      return runCommand("\"" + exe.string() + "\"");
    }

    if (command == "check") {
      if (argc < 3) {
        usage();
        return 1;
      }
      fs::path sourcePath = fs::absolute(argv[2]);
      std::string source = readFile(sourcePath.string());
      typecheckSource(sourcePath, source);
      std::cout << "ok\n";
      return 0;
    }

    if (command == "compile") {
      if (argc < 5 || std::string(argv[3]) != "-o") {
        usage();
        return 1;
      }
      std::string source = readFile(argv[2]);
      fs::path sourcePath = fs::absolute(argv[2]);
      if (compileSource(source, sourcePath, argv[4], clang) != 0)
        return 1;
      std::cout << "built " << argv[4] << "\n";
      return 0;
    }

    if (command == "emit-ir") {
      if (argc < 3) {
        usage();
        return 1;
      }
      std::string source = readFile(argv[2]);
      fs::path sourcePath = fs::absolute(argv[2]);
      far::DiagnosticScope diag(sourcePath.string(), source);
      far::Program program = parseFile(sourcePath, source);
      far::typecheckProgram(program);
      std::string ir = far::generateIR(program, g_target);
      if (argc >= 5 && std::string(argv[3]) == "-o") {
        writeFile(argv[4], ir);
        std::cout << "wrote " << argv[4] << "\n";
      } else {
        std::cout << ir;
      }
      return 0;
    }

    if (command == "repl")
      return replLoop();

    if (command == "fmt") {
      if (argc < 3) {
        usage();
        return 1;
      }
      std::string source = readFile(argv[2]);
      writeFormattedStdout(formatSource(source));
      return 0;
    }

    if (command == "lint") {
      if (argc < 3) {
        usage();
        return 1;
      }
      std::string source = readFile(argv[2]);
      int issues = lintSource(source);
      if (issues > 0)
        std::cout << issues << " issue(s)\n";
      else
        std::cout << "ok\n";
      return issues > 0 ? 1 : 0;
    }

    if (command == "doc") {
      if (argc < 4) {
        usage();
        return 1;
      }
      std::string source = readFile(argv[3]);
      std::cout << docForSymbol(source, argv[2]) << "\n";
      return 0;
    }

    if (command == "perf") {
      std::cout << "Far performance profile\n"
                << "  target: " << g_target.alias << " (" << g_target.triple << ")\n"
                << "  backend: LLVM IR -> native (clang -O2)\n"
                << "  incremental: source binary mtime cache\n"
                << "  runtime: " << g_target.object_cache_name << " cached object\n"
                << "  concurrency: pthread + spawn/parallel in language\n";
      return 0;
    }

    usage();
    return 1;
  } catch (const std::exception& err) {
    far::printDiagnostic(std::cerr, err);
    return 1;
  }
}
