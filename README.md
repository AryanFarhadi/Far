# 🚀 Far Programming Language

**Far** is a compiled programming language with a native C++ frontend, LLVM IR codegen, and a rich standard library. Programs compile to native executables via **Clang** (`-O2`), giving performance in the same class as C — not an interpreter.

```far
fun main() -> i64 {
  print("Hello, Far!")
  return 0
}
```

## ✨ Features

- **Native Performance** — LLVM IR → Clang `-O2` → machine code.
- **Static Typing** — `i64`, `f64`, `string`, structs, classes, enums, generics, traits.
- **Modules & Packages** — `import math` then `math.sqrt(x)` (or `import math as m`). 
- **Geometry Type Modules** — `import vectors` then `vectors.distance(a, b)`, `vectors.dot(a, b)`, `vectors.cross(x, y)`. Same pattern for `points`, `rects`, `matrices`, `quaternions`, `colors`, `bounds`, and `transforms`.
- **Concurrency** — Built-in support for `spawn`, actors, and parallel blocks.
- **Large Standard Library** — Math, I/O, JSON, networking, crypto, science, and performance introspection.
- **Cross-Compilation** — Target `windows-x64`, `linux-x64`, and `linux-arm64` out of the box.
- **VS Code Extension** — Full syntax highlighting, IntelliSense, diagnostics, a Run button, and custom `.far` file icons.

---

## 📦 Installation

### Windows (Recommended: One-Click Installer)
Installs **Far**, **Clang** (via winget when available), configures **PATH**, and installs the **VS Code extension**:

```bash
git clone https://github.com/AryanFarhadi/Far.git
cd Far
install\windows\install-far.bat
```
*Reload VS Code and open any `.far` file to get started.*

> **Uninstall:** Run `install\windows\uninstall-far.bat`

### Windows (Manual Build)
```bash
git clone https://github.com/AryanFarhadi/Far.git
cd Far
build.bat
```
*Ensure `clang` and `clang++` are on your PATH. Optionally install the VS Code extension via `vscode\install-extension.bat`.*

### Linux (Manual Build)
```bash
git clone https://github.com/AryanFarhadi/Far.git
cd Far
chmod +x build.sh run_tests.sh benchmark/run_benchmark.sh
./build.sh
```
*Add `./far` to your PATH or copy it to `/usr/local/bin`.*
*Optional: Install the VS Code extension from `vscode/far-lang-*.vsix` using `code --install-extension <file.vsix>`.*

---

## 🛠 Requirements

### Windows (x64)
| Tool | Purpose | Installation |
| :--- | :--- | :--- |
| **LLVM / Clang** | Compiles Far programs to `.exe` | [LLVM-MinGW](https://github.com/mstorsjo/llvm-mingw) or `winget install MartinStorsjo.LLVM-MinGW.UCRT` |
| **Clang++** | Builds the Far compiler itself | Included in the LLVM package above (`clang++` must be on PATH) |
| **VS Code** *(Optional)* | Editor support | [code.visualstudio.com](https://code.visualstudio.com/) |

### Linux (x86_64 or ARM64)
| Tool | Purpose | Installation |
| :--- | :--- | :--- |
| **clang / clang++** | Compiler + Far backend | `sudo apt install clang` (Debian/Ubuntu) or distro equivalent |
| **VS Code** *(Optional)* | Editor support | [code.visualstudio.com](https://code.visualstudio.com/) |

### Build from Source (All Platforms)
- **C++17** compiler (`clang++` recommended)
- **Git**
- *No CMake required — `build.bat` / `build.sh` invoke Clang directly.*

---

## 🚀 Quick Start

Every runnable program needs a `main` entry point:

```far
fun main() -> i64 {
  print(42)
  return 0
}
```

**Run a program:**
```bash
# Windows
far run hello.far

# Linux
./far run hello.far
```

**Compile to a standalone executable:**
```bash
# Windows
far compile hello.far -o hello.exe

# Linux
./far compile hello.far -o hello
```

---

## 💻 Compiler CLI

| Command | Description |
| :--- | :--- |
| `far run <file.far>` | Build (if stale) and run the program |
| `far check <file.far>` | Typecheck only (no executable generated) |
| `far compile <file.far> -o <output>` | Build a standalone executable |
| `far emit-ir <file.far>` | Emit LLVM IR (optionally with `-o out.ll`) |
| `far fmt <file.far>` | Format source code |
| `far repl` | Start the minimal REPL |
| `far perf` | Show backend / target info |

### Cross-Compilation
Compile for different targets using the `--target` flag or the `FAR_TARGET` environment variable.

| Alias | Triple |
| :--- | :--- |
| `windows-x64` | `x86_64-w64-windows-gnu` |
| `linux-x64` | `x86_64-unknown-linux-gnu` |
| `linux-arm64` | `aarch64-unknown-linux-gnu` |

**Example:**
```bash
# Using flag
far compile --target linux-arm64 app.far -o app

# Using environment variable
set FAR_TARGET=linux-x64
far compile app.far -o app
```

### Environment Variables
| Variable | Description |
| :--- | :--- |
| `FAR_CLANG` | Path to `clang` (set automatically by the Windows installer) |
| `FAR_TARGET` | Default cross-compile target alias |

---

## 🤝 Contributing

Contributions, issues, and feature requests are welcome!  
Feel free to check the [issues page](https://github.com/AryanFarhadi/Far/issues) or submit a Pull Request.

## 📄 License

This project is licensed under the [Apache-2.0 license](LICENSE)
