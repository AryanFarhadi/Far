'use strict';

const vscode = require('vscode');
const fs = require('fs');
const os = require('os');
const path = require('path');
const { spawn } = require('child_process');
const { promisify } = require('util');
const { execFile } = require('child_process');
const intel = require('./intellisense');
const { FAR_TEXTMATE_RULES } = require('./far-syntax-colors');

const execFileAsync = promisify(execFile);

/** @type {object|null} */
let coreApi = null;
/** @type {Map<string, NodeJS.Timeout>} */
const checkDebounce = new Map();

function getConfig() {
  return vscode.workspace.getConfiguration('far');
}

function extensionDir() {
  return path.dirname(__filename);
}

function readInstallManifest() {
  const candidates = [
    process.env.APPDATA && path.join(process.env.APPDATA, 'Far', 'install.json'),
    process.env.HOME && path.join(process.env.HOME, '.far', 'install.json'),
  ].filter(Boolean);
  for (const p of candidates) {
    if (!fs.existsSync(p)) continue;
    try {
      return JSON.parse(fs.readFileSync(p, 'utf8'));
    } catch {
      /* ignore */
    }
  }
  return null;
}

function standardInstallPaths() {
  const roots = [];
  if (process.env.LOCALAPPDATA) {
    roots.push(path.join(process.env.LOCALAPPDATA, 'Programs', 'Far'));
  }
  if (process.env.ProgramFiles) {
    roots.push(path.join(process.env.ProgramFiles, 'Far'));
  }
  return roots;
}

async function detectCompiler() {
  const manifest = readInstallManifest();
  if (manifest?.farExe && fs.existsSync(manifest.farExe)) {
    return manifest.farExe;
  }

  const configured = getConfig().get('compilerPath', '');
  if (configured && configured !== 'far' && fs.existsSync(configured)) return configured;

  for (const root of standardInstallPaths()) {
    const exe = process.platform === 'win32'
      ? path.join(root, 'far.exe')
      : path.join(root, 'far');
    if (fs.existsSync(exe)) return exe;
  }

  const roots = [
    vscode.workspace.workspaceFolders?.[0]?.uri.fsPath,
    path.join(extensionDir(), '..'),
    process.cwd(),
  ].filter(Boolean);

  for (const root of roots) {
    const exe = process.platform === 'win32'
      ? path.join(root, 'far.exe')
      : path.join(root, 'far');
    if (fs.existsSync(exe)) return exe;
  }

  return configured || 'far';
}

async function detectClang() {
  const manifest = readInstallManifest();
  if (manifest?.clangExe && fs.existsSync(manifest.clangExe)) {
    return manifest.clangExe;
  }

  const configured = getConfig().get('clangPath', '');
  if (configured && configured !== 'clang' && fs.existsSync(configured)) {
    return configured;
  }

  if (process.env.FAR_CLANG && fs.existsSync(process.env.FAR_CLANG)) {
    return process.env.FAR_CLANG;
  }

  return configured || 'clang';
}

function loadCoreApiFromDisk() {
  const apiPath = path.join(extensionDir(), 'data', 'core-api.json');
  if (!fs.existsSync(apiPath)) return null;
  try {
    return JSON.parse(fs.readFileSync(apiPath, 'utf8'));
  } catch {
    return null;
  }
}

async function regenerateCoreApi() {
  const script = path.join(extensionDir(), '..', 'tools', 'generate-core-api.mjs');
  const cpp = path.join(extensionDir(), '..', 'src', 'far_stdlib_modules.cpp');
  if (!fs.existsSync(script)) {
    coreApi = loadCoreApiFromDisk();
    return coreApi;
  }
  try {
    await execFileAsync(process.execPath, [script], {
      cwd: path.join(extensionDir(), '..'),
      env: { ...process.env },
    });
    coreApi = loadCoreApiFromDisk();
    vscode.window.showInformationMessage(
      `Far IntelliSense updated (${coreApi?.modules ? Object.keys(coreApi.modules).length : 0} stdlib modules).`,
    );
  } catch (err) {
    coreApi = loadCoreApiFromDisk();
    vscode.window.showWarningMessage(`Far: could not regenerate API (${err.message}). Using cached data.`);
  }
  return coreApi;
}

function ensureCoreApi() {
  if (!coreApi) coreApi = loadCoreApiFromDisk();
  return coreApi;
}

function quoteWinPath(p) {
  return `"${p.replace(/"/g, '""')}"`;
}

function terminalRunCommand(compiler, args) {
  if (process.platform === 'win32') {
    const shell = (vscode.env.shell || '').toLowerCase();
    const isPowerShell = shell.includes('powershell') || shell.includes('pwsh');
    const parts = [quoteWinPath(compiler), ...args.map(quoteWinPath)].join(' ');
    if (isPowerShell) return `& ${parts}`;
    return `cmd /c ${parts}`;
  }
  const q = "'";
  const quote = (s) => (/\s/.test(s) ? `${q}${s}${q}` : s);
  return [quote(compiler), ...args.map(quote)].join(' ');
}

