#!/usr/bin/env node
/**
 * Generate geometry category modules (vectors, matrices, …) from aggregate-types.json.
 * Run: node tools/generate-geom-classes.mjs
 */
import fs from 'fs';
import path from 'path';
import { fileURLToPath } from 'url';

const __dirname = path.dirname(fileURLToPath(import.meta.url));
const root = path.resolve(__dirname, '..');
const aggregateJson = path.join(root, 'vscode', 'data', 'aggregate-types.json');
const cppPath = path.join(root, 'src', 'far_stdlib_modules.cpp');

const BEGIN = '// === GEOM_MODULES_BEGIN ===';
const END = '// === GEOM_MODULES_END ===';
const MAP_BEGIN = '// === FLAT_MODULE_MAP_BEGIN ===';
const MAP_END = '// === FLAT_MODULE_MAP_END ===';

const PUBLIC_ALIASES = {
  fvec2: 'vec2', fvec3: 'vec3', fvec4: 'vec4',
  fpoint: 'point', frect: 'rect',
};

const SCALAR_NAMES = { F32: 'f32', F64: 'f64', I32: 'i32', U8: 'u8' };

const METHODS_BY_KIND = {
  Vec: [
    ['length', 0, 'f64'], ['length2', 0, 'f64'], ['dot', 1, 'f64', 'self'],
    ['distance', 1, 'f64', 'self'], ['distance2', 1, 'f64', 'self'], ['normalize', 0, 'self'],
    ['min', 1, 'self'], ['max', 1, 'self'], ['clamp', 2, 'self'], ['approx_eq', 2, 'bool', 'self'],
    ['cross', 1, 'self', 'self', 3],
  ],
  IVec: [
    ['length', 0, 'f64'], ['length2', 0, 'i64'], ['dot', 1, 'i64', 'self'],
    ['min', 1, 'self'], ['max', 1, 'self'], ['cross', 1, 'self', 'self', 3],
  ],
  Point: [
    ['length', 0, 'f64'], ['length2', 0, 'f64'], ['dot', 1, 'f64', 'self'],
    ['distance', 1, 'f64', 'self'], ['distance2', 1, 'f64', 'self'], ['normalize', 0, 'self'],
    ['min', 1, 'self'], ['max', 1, 'self'], ['clamp', 2, 'self'], ['approx_eq', 2, 'bool', 'self'],
    ['distance_to', 1, 'f64', 'self'], ['translate', 1, 'self'],
  ],
  Rect: [
    ['width', 0, 'f64'], ['height', 0, 'f64'], ['center', 0, 'point'],
    ['contains', 1, 'bool', 'point'], ['intersects', 1, 'bool', 'self'],
    ['expand', 1, 'self'], ['area', 0, 'f64'],
  ],
  Mat: [['transpose', 0, 'self'], ['determinant', 0, 'f64'], ['mul', 1, 'vec', 'vec']],
  Quat: [
    ['length', 0, 'f64'], ['length2', 0, 'f64'], ['dot', 1, 'f64', 'self'],
    ['normalize', 0, 'self'], ['mul', 1, 'self'],
  ],
  Color: [
    ['min', 1, 'self'], ['max', 1, 'self'], ['clamp', 2, 'self'], ['approx_eq', 2, 'bool', 'self'],
  ],
  Color32: [['to_color', 0, 'color']],
  Bounds: [
    ['contains', 1, 'bool', 'fvec3'], ['intersects', 1, 'bool', 'self'],
    ['expand', 1, 'self'], ['center', 0, 'fvec3'], ['size', 0, 'fvec3'],
  ],
};

const VEC_BY_SCALAR_DIM = {
  f32: { 2: 'vec2', 3: 'vec3', 4: 'vec4' },
  f64: { 2: 'dvec2', 3: 'dvec3', 4: 'dvec4' },
  i32: { 2: 'ivec2', 3: 'ivec3', 4: 'ivec4' },
};

const POINT_BY_SCALAR = { f32: 'point', f64: 'dpoint' };

function typeFields(type) {
  return type.members.filter((m) => m.kind === 'field').map((m) => m.name);
}

function resolveMethodArg(argKind, type) {
  const pub = publicType(type);
  if (argKind === 'self') return pub;
  if (argKind === 'point') return POINT_BY_SCALAR[type.scalar] ?? 'dpoint';
  if (argKind === 'vec') {
    const matDim = type.name.includes('mat2') || type.name === 'dmat2' ? 2
      : type.name.includes('mat3') || type.name === 'dmat3' ? 3 : 4;
    return VEC_BY_SCALAR_DIM[type.scalar]?.[matDim] ?? 'vec2';
  }
  if (argKind === 'fvec3') return 'vec3';
  if (argKind === 'color') return 'color';
  return argKind;
}

