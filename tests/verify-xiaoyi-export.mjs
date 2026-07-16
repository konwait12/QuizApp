import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const projectRoot = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const root = path.join(projectRoot, 'output', 'xiaoyi-question-banks');
const report = JSON.parse(fs.readFileSync(path.join(root, 'export-report.json'), 'utf8'));
const files = [];

function walk(directory) {
  for (const entry of fs.readdirSync(directory, { withFileTypes: true })) {
    const absolute = path.join(directory, entry.name);
    if (entry.isDirectory()) walk(absolute);
    else if (entry.name.endsWith('.json') && entry.name !== 'export-report.json') files.push(absolute);
  }
}

walk(root);
let questions = 0;
let questionImages = 0;
let builtinImages = 0;
const types = new Set();

for (const file of files) {
  const bank = JSON.parse(fs.readFileSync(file, 'utf8'));
  assert.equal(bank.schemaVersion, 2, `${file} schemaVersion`);
  assert.equal(bank.subject, '考研数学', `${file} subject`);
  assert.ok(Array.isArray(bank.path) && bank.path.length === 4, `${file} path`);
  for (const question of bank.questions) {
    questions += 1;
    types.add(question.type);
    assert.ok(question.id.startsWith('xiaoyi:'), `${file} question id`);
    assert.ok(question.questionImages?.length, `${file} ${question.id} question image`);
    assert.ok(question.explanations?.builtin, `${file} ${question.id} builtin explanation`);
    assert.equal(question.explanations.ai, undefined, `${file} ${question.id} AI must remain separate`);
    assert.equal(question.aiAnalysis, undefined, `${file} ${question.id} AI must remain separate`);
    questionImages += question.questionImages.length;
    builtinImages += question.explanations.builtin.images?.length || 0;
  }
}

assert.equal(files.length, 72);
assert.equal(questions, 870);
assert.equal(report.totals.questions, questions);
assert.equal(report.publicBanks.length, 2);
assert.equal(report.restrictedBanks.length, 10);
assert.ok(types.has('single'));
assert.ok(types.has('subjective'));
assert.equal(questionImages, 870);
assert.ok(builtinImages >= questions);

console.log(JSON.stringify({
  files: files.length,
  questions,
  types: Array.from(types).sort(),
  questionImages,
  builtinImages,
  restrictedBanks: report.restrictedBanks.length,
}, null, 2));