function runFar(args, cwd) {
  return Promise.all([detectCompiler(), detectClang()]).then(([compiler, clang]) => {
    const env = { ...process.env };
    if (clang && clang !== 'clang') env.FAR_CLANG = clang;
    return new Promise((resolve, reject) => {
      const proc = spawn(compiler, args, {
        cwd: cwd || process.cwd(),
        shell: process.platform === 'win32',
        env,
      });
      let stdout = '';
      let stderr = '';
      proc.stdout?.on('data', (d) => { stdout += d; });
      proc.stderr?.on('data', (d) => { stderr += d; });
      proc.on('error', reject);
      proc.on('close', (code) => resolve({ code, stdout, stderr, compiler }));
    });
  });
}

const diagnosticCollection = vscode.languages.createDiagnosticCollection('far');

async function checkDocument(doc) {
  if (doc.languageId !== 'far') return;
  const file = doc.uri.fsPath;
  const cwd = path.dirname(file);
  try {
    const { code, stderr, stdout } = await runFar(['check', file], cwd);
    if (code === 0) {
      diagnosticCollection.set(doc.uri, []);
      return;
    }
    const combined = [stderr, stdout].filter(Boolean).join('\n');
    const diags = intel.parseDiagnostics(combined, doc.uri);
    if (diags.length === 0 && combined.trim()) {
      diags.push(new vscode.Diagnostic(
        new vscode.Range(0, 0, 0, 1),
        combined.trim(),
        vscode.DiagnosticSeverity.Error,
      ));
    }
    diagnosticCollection.set(doc.uri, diags);
  } catch (err) {
    diagnosticCollection.set(doc.uri, [
      new vscode.Diagnostic(
        new vscode.Range(0, 0, 0, 1),
        `Far compiler not found. Run "Far: Setup Compiler" (${err.message})`,
        vscode.DiagnosticSeverity.Warning,
      ),
    ]);
  }
}

function scheduleCheck(doc) {
  const key = doc.uri.toString();
  const existing = checkDebounce.get(key);
  if (existing) clearTimeout(existing);
  const delay = getConfig().get('checkDebounceMs', 400);
  checkDebounce.set(key, setTimeout(() => {
    checkDebounce.delete(key);
    checkDocument(doc);
  }, delay));
}

async function runCurrentFile() {
  const editor = vscode.window.activeTextEditor;
  if (!editor || editor.document.languageId !== 'far') {
    vscode.window.showWarningMessage('Open a .far file to run.');
    return;
  }
  await editor.document.save();
  const file = editor.document.uri.fsPath;
  const cwd = path.dirname(file);
  const compiler = await detectCompiler();

  if (getConfig().get('runInTerminal', true)) {
    const term = vscode.window.createTerminal({ name: 'Far Run', cwd });
    term.show();
    term.sendText(terminalRunCommand(compiler, ['run', file]), true);
    return;
  }

  const { code, stdout, stderr } = await runFar(['run', file], cwd);
  if (stdout) vscode.window.showInformationMessage(stdout.trim().slice(0, 200));
  if (code !== 0) vscode.window.showErrorMessage(stderr.trim() || `Exit code ${code}`);
}

async function buildCurrentFile() {
  const editor = vscode.window.activeTextEditor;
  if (!editor) return;
  await editor.document.save();
  const file = editor.document.uri.fsPath;
  const out = file.replace(/\.far$/i, '.exe');
  const cwd = path.dirname(file);
  const { code, stderr } = await runFar(['compile', file, '-o', out], cwd);
  if (code === 0) vscode.window.showInformationMessage(`Built ${out}`);
  else vscode.window.showErrorMessage(stderr.trim() || 'Build failed');
}

function normalizeFmtOutput(text) {
  return text.replace(/\r\n/g, '\n').replace(/\r/g, '\n').replace(/\n+$/, '');
}

function applyDocumentEol(doc, text) {
  if (doc.eol === vscode.EndOfLine.CRLF) {
    return text.replace(/\n/g, '\r\n');
  }
  return text;
}

async function formatDocument(doc) {
  const cwd = path.dirname(doc.uri.fsPath);
  const tmp = path.join(os.tmpdir(), `far-fmt-${process.pid}-${Date.now()}.far`);
  try {
    fs.writeFileSync(tmp, doc.getText(), 'utf8');
    const { code, stdout, stderr } = await runFar(['fmt', tmp], cwd);
    if (code !== 0) throw new Error(stderr.trim() || 'Format failed');
    const formatted = applyDocumentEol(doc, normalizeFmtOutput(stdout));
    const fullRange = new vscode.Range(
      doc.positionAt(0),
      doc.positionAt(doc.getText().length),
    );
    return [vscode.TextEdit.replace(fullRange, formatted)];
  } finally {
    try {
      fs.unlinkSync(tmp);
    } catch {
      /* ignore */
    }
  }
}

