#!/usr/bin/env node
/** Regenerate tests/suite_max.far — see existing file for structure. */
import fs from 'fs';
import path from 'path';
import { fileURLToPath } from 'url';

const __dirname = path.dirname(fileURLToPath(import.meta.url));
const out = path.join(path.resolve(__dirname, '..'), 'tests', 'suite_max.far');

if (fs.existsSync(out)) {
  console.log('suite_max.far already exists:', out);
  process.exit(0);
}
console.error('suite_max.far missing; restore from git or copy from repo.');
process.exit(1);
