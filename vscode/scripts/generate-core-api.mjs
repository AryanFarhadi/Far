#!/usr/bin/env node
import { spawnSync } from 'child_process';
import path from 'path';
import { fileURLToPath } from 'url';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../..');
const script = path.join(root, 'tools', 'generate-core-api.mjs');
const r = spawnSync(process.execPath, [script], { cwd: root, stdio: 'inherit' });
process.exit(r.status ?? 1);
