#!/usr/bin/env node
/** Migrate aggregate instance methods and legacy Linalg/Vectors/Matrices to static geometry classes. */
import fs from 'fs';
import path from 'path';
import { fileURLToPath } from 'url';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');

const CTOR_CLASS = {
  vec2: 'Vec2', vec3: 'Vec3', vec4: 'Vec4',
  dvec2: 'DVec2', dvec3: 'DVec3', dvec4: 'DVec4',
  ivec2: 'IVec2', ivec3: 'IVec3', ivec4: 'IVec4',
  fpoint: 'Point', dpoint: 'DPoint',
  frect: 'Rect', drect: 'DRect',
  mat2: 'Mat2', mat3: 'Mat3', mat4: 'Mat4',
  dmat2: 'DMat2', dmat3: 'DMat3', dmat4: 'DMat4',
  quat: 'Quat', dquat: 'DQuat',
  color: 'Color', color32: 'Color32',
  bounds: 'Bounds', transform: 'Transform',
};

const VAR_TYPES = new Map();

function walk(dir, out = []) {
  for (const name of fs.readdirSync(dir)) {
    const full = path.join(dir, name);
    if (fs.statSync(full).isDirectory()) {
      if (name === 'node_modules' || name === '.git') continue;
      walk(full, out);
    } else if (name.endsWith('.far')) out.push(full);
  }
  return out;
}

function trackVars(text) {
  VAR_TYPES.clear();
  for (const m of text.matchAll(/\b(\w+)\s*=\s*(vec2|dvec2|vec3|dvec3|vec4|dvec4|ivec2|ivec3|ivec4|fpoint|dpoint|frect|drect|mat2|mat3|mat4|dmat2|dmat3|dmat4|quat|dquat|color|color32|bounds|transform)\s*\(/g)) {
    VAR_TYPES.set(m[1], m[2]);
  }
}

function classForReceiver(recv) {
  recv = recv.trim();
  const ctor = recv.match(/^(vec2|dvec2|vec3|dvec3|vec4|dvec4|ivec2|ivec3|ivec4|fpoint|dpoint|frect|drect|mat2|mat3|mat4|dmat2|dmat3|dmat4|quat|dquat|color|color32|bounds|transform)\s*\(/);
  if (ctor) return CTOR_CLASS[ctor[1]];
  if (VAR_TYPES.has(recv)) return CTOR_CLASS[VAR_TYPES.get(recv)];
  return null;
}

function migrateLegacyFacades(text) {
  return text
    .replace(/\bLinalg\.dot2\s*\(/g, 'Vec2.dot(')
    .replace(/\bLinalg\.len2\s*\(/g, 'Vec2.length(')
    .replace(/\bLinalg\.lerp2\s*\(/g, 'Vec2.lerp(')
    .replace(/\bVectors\.dot3\s*\(/g, 'Vec3.dot(')
    .replace(/\bVectors\.cross3\s*\(/g, 'Vec3.cross(')
    .replace(/\bVectors\.norm3\s*\(/g, 'Vec3.length(')
    .replace(/\bVectors\.dot_components\s*\(/g, 'Vec3.dot_components(')
    .replace(/\bMatrices\.det2\s*\(/g, 'Mat2.determinant(')
    .replace(/\bMatrices\.mul_vec2\s*\(/g, 'Mat2.mul(')
    .replace(/\bMatrices\.trace2\s*\(/g, 'Mat2.trace2(')
    .replace(/\bMath\.rect_from_xywh\s*\(/g, 'DRect.from_xywh(')
    .replace(/\bMath\.rect_union\s*\(/g, 'DRect.union_rect(');
}

function migrateInstanceMethods(text) {
  let out = text;
  // zero-arg on ctor: dvec2(1,2).length() -> DVec2.length(dvec2(1,2))
  for (const [ctor, cls] of Object.entries(CTOR_CLASS)) {
    const recvRe = new RegExp(`\\b${ctor}\\s*\\((?:[^()]|\\([^()]*\\))*\\)`, 'g');
    out = out.replace(new RegExp(`(${recvRe.source})\\.(\\w+)\\s*\\(\\s*\\)`, 'g'), `${cls}.$2($1)`);
    out = out.replace(new RegExp(`(${recvRe.source})\\.(\\w+)\\s*\\(([^)]*)\\)`, 'g'), `${cls}.$2($1, $3)`);
  }
  // variable receivers
  out = out.replace(/\b([a-zA-Z_]\w*)\.(\w+)\s*\(\s*\)/g, (all, recv, method) => {
    const cls = classForReceiver(recv);
    return cls ? `${cls}.${method}(${recv})` : all;
  });
  out = out.replace(/\b([a-zA-Z_]\w*)\.(\w+)\s*\(([^)]*)\)/g, (all, recv, method, args) => {
    const cls = classForReceiver(recv);
    return cls ? `${cls}.${method}(${recv}, ${args})` : all;
  });
  return out;
}

function migrateImports(text) {
  return text
    .replace(/^import\s+std\.linalg\s*$/gm, 'from vectors import Vec2')
    .replace(/^from\s+std\.linalg\s+import\s+Linalg\s*$/gm, 'from vectors import Vec2')
    .replace(/^import\s+std\.vectors\s*$/gm, 'from vectors import Vec3')
    .replace(/^from\s+std\.vectors\s+import\s+Vectors\s*$/gm, 'from vectors import Vec3')
    .replace(/^import\s+std\.matrices\s*$/gm, 'from matrices import Mat2')
    .replace(/^from\s+std\.matrices\s+import\s+Matrices\s*$/gm, 'from matrices import Mat2')
    .replace(/^import\s+std\.sci\.vectors\s*$/gm, 'from vectors import Vec3')
    .replace(/^import\s+std\.sci\.matrices\s*$/gm, 'from matrices import Mat2');
}

function ensureImports(text) {
  const needs = new Set();
  if (/\bVec2\./.test(text)) needs.add('from vectors import Vec2');
  if (/\bVec3\./.test(text)) needs.add('from vectors import Vec3');
  if (/\bDVec2\./.test(text)) needs.add('from vectors import DVec2');
  if (/\bDVec3\./.test(text)) needs.add('from vectors import DVec3');
  if (/\bDVec4\./.test(text)) needs.add('from vectors import DVec4');
  if (/\bMat2\./.test(text)) needs.add('from matrices import Mat2');
  if (/\bQuat\./.test(text)) needs.add('from quaternions import Quat');
  if (/\bDPoint\./.test(text)) needs.add('from points import DPoint');
  if (/\bDRect\./.test(text)) needs.add('from rects import DRect');
  if (needs.size === 0) return text;
  const lines = text.split('\n');
  for (const imp of needs) {
    if (!lines.some((l) => l.trim() === imp)) {
      let at = 0;
      for (let i = 0; i < lines.length; i++) {
        if (/^(import|from|package|module)\s/.test(lines[i])) at = i + 1;
        else if (lines[i].trim() && !lines[i].trim().startsWith('#')) break;
      }
      lines.splice(at, 0, imp);
    }
  }
  return lines.join('\n');
}

function migrateFile(file) {
  let text = fs.readFileSync(file, 'utf8');
  trackVars(text);
  text = migrateImports(text);
  text = migrateLegacyFacades(text);
  text = migrateInstanceMethods(text);
  text = ensureImports(text);
  fs.writeFileSync(file, text);
}

for (const file of [...walk(root), path.join(root, 'program.far')].filter((f) => fs.existsSync(f))) {
  if (file.includes('node_modules')) continue;
  migrateFile(file);
}
console.log('Geometry syntax migration complete');
