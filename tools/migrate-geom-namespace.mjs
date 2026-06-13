#!/usr/bin/env node
/**
 * Migrate Type.method(...) geom calls to namespace.method(...).
 * Run: node tools/migrate-geom-namespace.mjs
 */
import fs from 'fs';
import path from 'path';
import { fileURLToPath } from 'url';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');

const REPLACEMENTS = [
  ['dvec4.', 'vectors.'],
  ['dvec3.', 'vectors.'],
  ['dvec2.', 'vectors.'],
  ['ivec4.', 'vectors.'],
  ['ivec3.', 'vectors.'],
  ['ivec2.', 'vectors.'],
  ['fvec4.', 'vectors.'],
  ['fvec3.', 'vectors.'],
  ['fvec2.', 'vectors.'],
  ['vec4.', 'vectors.'],
  ['vec3.', 'vectors.'],
  ['vec2.', 'vectors.'],
  ['dpoint.', 'points.'],
  ['fpoint.', 'points.'],
  ['point.', 'points.'],
  ['drect.', 'rects.'],
  ['frect.', 'rects.'],
  ['rect.', 'rects.'],
  ['dmat4.', 'matrices.'],
  ['dmat3.', 'matrices.'],
  ['dmat2.', 'matrices.'],
  ['mat4.', 'matrices.'],
  ['mat3.', 'matrices.'],
  ['mat2.', 'matrices.'],
  ['dquat.', 'quaternions.'],
  ['quat.', 'quaternions.'],
  ['color32.', 'colors.'],
  ['color.', 'colors.'],
  ['transform.', 'transforms.'],
];

function migrateFile(filePath) {
  let text = fs.readFileSync(filePath, 'utf8');
  let changed = false;
  for (const [from, to] of REPLACEMENTS) {
    if (text.includes(from)) {
      text = text.split(from).join(to);
      changed = true;
    }
  }
  if (changed) fs.writeFileSync(filePath, text);
  return changed;
}

function walk(dir, out = []) {
  for (const ent of fs.readdirSync(dir, { withFileTypes: true })) {
    const p = path.join(dir, ent.name);
    if (ent.isDirectory()) walk(p, out);
    else if (ent.name.endsWith('.far')) out.push(p);
  }
  return out;
}

const targets = [
  path.join(root, 'program.far'),
  ...walk(path.join(root, 'examples')),
];

let count = 0;
for (const f of targets) {
  if (fs.existsSync(f) && migrateFile(f)) {
    console.log('updated', path.relative(root, f));
    count++;
  }
}
console.log(`Migrated ${count} file(s)`);
