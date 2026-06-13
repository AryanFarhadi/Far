import { createRequire } from 'module';
import Module from 'module';

const require = createRequire(import.meta.url);
const originalLoad = Module._load;
Module._load = function (request, parent, isMain) {
  if (request === 'vscode') return require('./vscode-shim.js');
  return originalLoad.call(this, request, parent, isMain);
};

const intel = require('./intellisense.js');
const fs = require('fs');
const path = require('path');

const api = JSON.parse(fs.readFileSync(path.join(import.meta.dirname, 'data/core-api.json'), 'utf8'));

let passed = 0;
let failed = 0;

function assert(cond, msg) {
  if (cond) {
    passed++;
    console.log(`  ✓ ${msg}`);
  } else {
    failed++;
    console.error(`  ✗ ${msg}`);
  }
}

function varType(parsed, name) {
  const v = parsed.variables.filter((x) => x.name === name).pop();
  return v?.type;
}

function completionLabels(src, line, character) {
  const lines = src.split('\n');
  const doc = makeMockDoc(lines);
  const list = intel.provideCompletions(doc, { line, character }, api);
  return list.items.map((i) => i.label);
}

function makeMockDoc(lines) {
  const { Position, Range } = require('./vscode-shim.js');
  return {
    getText: (range) => {
      if (!range) return lines.join('\n');
      const slice = (line, start, end) => lines[line].slice(start, end);
      if (range.start.line === range.end.line) {
        return slice(range.start.line, range.start.character, range.end.character);
      }
      let out = slice(range.start.line, range.start.character, lines[range.start.line].length);
      for (let i = range.start.line + 1; i < range.end.line; i++) out += `\n${lines[i]}`;
      out += `\n${slice(range.end.line, 0, range.end.character)}`;
      return out;
    },
    lineAt: (line) => ({ text: lines[line] }),
    getWordRangeAtPosition: (pos) => {
      const line = lines[pos.line];
      const before = line.slice(0, pos.character);
      const m = before.match(/(\w+)$/);
      if (!m) return null;
      const startChar = pos.character - m[1].length;
      return new Range(new Position(pos.line, startChar), pos);
    },
    offsetAt: (pos) => {
      let off = 0;
      for (let i = 0; i < pos.line; i++) off += lines[i].length + 1;
      return off + pos.character;
    },
  };
}

console.log('Far IntelliSense tests\n');

const httpSrc = `from network import HttpClient

fun main() {
  cli = HttpClient("https://example.com")
  body = cli.fetch()
  print(body)
  return 0
}`;
const httpParsed = intel.parseDocument(httpSrc);
console.log('=== from import HttpClient ===');
console.log('imports:', JSON.stringify(httpParsed.imports));
assert(httpParsed.imports.length === 1, 'one import');
assert(httpParsed.imports[0].path === 'network', 'import path network');
assert(httpParsed.imports[0].symbols.includes('HttpClient'), 'imports HttpClient symbol');
assert(varType(httpParsed, 'cli') === 'HttpClient', 'cli:HttpClient');
assert(varType(httpParsed, 'body') === 'string', 'body:string');

const starParsed = intel.parseDocument('from math import *\nfun main() { return 0 }');
assert(starParsed.imports[0]?.fromStar === true, 'from import * sets fromStar');

const asParsed = intel.parseDocument(`from network import HttpClient as HC
fun main() {
  x = HC("http://x")
  return 0
}`);
assert(asParsed.imports[0]?.symbolEntries?.[0]?.alias === 'HC', 'import A as B parsed');
assert(varType(asParsed, 'x') === 'HttpClient', 'HC() resolves to HttpClient type');

const bareParsed = intel.parseDocument('import math\nfun main() { return 0 }');
assert(bareParsed.imports[0]?.alias === 'math', 'bare import math aliases to math');

const aliasParsed = intel.parseDocument(`import math as m
fun main() {
  x = m.sqrt(2.0)
  return 0
}`);
assert(aliasParsed.importMap.get('m')?.path === 'math', 'import math as m maps alias to math');
const modMembers = intel.getModuleDotMembers('m', '', aliasParsed, api);
assert(modMembers.some((m) => m.name === 'sqrt'), 'm. offers math.sqrt');
assert(modMembers.some((m) => m.name === 'sin'), 'm. offers math.sin');

assert(intel.parseDotAccess('  print(math.')?.qualifier === 'math', 'print(math. → qualifier math');
assert(intel.parseDotAccess('  m.')?.qualifier === 'm', 'm. → qualifier m');
assert(intel.parseDotAccess('  std.math.')?.qualifier === 'std.math', 'std.math. → full path');

