import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';

const root = path.resolve(import.meta.dirname, '..');
const legacyPath = path.join(root, 'output', 'apk-inspect-v1.0.18', 'assets', 'index.html');
const legacy = fs.readFileSync(legacyPath, 'utf8');
const currentIndex = fs.readFileSync(path.join(root, 'index.html'), 'utf8');
const currentSources = [
  currentIndex,
  fs.readFileSync(path.join(root, 'shell', 'main-hub.js'), 'utf8'),
  fs.readFileSync(path.join(root, 'exam', 'exam-ui.js'), 'utf8'),
  fs.readFileSync(path.join(root, 'review', 'review-ui.js'), 'utf8'),
  fs.readFileSync(path.join(root, 'review', 'fsrs-review.js'), 'utf8'),
  fs.readFileSync(path.join(root, 'notebook', 'speedynote-notebook.js'), 'utf8'),
].join('\n');

function functionNames(source) {
  return new Set([...source.matchAll(/^\s*(?:async\s+)?function\s+([A-Za-z_$][\w$]*)\s*\(/gm)].map(match => match[1]));
}

function storageKeys(source) {
  return new Map([...source.matchAll(/^const\s+([A-Z][A-Z0-9_]*KEY)\s*=\s*'([^']+)'/gm)].map(match => [match[1], match[2]]));
}

function uiConfigKeys(source) {
  const block = source.match(/uiConfig:\s*\{([\s\S]*?)\n\s*\},/);
  assert.ok(block, 'uiConfig block should exist');
  return new Set([...block[1].matchAll(/^\s*([A-Za-z_$][\w$]*)\s*:/gm)].map(match => match[1]));
}

function paletteLines(source) {
  const result = new Map();
  for (const match of source.matchAll(/^\s*(classic|forest|ink|sunset|berry|cyan):\s*(\{.*\}),?$/gm)) {
    result.set(match[1], match[2].replace(/\s+/g, ''));
  }
  return result;
}

const legacyFunctions = functionNames(legacy);
const currentFunctions = functionNames(currentSources);
const functionAliases = new Map([['readFile', 'handleFile']]);
const missingFunctions = [...legacyFunctions].filter(name => !currentFunctions.has(name) && !currentFunctions.has(functionAliases.get(name)));
assert.deepEqual(missingFunctions, [], `legacy functions missing: ${missingFunctions.join(', ')}`);

const legacyStorage = storageKeys(legacy);
const currentStorage = storageKeys(currentIndex);
const missingStorage = [...legacyStorage].filter(([name, value]) => currentStorage.get(name) !== value);
assert.deepEqual(missingStorage, [], `legacy storage keys changed: ${JSON.stringify(missingStorage)}`);

const legacyConfig = uiConfigKeys(legacy);
const currentConfig = uiConfigKeys(currentIndex);
const missingConfig = [...legacyConfig].filter(key => !currentConfig.has(key));
assert.deepEqual(missingConfig, [], `legacy settings missing: ${missingConfig.join(', ')}`);

const legacyPalettes = paletteLines(legacy);
const currentPalettes = paletteLines(currentIndex);
for (const [name, value] of legacyPalettes) {
  assert.equal(currentPalettes.get(name), value, `${name} palette should remain byte-equivalent after whitespace normalization`);
}

const currentVersion = currentIndex.match(/const APP_VERSION\s*=\s*'v([^']+)'/)?.[1] || '';
const parts = currentVersion.split('.').map(Number);
assert.ok(parts.length === 3 && (parts[0] > 1 || parts[0] === 1 && (parts[1] > 0 || parts[1] === 0 && parts[2] >= 18)), `current version should not precede v1.0.18: ${currentVersion}`);

const requiredCurrentCapabilities = [
  'openCurrentQuizHandwriting',
  'openFreeNotebookLibrary',
  'openReviewQueue',
  'openExamSetup',
  'openAiChat',
  'showBackupExportDialog',
  'checkForBankUpdates',
  'checkForRemoteAnnouncements',
  'getSubjectStudyStats',
  'setSystemTheme',
  'previewComponentRadius',
  'previewReduceMotion',
];
const missingCurrentCapabilities = requiredCurrentCapabilities.filter(name => !currentSources.includes(name));
assert.deepEqual(missingCurrentCapabilities, [], `current capabilities missing: ${missingCurrentCapabilities.join(', ')}`);
assert.ok(!/gradient\(/i.test(currentSources), 'current product source should not contain gradients');

console.log(JSON.stringify({
  legacyFunctions: legacyFunctions.size,
  currentFunctions: currentFunctions.size,
  legacyStorageKeys: legacyStorage.size,
  currentStorageKeys: currentStorage.size,
  legacySettings: legacyConfig.size,
  currentSettings: currentConfig.size,
  legacyPalettes: [...legacyPalettes.keys()],
  currentVersion,
  currentCapabilities: requiredCurrentCapabilities.length,
}, null, 2));
