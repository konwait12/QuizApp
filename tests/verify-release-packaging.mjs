import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const read = relative => fs.readFileSync(path.join(root, relative), 'utf8');
const buildApk = read('scripts/build-apk.ps1');
const releaseBuilder = read('scripts/build-release-bank-assets.py');
const report = JSON.parse(read('output/xiaoyi-question-banks/export-report.json'));

assert.match(buildApk, /\[switch\]\$IncludePostgraduateBanks/);
assert.match(buildApk, /if \(\$IncludePostgraduateBanks -and \(Test-Path -LiteralPath \$postgraduateReport\)\)/);
assert.match(releaseBuilder, /report\.get\("publicBanks"\)/);
assert.doesNotMatch(releaseBuilder, /report\.get\("restrictedBanks"\)/);
assert.match(releaseBuilder, /--dry-run/);

const dataRoot = path.join(root, 'data');
const dataFiles = fs.readdirSync(dataRoot, { recursive: true, withFileTypes: true })
  .filter(entry => entry.isFile() && entry.name.endsWith('.json'));
const publicSections = (report.publicBanks || []).flatMap(group =>
  (group.chapters || []).flatMap(chapter => chapter.sections || []));
assert.ok(publicSections.length > 0, 'public postgraduate release list should not be empty');
assert.ok((report.restrictedBanks || []).length > 0, 'fixture should include restricted banks to verify exclusion');

let publicBytes = 0;
for (const section of publicSections) {
  const source = path.resolve(root, 'output/xiaoyi-question-banks', section.file);
  const postgraduateRoot = path.resolve(root, 'output/xiaoyi-question-banks');
  assert.ok(source.startsWith(`${postgraduateRoot}${path.sep}`), `unsafe public bank path: ${section.file}`);
  assert.ok(fs.statSync(source).isFile(), `missing public bank file: ${section.file}`);
  publicBytes += fs.statSync(source).size;
}

console.log(JSON.stringify({
  bundledBaseBanks: dataFiles.length,
  releasePostgraduateBanks: publicSections.length,
  releasePostgraduateMB: Math.round(publicBytes / 1024 / 1024 * 100) / 100,
  restrictedBanksExcluded: report.restrictedBanks.length,
  defaultApkExcludesPostgraduate: true,
  indexedDbThresholdPresent: /LARGE_BANK_THRESHOLD/.test(read('index.html')),
}, null, 2));