function resolveMethodReturn(retKind, type) {
  const pub = publicType(type);
  if (retKind === 'self') return pub;
  if (retKind === 'point') return POINT_BY_SCALAR[type.scalar] ?? 'dpoint';
  if (retKind === 'vec') {
    const matDim = type.name.includes('mat2') || type.name === 'dmat2' ? 2
      : type.name.includes('mat3') || type.name === 'dmat3' ? 3 : 4;
    return VEC_BY_SCALAR_DIM[type.scalar]?.[matDim] ?? 'vec2';
  }
  if (retKind === 'fvec3') return 'vec3';
  if (retKind === 'color') return 'color';
  return retKind;
}

function buildInstanceMethodSignature(type, method) {
  const [name, argc, retKind, argKind, minDim] = method;
  const fields = typeFields(type);
  if (minDim && fields.length < minDim) return null;
  const ret = resolveMethodReturn(retKind, type);
  const params = [];
  for (let i = 0; i < argc; i++) {
    params.push(`${i === 0 ? 'other' : i === 1 ? 'hi' : 'arg'}: ${resolveMethodArg(argKind ?? 'self', type)}`);
  }
  if (name === 'clamp') {
    params[0] = `lo: ${publicType(type)}`;
    params[1] = `hi: ${publicType(type)}`;
  }
  if (name === 'approx_eq') {
    params[0] = `other: ${publicType(type)}`;
    params[1] = 'eps: f64';
  }
  const paramStr = params.join(', ');
  return `def ${name}(${paramStr}) -> ${ret}`;
}

const GEOM_CATEGORIES = {
  vectors: ['fvec2', 'fvec3', 'fvec4', 'dvec2', 'dvec3', 'dvec4', 'ivec2', 'ivec3', 'ivec4'],
  matrices: ['mat2', 'mat3', 'mat4', 'dmat2', 'dmat3', 'dmat4'],
  points: ['fpoint', 'dpoint'],
  rects: ['frect', 'drect'],
  quaternions: ['quat', 'dquat'],
  colors: ['color', 'color32'],
  bounds: ['bounds'],
  transforms: ['transform'],
};

const GEOM_CATEGORY_NAMES = new Set(Object.keys(GEOM_CATEGORIES));

function publicType(type) {
  return PUBLIC_ALIASES[type.name] ?? type.name;
}

function remapTypeName(typeName, ownerType) {
  if (typeName === ownerType.name) return publicType(ownerType);
  for (const [from, to] of Object.entries(PUBLIC_ALIASES)) {
    if (typeName === from) return to;
  }
  return typeName;
}

function className(type) {
  return publicType(type);
}

function parseMethodSignature(sig) {
  const m = sig.match(/^def\s+(\w+)\(([^)]*)\)\s*->\s*(\S+)/);
  if (!m) return null;
  const params = m[2]
    ? m[2].split(',').map((p) => {
        const [n, t] = p.trim().split(':').map((s) => s.trim());
        return { name: n, type: t };
      })
    : [];
  return { name: m[1], params, ret: m[3] };
}

function toStaticSignature(ownerType, method) {
  const typeName = publicType(ownerType);
  const parsed = parseMethodSignature(method.signature);
  if (!parsed) return null;
  const { name, params, ret } = parsed;
  const staticParams = [{ name: 'v', type: typeName }, ...params.map((p) => ({
    ...p,
    type: remapTypeName(p.type, ownerType),
  }))];
  if (name === 'clamp' && params.length === 2) {
    staticParams[0] = { name: 'v', type: typeName };
    staticParams[1] = { name: 'lo', type: typeName };
    staticParams[2] = { name: 'hi', type: typeName };
  } else if (params.length === 1) {
    staticParams[0] = { name: 'a', type: typeName };
    staticParams[1] = {
      name: params[0].name === 'other' ? 'b' : params[0].name,
      type: remapTypeName(params[0].type, ownerType),
    };
  } else if (params.length === 0) {
    staticParams[0] = { name: 'v', type: typeName };
  } else if (name === 'approx_eq' && params.length === 2) {
    staticParams[0] = { name: 'a', type: typeName };
    staticParams[1] = { name: 'b', type: typeName };
    staticParams[2] = params[1];
  }
  const paramStr = staticParams.map((p) => `${p.name}: ${p.type}`).join(', ');
  const retType = remapTypeName(ret, ownerType);
  return { name, sig: `${name}(${paramStr}) -> ${retType}` };
}