async function setupCompiler() {
  const current = await detectCompiler();
  const currentClang = await detectClang();
  const manifest = readInstallManifest();

  if (manifest?.farExe && manifest?.clangExe && fs.existsSync(manifest.farExe)) {
    const cfg = getConfig();
    await cfg.update('compilerPath', manifest.farExe, vscode.ConfigurationTarget.Global);
    await cfg.update('clangPath', manifest.clangExe, vscode.ConfigurationTarget.Global);
    vscode.window.showInformationMessage(
      `Far configured from installer:\n${manifest.farExe}`,
    );
    return;
  }

  const compilerPath = await vscode.window.showInputBox({
    title: 'Far: Setup Compiler',
    prompt: 'Path to far.exe (or run install\\windows\\install-far.bat for auto setup)',
    value: current,
  });
  if (compilerPath === undefined) return;

  const clangPath = await vscode.window.showInputBox({
    title: 'Far: Clang path',
    prompt: 'Clang executable for LLVM backend',
    value: currentClang,
  });
  if (clangPath === undefined) return;

  const cfg = getConfig();
  await cfg.update('compilerPath', compilerPath, vscode.ConfigurationTarget.Global);
  await cfg.update('clangPath', clangPath, vscode.ConfigurationTarget.Global);

  try {
    const { code, stdout, stderr } = await runFar(['perf'], path.dirname(compilerPath === 'far' ? process.cwd() : compilerPath));
    if (code === 0) {
      vscode.window.showInformationMessage('Far compiler configured successfully.');
    } else {
      vscode.window.showWarningMessage(`Compiler responded with errors: ${stderr || stdout}`);
    }
  } catch (e) {
    vscode.window.showErrorMessage(`Could not run compiler: ${e.message}`);
  }

  await regenerateCoreApi();
}

function registerWindowsFileType(context) {
  if (process.platform !== 'win32') return;
  if (!getConfig().get('registerWindowsFileType', true)) return;

  const script = path.join(extensionDir(), 'scripts', 'register-far-windows.ps1');
  if (!fs.existsSync(script)) return;

  const run = () => {
    const child = spawn(
      'powershell.exe',
      ['-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', script, '-SourceDir', extensionDir(), '-Quiet'],
      { windowsHide: true },
    );
    child.on('error', () => { /* optional */ });
  };

  run();
  context.subscriptions.push(
    vscode.commands.registerCommand('far.registerWindowsFileType', () => {
      const child = spawn(
        'powershell.exe',
        ['-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', script, '-SourceDir', extensionDir()],
        { windowsHide: false },
      );
      child.on('close', (code) => {
        if (code === 0) {
          vscode.window.showInformationMessage('Windows .far file icon registered.');
        } else {
          vscode.window.showWarningMessage('Could not register .far file icon. Run install-extension.bat as user.');
        }
      });
    }),
  );
}

function registerSyntaxColors(context) {
  const mergeFarColors = async (uri) => {
    const cfg = vscode.workspace.getConfiguration('editor', uri);
    const current = cfg.get('tokenColorCustomizations') || {};
    const rules = Array.isArray(current.textMateRules) ? [...current.textMateRules] : [];
    const filtered = rules.filter((r) => !String(r.scope || '').startsWith('source.far '));
    const merged = [...filtered, ...FAR_TEXTMATE_RULES];
    await cfg.update(
      'tokenColorCustomizations',
      { ...current, textMateRules: merged },
      uri ? vscode.ConfigurationTarget.WorkspaceFolder : vscode.ConfigurationTarget.Workspace,
    );
  };

  const apply = () => {
    const folders = vscode.workspace.workspaceFolders;
    if (folders?.length) {
      for (const folder of folders) mergeFarColors(folder.uri);
    } else {
      mergeFarColors(undefined);
    }
  };

  apply();
  context.subscriptions.push(
    vscode.workspace.onDidOpenTextDocument((doc) => {
      if (doc.languageId === 'far') apply();
    }),
  );
}

function watchCoreModules(context) {
  const cpp = path.join(extensionDir(), '..', 'src', 'far_stdlib_modules.cpp');
  const apiFile = path.join(extensionDir(), 'data', 'core-api.json');
  const watchTargets = [cpp, apiFile].filter((p) => fs.existsSync(p));
  if (!watchTargets.length) return;
  let debounce;
  const refresh = () => {
    clearTimeout(debounce);
    debounce = setTimeout(async () => {
      if (fs.existsSync(cpp)) {
        await regenerateCoreApi();
      } else {
        coreApi = loadCoreApiFromDisk();
      }
    }, 800);
  };
  for (const target of watchTargets) {
    const watcher = fs.watch(target, refresh);
    context.subscriptions.push({ dispose: () => watcher.close() });
  }
}

