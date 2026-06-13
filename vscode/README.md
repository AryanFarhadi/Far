# Far Language — VS Code Extension

Syntax highlighting, scoped IntelliSense, live diagnostics, and a **Run** button for `.far` files.

## Features

- **Play button** — runs `far run` on the current file
- **Scoped IntelliSense** — keywords, types, builtins, **imported** stdlib symbols (not a 2000-item dump)
- **Member completion** — `std.math.sqrt`, `import` path/selector completion
- **Signature help** — parameter hints when calling functions
- **Go to definition** — jump to local `fun` / `struct` / `class`
- **Document outline** — symbols in the Explorer outline view
- **Hover** — stdlib signatures + `far doc` for local functions
- **Format** — `Shift+Alt+F` via `far fmt`
- **Live diagnostics** — debounced `far check` while typing (GCC-style error squiggles)
- **File icon** — orange **F** badge on `.far` files in the Explorer (when the file icon theme supports language icons)
- **Setup wizard** — configure `far.exe` and clang

## Install (one command)

From the Far repo root:

```bat
vscode\install-extension.bat
```

This builds `vscode\far-lang-0.2.1.vsix` if needed and installs it into VS Code.

**Rebuild only:**

```bat
vscode\package-extension.bat
```

Then install manually: VS Code → Extensions → `...` → **Install from VSIX** → select `vscode\far-lang-0.2.1.vsix`.

Or:

```bat
code --install-extension vscode\far-lang-0.2.1.vsix
```

## Install (development — F5 debug)

1. Build the Far compiler from the repo root:
   ```bat
   build.bat
   ```

2. Open the extension folder in VS Code:
   ```
   code vscode
   ```

3. Press **F5** to launch an Extension Development Host with Far support.

## Install (permanent)

From the repo root:

```bat
cd vscode
npm install -g @vscode/vsce
vsce package
code --install-extension far-lang-0.1.0.vsix
```

Or in VS Code: **Extensions → … → Install from VSIX**.

## First-time setup

1. Open any `.far` file.
2. Run command **Far: Setup Compiler** (`Ctrl+Shift+P`).
3. Set paths:
   - **Compiler**: `C:\path\to\Far\far.exe`
   - **Clang**: `clang` (must be on PATH)

## Settings

| Setting | Description |
|---------|-------------|
| `far.compilerPath` | Path to `far.exe` |
| `far.clangPath` | Clang for LLVM backend |
| `far.checkOnSave` | Typecheck on save (default: true) |
| `far.checkWhileTyping` | Live typecheck while editing (default: true) |
| `far.checkDebounceMs` | Delay before live check (default: 400) |
| `far.runInTerminal` | Run in integrated terminal (default: true) |

## Refresh IntelliSense manually

```bat
node vscode/scripts/generate-core-api.mjs
```

Or run **Far: Refresh IntelliSense from Core** in VS Code.

## Compiler commands used

| Command | Purpose |
|---------|---------|
| `far run file.far` | Build (if needed) and run |
| `far check file.far` | Typecheck only (diagnostics) |
| `far compile file.far -o out.exe` | Build executable |

## Recommended workspace settings

Add to `.vscode/settings.json` in your Far project:

```json
{
  "far.compilerPath": "${workspaceFolder}/far.exe",
  "far.corePath": "${workspaceFolder}/core",
  "far.checkOnSave": true,
  "files.associations": {
    "*.far": "far"
  }
}
```