const EXTRAS = {
  fvec2: [
    { sig: 'lerp(a: vec2, b: vec2, t: f64) -> vec2', body: 'return vec2(lerp(a.x, b.x, t), lerp(a.y, b.y, t))' },
    { sig: 'reflect_v(v: vec2, n: vec2) -> vec2', body: 'return v' },
    { sig: 'angle(v: vec2) -> f64', body: 'return 0.0' },
  ],
  dvec2: [
    { sig: 'lerp(a: dvec2, b: dvec2, t: f64) -> dvec2', body: 'return dvec2(lerp(a.x, b.x, t), lerp(a.y, b.y, t))' },
  ],
  dvec3: [
    { sig: 'lerp(a: dvec3, b: dvec3, t: f64) -> dvec3', body: 'return dvec3(lerp(a.x, b.x, t), lerp(a.y, b.y, t), lerp(a.z, b.z, t))' },
  ],
  fvec3: [
    { sig: 'lerp(a: vec3, b: vec3, t: f64) -> vec3', body: 'return vec3(lerp(a.x, b.x, t), lerp(a.y, b.y, t), lerp(a.z, b.z, t))' },
    { sig: 'dot_components(ax: f64, ay: f64, az: f64, bx: f64, by: f64, bz: f64) -> f64', body: 'return sci_v3_dot(ax, ay, az, bx, by, bz)' },
  ],
  mat2: [
    { sig: 'trace2(m00: f64, m01: f64, m10: f64, m11: f64) -> f64', body: 'return sci_mat2_trace(m00, m01, m10, m11)' },
  ],
  drect: [
    { sig: 'from_xywh(x: f64, y: f64, w: f64, h: f64) -> drect', body: 'return rect_from_xywh(x, y, w, h)' },
    { sig: 'union_rect(a: drect, b: drect) -> drect', body: 'return rect_union(a, b)' },
  ],
};

function stubBody(staticSig, typeName) {
  const m = staticSig.match(/(\w+)\(([^)]*)\)\s*->\s*(\S+)/);
  if (!m) return 'return 0.0';
  const params = m[2] ? m[2].split(',').map((s) => s.trim().split(':')[0].trim()) : [];
  const ret = m[3];
  if (ret === typeName && params.length > 0) return `return ${params[0]}`;
  if (ret === 'bool') return 'return false';
  if (ret === typeName) return `return ${params[0] ?? typeName + '(0.0)'}`;
  if (ret === 'vec2' || ret === 'fvec2') return 'return vec2(0.0, 0.0)';
  if (ret === 'vec3' || ret === 'fvec3') return 'return vec3(0.0, 0.0, 0.0)';
  if (ret === 'vec4' || ret === 'fvec4') return 'return vec4(0.0, 0.0, 0.0, 0.0)';
  if (ret === 'dvec2') return 'return dvec2(0.0, 0.0)';
  if (ret === 'dvec3') return 'return dvec3(0.0, 0.0, 0.0)';
  if (ret === 'dvec4') return 'return dvec4(0.0, 0.0, 0.0, 0.0)';
  if (ret === 'ivec2') return 'return ivec2(0, 0)';
  if (ret === 'ivec3') return 'return ivec3(0, 0, 0)';
  if (ret === 'ivec4') return 'return ivec4(0, 0, 0, 0)';
  if (ret === 'fpoint' || ret === 'point') return 'return fpoint(0.0, 0.0)';
  if (ret === 'dpoint') return 'return dpoint(0.0, 0.0)';
  if (ret === 'frect' || ret === 'rect') return 'return rect(0.0, 0.0, 0.0, 0.0)';
  if (ret === 'drect') return 'return drect(0.0, 0.0, 0.0, 0.0)';
  if (ret === 'color') return 'return color(0.0, 0.0, 0.0, 0.0)';
  if (ret === 'bounds') return 'return bounds(0.0, 0.0, 0.0, 0.0, 0.0, 0.0)';
  if (ret === 'i64' || ret === 'i32' || ret === 'u64' || ret === 'u32' || ret === 'bool') return 'return 0';
  if (ret === 'f64' || ret === 'f32') return 'return 0.0';
  return 'return 0.0';
}

function generateTypeClassBlock(type) {
  const cls = className(type);
  return `public class ${cls} {}`;
}

