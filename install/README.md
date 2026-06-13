# Far Windows Installer

One-click setup for **far.exe**, **LLVM/Clang**, and the **VS Code extension**.

## Quick install (from repo)

```bat
install\windows\install-far.bat
```

This will:

1. Build `far.exe` (if missing)
2. Install Far to `%LOCALAPPDATA%\Programs\Far\`
3. Install **LLVM-MinGW** (clang) via winget if not already on PATH
4. Add Far and Clang to your **user PATH**
5. Install the **Far Language** VS Code extension
6. Write VS Code settings (`far.compilerPath`, `far.clangPath`) automatically

After install, reload VS Code and open any `.far` file — Run should work without manual setup.

## Uninstall

```bat
install\windows\uninstall-far.bat
```

Removes Far from `%LOCALAPPDATA%\Programs\Far\`, PATH entries, VS Code settings keys, and `%APPDATA%\Far\install.json`. Does **not** remove LLVM (may be used by other tools).

## Install locations

| Item | Path |
|------|------|
| Far compiler | `%LOCALAPPDATA%\Programs\Far\far.exe` |
| Runtime | `%LOCALAPPDATA%\Programs\Far\runtime\` |
| Install manifest | `%APPDATA%\Far\install.json` |
| VS Code extension | Installed via `code --install-extension` |

## Environment variables (optional)

| Variable | Purpose |
|----------|---------|
| `FAR_CLANG` | Path to clang (set by installer if needed) |
| `FAR_TARGET` | Cross-compile target alias |

## Requirements

- Windows 10/11 x64
- [LLVM-MinGW](https://github.com/mstorsjo/llvm-mingw) (installed automatically via winget when possible)
- VS Code with `code` on PATH (optional, for extension install)

## Manual install without winget

If winget is unavailable, install LLVM yourself, then run:

```powershell
powershell -ExecutionPolicy Bypass -File install\windows\install-far.ps1 -SkipClangInstall
```

Ensure `clang` and `clang++` are on PATH before running Far programs.
