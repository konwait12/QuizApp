const fs = require('fs');
const path = require('path');

const dataDir = path.resolve(__dirname, '..', 'data');

const xisiChapterNames = {
  '导论': '导论',
  '中国特色社会主义': '第一章 中国特色社会主义',
  '党的领导': '第二章 坚持党的全面领导',
  '经济建设': '第三章 经济建设',
  '政治建设': '第四章 政治建设',
  '文化建设': '第五章 文化建设',
  '社会建设': '第六章 社会建设',
  '生态文明': '第七章 生态文明建设',
  '国家安全': '第八章 国家安全',
  '外交': '第九章 中国特色大国外交',
  '综合': '综合题库',
};

function cleanQuestionText(text) {
  return String(text || '')
    .replace(/\u200b|\u200c|\u200d|\ufeff/g, '')
    .replace(/[─━]{5,}/g, '')
    .replace(/\s{2,}/g, ' ')
    .trim();
}

function stripOptionLabel(text) {
  return String(text || '').replace(/^\s*[A-H](?:[.．、]|\s+)\s*/i, '').trim();
}

function splitOptionBlock(text) {
  const clean = String(text || '')
    .replace(/\u200b|\u200c|\u200d|\ufeff/g, '')
    .replace(/\r/g, '\n')
    .trim();
  const result = [];
  const re = /(?:^|\n)\s*([A-H])(?:[.．、]|[\s])+\s*([\s\S]*?)(?=\n\s*[A-H](?:[.．、]|[\s])+\s*|$)/g;
  let match;
  while ((match = re.exec(clean))) {
    const value = stripOptionLabel(match[2]).trim();
    if (value) result.push(value);
  }
  return result;
}

function normalizeOptions(options) {
  const raw = Array.isArray(options) ? options.map(item => String(item || '').trim()).filter(Boolean) : [];
  if (!raw.length) return [];
  if (raw.length === 1) {
    const split = splitOptionBlock(raw[0]);
    if (split.length > 1) return split;
  }
  return raw.flatMap(item => {
    const split = splitOptionBlock(item);
    return split.length > 1 ? split : [stripOptionLabel(item)];
  }).map(item => item.trim()).filter(Boolean);
}

function answerLetters(answer) {
  return String(answer || '').toUpperCase().split('').filter(c => c >= 'A' && c <= 'Z');
}

function normalizeAnswer(question) {
  const raw = String(question.ans ?? question.answer ?? question.correct ?? '').trim();
  const upper = raw.toUpperCase();
  if (question.type === '判断') {
    if (['A', 'TRUE', 'T', 'YES', 'Y', '1', '对', '正确', '是', '√', '✓'].includes(upper) || ['对', '正确', '是', '√', '✓'].includes(raw)) return 'A';
    if (['B', 'FALSE', 'F', 'NO', 'N', '0', '错', '错误', '否', '×', '✗'].includes(upper) || ['错', '错误', '否', '×', '✗'].includes(raw)) return 'B';
  }
  return upper.split('').filter(c => c >= 'A' && c <= 'Z').sort().join('');
}

function normalizeType(question, options) {
  const declared = String(question.type || '').trim();
  if (declared.includes('判断')) return '判断';
  if (declared.includes('多选') || answerLetters(question.ans).length > 1) return '多选';
  if (options.length === 2 && options[0] === '对' && options[1] === '错') return '判断';
  return '单选';
}

function repairChapter(subject, chapter) {
  if (subject !== '习思') return chapter || '综合练习';
  return xisiChapterNames[chapter] || chapter || '综合题库';
}

let changedFiles = 0;
let changedQuestions = 0;

for (const file of fs.readdirSync(dataDir).filter(name => name.endsWith('.json'))) {
  const fullPath = path.join(dataDir, file);
  const data = JSON.parse(fs.readFileSync(fullPath, 'utf8'));
  const questions = Array.isArray(data.questions) ? data.questions : [];
  let fileChanged = false;

  const originalChapter = data.chapter;
  data.chapter = repairChapter(data.subject, data.chapter);
  if (data.chapter !== originalChapter) fileChanged = true;
  if (data.subject === '习思') {
    const newName = `习思-${data.chapter}`;
    if (data.name !== newName) {
      data.name = newName;
      fileChanged = true;
    }
  }

  for (const q of questions) {
    const before = JSON.stringify(q);
    q.q = cleanQuestionText(q.q || q.question || q['题目'] || '');
    q.options = normalizeOptions(q.options);
    q.type = normalizeType(q, q.options);
    if (q.type === '判断') q.options = ['对', '错'];
    q.ans = normalizeAnswer(q);
    if (q.exp == null) q.exp = '';
    if (JSON.stringify(q) !== before) {
      changedQuestions++;
      fileChanged = true;
    }
  }

  if (fileChanged) {
    fs.writeFileSync(fullPath, `${JSON.stringify(data, null, 2)}\n`, 'utf8');
    changedFiles++;
  }
}

console.log(`repaired files: ${changedFiles}`);
console.log(`repaired questions: ${changedQuestions}`);
