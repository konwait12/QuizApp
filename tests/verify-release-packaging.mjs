import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const read = relative => fs.readFileSync(path.join(root, relative), 'utf8');

const buildApk = read('scripts/build-apk.ps1');
const releaseBuilder = read('scripts/build-release-bank-assets.py');
const indexText = read('index.html');

assert.doesNotMatch(buildApk, /IncludePostgraduateBanks/);
assert.doesNotMatch(buildApk, /xiaoyi-question-banks/);
assert.doesNotMatch(buildApk, /postgraduate\//);
assert.match(buildApk, /\$sourceDataAssets/);
assert.match(buildApk, /\.Extension -notin @\("\.json"\)/);
assert.doesNotMatch(releaseBuilder, /xiaoyi-question-banks|POSTGRADUATE|publicBanks/);
assert.match(releaseBuilder, /source_root\.rglob\(".*\.json"\)/);
assert.match(releaseBuilder, /--dry-run/);
assert.match(releaseBuilder, /--extra-dir/);
assert.ok(indexText.includes("const LOCAL_POSTGRADUATE_REPORT = '';"));
assert.ok(!indexText.includes('./output/xiaoyi-question-banks/export-report.json'));
assert.doesNotMatch(indexText, /^  '考研数学\//m, 'removed Zhang Yu JSON paths must not remain in built-in bank lists');
assert.match(indexText, /LARGE_BANK_THRESHOLD/);

const dataRoot = path.join(root, 'data');
const dataFiles = [];
function walkJson(dir) {
  for (const entry of fs.readdirSync(dir, { withFileTypes: true })) {
    const full = path.join(dir, entry.name);
    if (entry.isDirectory()) walkJson(full);
    else if (entry.isFile() && entry.name.endsWith('.json')) dataFiles.push(full);
  }
}
walkJson(dataRoot);

const mathJsonFiles = dataFiles.filter(file => file.includes(`${path.sep}考研数学${path.sep}`));
assert.equal(dataFiles.length, 27, 'release bank source should keep the 27 original JSON files');
assert.equal(mathJsonFiles.length, 0, 'the discarded Zhang Yu per-question JSON banks must stay removed');

const expectedPdfSources = [
  ['做题本', '数学一-基础篇.pdf'],
  ['做题本', '数学一-强化篇.pdf'],
  ['做题本', '数学二-基础篇.pdf'],
  ['做题本', '数学二-强化篇.pdf'],
  ['做题本', '数学三-基础篇.pdf'],
  ['做题本', '数学三-强化篇.pdf'],
  ['解析', '数学一-解析.pdf'],
  ['解析', '数学二-解析.pdf'],
  ['解析', '数学三-解析.pdf'],
];

const builtInBlock = indexText.match(/const BUILTIN_MATH_PDF_DOCUMENTS = \[([\s\S]*?)\n\];/)?.[1] || '';
assert.equal((builtInBlock.match(/id: 'builtin-pdf:zhangyu:/g) || []).length, 9, 'the notebook library should register nine built-in math PDFs');

for (const [section, fileName] of expectedPdfSources) {
  const relative = path.posix.join('data', '考研数学', '_documents', section, fileName);
  const absolute = path.join(root, ...relative.split('/'));
  assert.ok(fs.existsSync(absolute), `${relative} should exist`);
  assert.ok(fs.statSync(absolute).size > 0, `${relative} should not be empty`);
  assert.ok(builtInBlock.includes(`url: '${relative}'`), `${relative} should be registered in BUILTIN_MATH_PDF_DOCUMENTS`);
}

console.log(JSON.stringify({
  bundledDataBanks: dataFiles.length,
  builtInMathPdfs: expectedPdfSources.length,
  removedPerQuestionMathBanks: true,
  apkCopiesNonJsonDataAssets: true,
  indexedDbThresholdPresent: true,
}, null, 2));
