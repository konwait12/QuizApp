const fs = require('fs');
const path = require('path');

const root = path.resolve(__dirname, '..');
const source = path.join(root, '毛概.md');
const dataDir = path.join(root, 'data');

const text = fs.readFileSync(source, 'utf8').replace(/\r\n/g, '\n');
const lines = text.split('\n');

function cleanInline(value) {
  return String(value || '')
    .replace(/\*\*/g, '')
    .replace(/【(单选题|多选题|判断题)】/g, '')
    .replace(/\[?\*AI讲解\*]?\(javascript:;\)/g, '')
    .replace(/\s+/g, ' ')
    .trim();
}

function cleanOption(value) {
  return cleanInline(value)
    .replace(/^[:：]\s*/, '')
    .replace(/;$/, '')
    .trim();
}

function parseAnswer(blockText, type) {
  const correctMatch = blockText.match(/\*正确答案:\*\s*([A-H]+|对|错|正确|错误)/);
  if (!correctMatch) return '';
  const raw = correctMatch[1].trim();
  if (type === '判断') {
    if (raw === '对' || raw === '正确') return '对';
    if (raw === '错' || raw === '错误') return '错';
  }
  return raw.toUpperCase();
}

function parseExplanation(blockLines) {
  const start = blockLines.findIndex(line => line.startsWith('*答案解析：*'));
  if (start < 0) return '';
  const parts = [];
  for (let i = start; i < blockLines.length; i++) {
    const line = blockLines[i];
    if (i > start && (line.startsWith('[*AI讲解*]') || /^\*\d/.test(line) || line.startsWith('### '))) break;
    parts.push(line.replace('*答案解析：*', ''));
  }
  return cleanInline(parts.join(' '));
}

function parseOptions(blockLines, type) {
  if (type === '判断') return ['对', '错'];
  const options = [];

  for (let i = 0; i < blockLines.length; i++) {
    const match = blockLines[i].match(/^-\s*([A-H])\.\s*(.*)$/);
    if (!match) continue;

    const parts = [match[2]];
    for (let j = i + 1; j < blockLines.length; j++) {
      const next = blockLines[j];
      if (/^-\s*[A-H]\.\s*/.test(next)) break;
      if (next.startsWith('*我的答案') || next.startsWith('*正确答案') || next.startsWith('*答案解析') || next.startsWith('[*AI讲解*]')) break;
      if (/^\*\d/.test(next)) break;
      const clean = next.trim();
      if (clean) parts.push(clean);
    }
    options.push(cleanOption(parts.join(' ')));
  }

  return options;
}

function parseQuestion(blockLines) {
  const heading = blockLines[0];
  const match = heading.match(/^###\s+(\d+)\.\s+\((单选题|多选题|判断题)\)\s*(.*)$/);
  if (!match) return null;

  const [, num, rawType, rawQuestion] = match;
  const type = rawType === '单选题' ? '单选' : rawType === '多选题' ? '多选' : '判断';
  const blockText = blockLines.join('\n');
  const ans = parseAnswer(blockText, type);
  const options = parseOptions(blockLines, type);
  const q = cleanInline(rawQuestion);

  if (!q || !ans) return null;
  return {
    id: `q${num}`,
    type,
    q,
    options,
    ans,
    exp: parseExplanation(blockLines),
  };
}

function isChapterHeading(index) {
  const line = lines[index] || '';
  if (!line.startsWith('## ')) return false;
  if (/^## [一二三]\./.test(line)) return false;
  return /^题量:\s*\d+/.test(lines[index + 2] || '');
}

const chapters = [];
for (let i = 0; i < lines.length; i++) {
  if (!isChapterHeading(i)) continue;
  const name = cleanInline(lines[i].replace(/^##\s*/, ''));
  const count = Number((lines[i + 2].match(/^题量:\s*(\d+)/) || [])[1] || 0);
  chapters.push({ name, count, start: i });
}

for (let i = 0; i < chapters.length; i++) {
  chapters[i].end = i + 1 < chapters.length ? chapters[i + 1].start : lines.length;
}

const output = [];
for (const chapter of chapters) {
  const block = lines.slice(chapter.start, chapter.end);
  const questionStarts = [];
  for (let i = 0; i < block.length; i++) {
    if (/^###\s+\d+\.\s+\((单选题|多选题|判断题)\)/.test(block[i])) questionStarts.push(i);
  }

  const questions = [];
  for (let i = 0; i < questionStarts.length; i++) {
    const start = questionStarts[i];
    const end = i + 1 < questionStarts.length ? questionStarts[i + 1] : block.length;
    const parsed = parseQuestion(block.slice(start, end));
    if (parsed) questions.push(parsed);
  }

  const fileName = `毛概-${chapter.name}.json`;
  const payload = {
    name: `毛概-${chapter.name}`,
    subject: '毛概',
    chapter: chapter.name,
    questions,
  };
  fs.writeFileSync(path.join(dataDir, fileName), `${JSON.stringify(payload, null, 2)}\n`, 'utf8');
  output.push({ fileName, expected: chapter.count, parsed: questions.length });
}

console.log(JSON.stringify(output, null, 2));