function activate(context) {
  coreApi = loadCoreApiFromDisk();
  registerSyntaxColors(context);
  registerWindowsFileType(context);

  const runDoc = (args, filePath) => runFar(args, path.dirname(filePath));

  context.subscriptions.push(
    vscode.commands.registerCommand('far.runFile', runCurrentFile),
    vscode.commands.registerCommand('far.checkFile', () => {
      const ed = vscode.window.activeTextEditor;
      if (ed) return checkDocument(ed.document);
    }),
    vscode.commands.registerCommand('far.buildFile', buildCurrentFile),
    vscode.commands.registerCommand('far.setup', setupCompiler),
    vscode.commands.registerCommand('far.refreshIntelliSense', regenerateCoreApi),
    vscode.commands.registerCommand('far.formatFile', async () => {
      const ed = vscode.window.activeTextEditor;
      if (!ed || ed.document.languageId !== 'far') return;
      const edits = await formatDocument(ed.document);
      await ed.edit((eb) => { for (const e of edits) eb.replace(e.range, e.newText); });
    }),

    vscode.languages.registerCompletionItemProvider('far', {
      provideCompletionItems: (doc, pos) => {
        const cfg = getConfig();
        return intel.provideCompletions(doc, pos, ensureCoreApi(), {
          autoStdlib: cfg.get('autoStdlibCompletions', true),
          stdlibMinPrefix: cfg.get('stdlibCompletionMinPrefix', 1),
        });
      },
    }, '$', '.', ':', ' ', '{', '(', 'f', 'i', 'p', 'r', 'w'),

    vscode.languages.registerHoverProvider('far', {
      provideHover: (doc, pos) => intel.provideHover(doc, pos, ensureCoreApi(), runDoc),
    }),

    vscode.languages.registerSignatureHelpProvider('far', {
      provideSignatureHelp: (doc, pos) => intel.provideSignatureHelp(doc, pos, ensureCoreApi()),
    }, '(', ','),

    vscode.languages.registerDefinitionProvider('far', {
      provideDefinition: (doc, pos) => intel.provideDefinition(doc, pos, ensureCoreApi()),
    }),

    vscode.languages.registerDocumentSymbolProvider('far', {
      provideDocumentSymbols: (doc) => intel.provideDocumentSymbols(doc),
    }),

    vscode.languages.registerDocumentFormattingEditProvider('far', {
      provideDocumentFormattingEdits: (doc) => formatDocument(doc),
    }),

    diagnosticCollection,
  );

  context.subscriptions.push(
    vscode.workspace.onDidSaveTextDocument((doc) => {
      if (doc.languageId === 'far' && getConfig().get('checkOnSave', true)) {
        checkDocument(doc);
      }
    }),
  );

  context.subscriptions.push(
    vscode.workspace.onDidChangeTextDocument((e) => {
      if (e.document.languageId !== 'far') return;
      if (!getConfig().get('checkWhileTyping', true)) return;
      scheduleCheck(e.document);
    }),
  );

  // Check open Far documents on activation
  for (const doc of vscode.workspace.textDocuments) {
    if (doc.languageId === 'far' && getConfig().get('checkOnSave', true)) {
      scheduleCheck(doc);
    }
  }

  watchCoreModules(context);

  detectCompiler().then(async (compiler) => {
    const clang = await detectClang();
    const manifest = readInstallManifest();
    if (manifest?.farExe && fs.existsSync(manifest.farExe)) {
      const cfg = getConfig();
      if (!cfg.get('compilerPath')) {
        await cfg.update('compilerPath', manifest.farExe, vscode.ConfigurationTarget.Global);
      }
      if (!cfg.get('clangPath') || cfg.get('clangPath') === 'clang') {
        await cfg.update('clangPath', manifest.clangExe, vscode.ConfigurationTarget.Global);
      }
      return;
    }
    if (compiler === 'far' || !fs.existsSync(compiler)) {
      const found = await runFar(['perf'], process.cwd()).catch(() => null);
      if (!found || found.code !== 0) {
        vscode.window.showInformationMessage(
          'Far not installed. Run install\\windows\\install-far.bat from the repo, or use "Far: Setup Compiler".',
          'Setup',
        ).then((choice) => {
          if (choice === 'Setup') vscode.commands.executeCommand('far.setup');
        });
      }
    }
  });
}

function deactivate() {
  diagnosticCollection.dispose();
  for (const t of checkDebounce.values()) clearTimeout(t);
  checkDebounce.clear();
}

module.exports = { activate, deactivate };