const consoleNames = new Set(['input', 'len', 'print']);
const { Position } = require('./vscode-shim.js');

const mDotDoc = makeMockDoc(['import math as m', '', 'fun main() {', '    m.', '}']);
const mDotList = intel.provideCompletions(mDotDoc, new Position(3, 6), api);
const mDotLabels = mDotList.items.map((i) => (typeof i.label === 'string' ? i.label : i.label?.label ?? ''));
assert(mDotLabels.some((l) => l.startsWith('sqrt')), 'provideCompletions at m. includes sqrt');
assert(!mDotLabels.some((l) => consoleNames.has(l.replace(/\(.*/, ''))), 'provideCompletions at m. excludes console builtins');

const mathInPrintDoc = makeMockDoc([
  'import math', 'import vectors', '', 'fun main() {', '  print(math.)', '  return 0', '}',
]);
const mathDotList = intel.provideCompletions(mathInPrintDoc, new Position(4, 13), api);
const mathDotLabels = mathDotList.items.map((i) => (typeof i.label === 'string' ? i.label : i.label?.label ?? ''));
assert(mathDotLabels.some((l) => l.startsWith('sqrt')), 'provideCompletions at print(math. includes sqrt');
assert(!mathDotLabels.some((l) => consoleNames.has(l.replace(/\(.*/, ''))), 'print(math. excludes console builtins');

const matPrefixDoc = makeMockDoc([
  'import math', 'import vectors', '', 'fun main() {', '  x = mat', '  return 0', '}',
]);
const matPrefixList = intel.provideCompletions(matPrefixDoc, new Position(4, 9), api);
const matPrefixLabels = matPrefixList.items.map((i) => (typeof i.label === 'string' ? i.label : i.label?.label ?? ''));
assert(matPrefixLabels.includes('math'), 'typing mat suggests imported math module');
const mathIdx = matPrefixLabels.indexOf('math');
const mat2Idx = matPrefixLabels.indexOf('mat2(m00)');
assert(mathIdx >= 0 && (mat2Idx < 0 || mathIdx < mat2Idx), 'math ranks before mat2 when typing mat');

const printDoc = makeMockDoc(['import math', 'import vectors', '', 'fun main() {', '  x = vec2(23, 12)', '  print', '  return 0', '}']);
const printList = intel.provideCompletions(printDoc, new Position(5, 7), api);
const printItems = printList.items.filter((i) => {
  const label = typeof i.label === 'string' ? i.label : i.label?.label ?? '';
  return label.startsWith('print(');
});
assert(printItems.length === 1, 'only one print() completion');
assert(printItems[0].detail === 'built-in', 'print labeled built-in not console namespace');

const sinDoc = makeMockDoc(['import math', 'import vectors', '', 'fun main() {', '  sin', '  return 0', '}']);
const sinList = intel.provideCompletions(sinDoc, new Position(4, 5), api);
const sinItems = sinList.items.filter((i) => {
  const label = typeof i.label === 'string' ? i.label : i.label?.label ?? '';
  return label.startsWith('sin(');
});
assert(sinItems.length === 0, 'sin is not a global builtin — use math.sin');

const mathSinDoc = makeMockDoc(['import math', '', 'fun main() {', '  math.', '  return 0', '}']);
const mathSinList = intel.provideCompletions(mathSinDoc, new Position(3, 7), api);
const mathSinLabels = mathSinList.items.map((i) => (typeof i.label === 'string' ? i.label : i.label?.label ?? ''));
assert(mathSinLabels.some((l) => l.startsWith('sin')), 'math. offers sin');

const inpDoc = makeMockDoc(['import math', '', 'fun main() {', '  inp', '  return 0', '}']);
const inpList = intel.provideCompletions(inpDoc, new Position(3, 5), api);
const inpItems = inpList.items.filter((i) => {
  const label = typeof i.label === 'string' ? i.label : i.label?.label ?? '';
  return label.startsWith('input(');
});
assert(inpItems.length === 1, 'only one input() completion when typing name');
assert(inpItems[0].detail?.includes('2 overloads'), 'input detail mentions overload count');

const inpTypeDoc = makeMockDoc(['', 'fun main() {', '  inp', '  return 0', '}']);
const inpTypeList = intel.provideCompletions(inpTypeDoc, new Position(2, 5), api);
const inputTypeItems = inpTypeList.items.filter((i) => {
  const label = typeof i.label === 'string' ? i.label : i.label?.label ?? '';
  return label === 'input';
});
assert(inputTypeItems.length === 0, 'no input stdlib type alongside input() builtin');

const inpSigDoc = makeMockDoc(['', 'fun main() {', '  input(', '  return 0', '}']);
const inpSig = intel.provideSignatureHelp(inpSigDoc, new Position(2, 8), api);
assert(inpSig?.signatures?.length === 2, 'input( shows both overloads in signature help');

const inCallDoc = makeMockDoc(['', 'fun main() {', '  input( ', '  return 0', '}']);
const inCallList = intel.provideCompletions(inCallDoc, new Position(2, 8), api);
const inCallInput = inCallList.items.filter((i) => {
  const label = typeof i.label === 'string' ? i.label : i.label?.label ?? '';
  return label.startsWith('input(');
});
assert(inCallInput.length === 0, 'no duplicate input completion inside input(');

const wDoc = makeMockDoc(['', 'fun main() {', '  w', '  return 0', '}']);
const wList = intel.provideCompletions(wDoc, new Position(2, 3), api);
const writeItems = wList.items.filter((i) => {
  const label = typeof i.label === 'string' ? i.label : i.label?.label ?? '';
  const bare = label.replace(/\(.*/, '');
  return bare === 'write' || bare.startsWith('write_');
});
assert(writeItems.length === 0, 'no global write* — use print()');

const inputAssignSrc = `import math
import vectors

fun main() {
  name = input("Enter your name: ")
  return 0
}`;
const inputAssignParsed = intel.parseDocument(inputAssignSrc);
assert(varType(inputAssignParsed, 'name') === 'string', 'input("...") infers string not input class');

const nameDotDoc = makeMockDoc([
  'import math', 'import vectors', '', 'fun main() {',
  '  name = input("Enter your name: ")', '  name.', '  return 0', '}',
]);
const nameDotList = intel.provideCompletions(nameDotDoc, new Position(5, 7), api);
const ioMemberLabels = nameDotList.items.map((i) => (typeof i.label === 'string' ? i.label : i.label?.label ?? ''));
assert(!ioMemberLabels.some((l) => l === 'read_f64' || l === 'read_line' || l.startsWith('input')), 'name:string does not offer input class methods');

const nameDotLabels = nameDotList.items.map((i) => (typeof i.label === 'string' ? i.label : i.label?.label ?? ''));
assert(nameDotLabels.some((l) => l === 'split' || l.startsWith('split')), 'name. offers string.split');
assert(nameDotLabels.some((l) => l === 'tolower' || l.startsWith('tolower')), 'name. offers string.tolower');
assert(nameDotLabels.some((l) => l === 'toupper' || l.startsWith('toupper')), 'name. offers string.toupper');
assert(nameDotLabels.some((l) => l === 'trim' || l.startsWith('trim')), 'name. offers string.trim');
assert(!nameDotLabels.some((l) => l === 'len' || l.startsWith('len(')), 'name. has no len() — use len(name)');

const inCallEmptyDoc = makeMockDoc(['', 'fun main() {', '  input(,', '  return 0', '}']);
const inCallEmptyList = intel.provideCompletions(inCallEmptyDoc, new Position(2, 8), api);
assert(inCallEmptyList.items.length === 0, 'inside input( suppresses completion list for signature help');
assert(!inpList.items.some((i) => {
  const label = typeof i.label === 'string' ? i.label : i.label?.label ?? '';
  return label.startsWith('read_line');
}), 'no read_line in IO builtins');

const netParsed = intel.parseDocument(`import network
fun main() {
  c = HttpClient("http://x")
  return 0
}`);
assert(intel.visibleImportedTypeNames(netParsed, api).has('HttpClient'), 'HttpClient in scope after import network');
assert(varType(netParsed, 'c') === 'HttpClient', 'HttpClient constructor infers type');

const dvecSrc = `import vectors

fun main() {
  a = dvec2(0.0, 0.0)
  b = dvec2(3.0, 4.0)
  d = vectors.distance(a, b)
  print(d)
  return 0
}`;
const dvecParsed = intel.parseDocument(dvecSrc);
console.log('\n=== vectors.distance ===');
console.log('variables:', dvecParsed.variables.map((v) => `${v.name}:${v.type ?? '?'}`).join(', '));
assert(!intel.visibleImportedTypeNames(dvecParsed, api).has('distance'), 'distance is not a type name');
assert(intel.visibleImportedTypeNames(dvecParsed, api).has('dvec2'), 'dvec2 is a type after import vectors');
assert(varType(dvecParsed, 'a') === 'dvec2', 'a:dvec2');
assert(varType(dvecParsed, 'd') === 'f64', 'd:f64 for distance example');

const vecModMembers = intel.getModuleDotMembers('vectors', '', dvecParsed, api);
assert(vecModMembers.some((m) => m.name === 'distance'), 'vectors. offers distance');
assert(vecModMembers.some((m) => m.name === 'dot'), 'vectors. offers dot');

const members = intel.getTypeMembers(dvecParsed, 'dvec2', api);
assert(!members.find((m) => m.name === 'distance'), 'dvec2 type no longer exposes distance');

assert(intel.resolveInstanceTypeName('a', dvecParsed, 5, api) === 'dvec2', 'resolveInstanceTypeName for variable a');
assert(intel.resolveInstanceTypeName('dvec2', dvecParsed, 1, api) === 'dvec2', 'resolveInstanceTypeName for type dvec2');

const typedParsed = intel.parseDocument(`import vectors
fun main() {
  a: dvec2 = dvec2(10, 20)
  b: dvec2 = dvec2(30, 40)
  d = vectors.distance(a, b)
  return 0
}`);
assert(varType(typedParsed, 'd') === 'f64', 'typed vectors.distance returns f64');

const foreachParsed = intel.parseDocument(`fun main() {
  ages = [10, 20, 30]
  for age in ages {
    print(age)
  }
  return 0
}`);
assert(foreachParsed.variables.some((v) => v.name === 'ages' && v.type === 'i32[]'), 'array literal typed i32[]');
assert(varType(foreachParsed, 'age') === 'i32', 'for-in loop variable typed from collection element');

assert(intel.inferCollectionElemType('i64[]') === 'i64', 'infer elem from i64[]');
assert(intel.isCollectionTypeName('i64[]'), 'i64[] is collection type');

const forBlockSrc = `fun main() {
  ages = [10, 20, 30, 40, 50]
  for
  return 0
}`;
const forLabels = completionLabels(forBlockSrc, 2, 5);
assert(forLabels.some((l) => l.includes('for i in 0..n')), 'indented for shows range loop snippet');
assert(forLabels.some((l) => l.includes('for item in collection')), 'indented for shows collection loop snippet');

const forAgeSrc = `fun main() {
  ages = [10, 20, 30]
  for age
  return 0
}`;
const forAgeLabels = completionLabels(forAgeSrc, 2, 9);
assert(forAgeLabels.some((l) => l.includes('in collection')), 'for age suggests in continuation');

const forInLabels = completionLabels(`fun main() {
  ages = [10, 20, 30]
  for age in
  return 0
}`, 2, 12);
assert(forInLabels.includes('ages'), 'for age in suggests collection variable ages');

const whileLabels = completionLabels(`fun main() {
  while
}`, 1, 7);
assert(whileLabels.some((l) => l.includes('while (cond)')), 'while shows loop snippet');

const vecMinDotDoc = makeMockDoc(['import vectors', '', 'fun main() {', '  vectors.', '  return 0', '}']);
const vecMinDotList = intel.provideCompletions(vecMinDotDoc, new Position(3, 10), api);
const vecMinItem = vecMinDotList.items.find((i) => {
  const label = typeof i.label === 'string' ? i.label : i.label?.label ?? '';
  return label === 'min()';
});
assert(vecMinItem, 'vectors. offers grouped min()');
assert(vecMinItem.detail?.includes('overloads'), 'vectors.min detail shows overload count');

const vecMinSigDoc = makeMockDoc(['import vectors', '', 'fun main() {', '  vectors.min(', '  return 0', '}']);
const vecMinSig = intel.provideSignatureHelp(vecMinSigDoc, new Position(3, 14), api);
assert((vecMinSig?.signatures?.length ?? 0) > 1, 'vectors.min( shows all overloads in signature help');

const mathSigDoc = makeMockDoc(['import math as m', '', 'fun main() {', '  m.sqrt(', '  return 0', '}']);
const mathSig = intel.provideSignatureHelp(mathSigDoc, new Position(3, 9), api);
assert(mathSig?.signatures?.length === 1, 'm.sqrt( shows signature help');
assert(mathSig.signatures[0].label.includes('sqrt'), 'sqrt signature label');

console.log(`\n${passed} passed, ${failed} failed`);
process.exit(failed > 0 ? 1 : 0);