function generateNamespaceFacade(category, types) {
  /** @type {Map<string, { sig: string, stub: string }[]>} */
  const methodMap = new Map();

  const addMethod = (name, sig, stub) => {
    if (!methodMap.has(name)) methodMap.set(name, []);
    methodMap.get(name).push({ sig, stub });
  };

  for (const type of types) {
    const pub = publicType(type);
    const specs = METHODS_BY_KIND[type.kind] ?? [];
    for (const spec of specs) {
      const instSig = buildInstanceMethodSignature(type, spec);
      if (!instSig) continue;
      const st = toStaticSignature(type, { signature: instSig });
      if (!st) continue;
      addMethod(st.name, st.sig, stubBody(st.sig, pub));
    }
    for (const extra of EXTRAS[type.name] ?? []) {
      const name = extra.sig.match(/^(\w+)/)?.[1];
      if (!name) continue;
      addMethod(name, extra.sig, extra.body);
    }
  }

  const lines = [`public class ${category} {`];
  for (const overloads of methodMap.values()) {
    for (const { sig, stub } of overloads) {
      lines.push(`  public static fun ${sig} { ${stub} }`);
    }
  }
  lines.push('}');
  return lines.join('\n');
}

function generateClassBlock(type) {
  return generateTypeClassBlock(type);
}

function generateCategoryModule(category, types) {
  const blocks = [];
  const exports = [];
  for (const type of types) {
    const cls = className(type);
    if (cls === category) {
      exports.push(cls);
      continue;
    }
    blocks.push(generateTypeClassBlock(type));
    exports.push(cls);
  }
  blocks.push(generateNamespaceFacade(category, types));
  if (!exports.includes(category)) exports.push(category);
  return {
    var: `kStdlibMod_${category}`,
    body: `static const char kStdlibMod_${category}[] = R"FAR_STDLIB(package far

module ${category}

${blocks.join('\n\n')}

export ${exports.join(', ')}
)FAR_STDLIB";`,
    map: `    {"${category}", kStdlibMod_${category}},`,
  };
}

function main() {
  const data = JSON.parse(fs.readFileSync(aggregateJson, 'utf8'));
  const byName = new Map(data.types.map((t) => [t.name, t]));

  const modules = [];
  const mapLines = [];
  for (const [category, typeNames] of Object.entries(GEOM_CATEGORIES)) {
    const types = typeNames.map((n) => byName.get(n)).filter(Boolean);
    const mod = generateCategoryModule(category, types);
    modules.push(mod.body);
    mapLines.push(mod.map);
  }

  const modulesBlock = modules.join('\n\n');
  const mapBlock = mapLines.join('\n');

  let cpp = fs.readFileSync(cppPath, 'utf8');

  // Remove misplaced duplicate geom modules (between json and log).
  cpp = cpp.replace(
    /\nstatic const char kStdlibMod_vectors\[\][\s\S]*?static const char kStdlibMod_transforms\[\][\s\S]*?\)FAR_STDLIB";\n\n(?=static const char kStdlibMod_log)/,
    '\n',
  );

  cpp = cpp.replace(/\r?\n  public static fun rect_from_xywh[^\n]+\n/g, '');
  cpp = cpp.replace(/\r?\n  public static fun rect_union[^\n]+\n/g, '');

  if (cpp.includes(BEGIN)) {
    cpp = cpp.replace(new RegExp(`${BEGIN}[\\s\\S]*?${END}`), `${BEGIN}\n${modulesBlock}\n${END}`);
  } else {
    const anchor = 'static const char* primaryStdlibModuleSource';
    cpp = cpp.replace(anchor, `${BEGIN}\n${modulesBlock}\n${END}\n\n${anchor}`);
  }

  if (cpp.includes(MAP_BEGIN)) {
    const flatMapMatch = cpp.match(new RegExp(`${MAP_BEGIN}[\\s\\S]*?${MAP_END}`));
    const kept = flatMapMatch[0]
      .replace(`${MAP_BEGIN}\n`, '')
      .replace(`\n${MAP_END}`, '')
      .split('\n')
      .filter((line) => {
        const m = line.match(/\{"(\w+)"/);
        if (!m) return false;
        return !GEOM_CATEGORY_NAMES.has(m[1]);
      });
    cpp = cpp.replace(
      new RegExp(`${MAP_BEGIN}[\\s\\S]*?${MAP_END}`),
      `${MAP_BEGIN}\n${kept.join('\n')}\n${mapBlock}\n${MAP_END}`,
    );
  }

  fs.writeFileSync(cppPath, cpp);
  console.log(`Generated ${Object.keys(GEOM_CATEGORIES).length} geometry category modules`);
}

main();
