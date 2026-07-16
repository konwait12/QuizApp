import assert from 'node:assert/strict';
import path from 'node:path';
import { pathToFileURL } from 'node:url';

const memory = new Map();
globalThis.window = globalThis;
globalThis.localStorage = {
  getItem(key) { return memory.has(key) ? memory.get(key) : null; },
  setItem(key, value) { memory.set(key, String(value)); },
  removeItem(key) { memory.delete(key); },
};

await import(`${pathToFileURL(path.resolve('ai/learning-tasks.js')).href}?test=${Date.now()}`);
const ai = globalThis.QuizAiLearning;

const imageQuestion = {
  q: '图片题',
  options: [],
  questionImages: ['data:image/png;base64,AA=='],
};
const blockedQuestion = ai.capabilityFor(ai.TASK_TYPES.SIMILAR, {
  question: imageQuestion,
  images: imageQuestion.questionImages,
}, { visionEnabled: false });
assert.equal(blockedQuestion.allowed, false);
assert.equal(blockedQuestion.needsVision, true);

const allowedQuestion = ai.capabilityFor(ai.TASK_TYPES.SIMILAR, {
  question: imageQuestion,
  images: imageQuestion.questionImages,
}, { visionEnabled: true });
assert.equal(allowedQuestion.allowed, true);
assert.equal(allowedQuestion.includeImages, true);

const blockedNotebook = ai.capabilityFor(ai.TASK_TYPES.NOTEBOOK_SUMMARY, { hasInk: true }, { visionEnabled: false });
assert.equal(blockedNotebook.allowed, false);
const textNotebook = ai.capabilityFor(ai.TASK_TYPES.NOTEBOOK_SUMMARY, { hasInk: false }, { visionEnabled: false });
assert.equal(textNotebook.allowed, true);

const originalContext = {
  question: '示例题',
  options: [{ letter: 'A', text: '甲' }, { letter: 'B', text: '乙' }],
  correctAnswer: { letters: 'A' },
};
const originalSnapshot = structuredClone(originalContext);
const similar = await ai.run({
  type: ai.TASK_TYPES.SIMILAR,
  key: 'question:1',
  input: { context: originalContext, question: { q: '示例题', options: ['甲', '乙'] } },
  config: { visionEnabled: false, model: 'mock-model' },
  request: async () => ({
    content: JSON.stringify({
      questions: [
        { type: 'single', question: '相似题一', options: ['甲', '乙', '丙'], answer: 'B', analysis: '解析一' },
        { type: 'bool', question: '相似题二', options: ['正确', '错误'], answer: 'A', analysis: '解析二' },
      ],
    }),
    usage: { prompt_tokens: 10, completion_tokens: 20 },
  }),
});
assert.equal(similar.parsed.questions.length, 2);
assert.equal(similar.parsed.questions[0].answer, 'B');
assert.deepEqual(originalContext, originalSnapshot, 'AI task must not mutate source question facts');
assert.equal(ai.get(ai.TASK_TYPES.SIMILAR, 'question:1').model, 'mock-model');

const summary = await ai.run({
  type: ai.TASK_TYPES.NOTEBOOK_SUMMARY,
  key: 'notebook:page:1',
  input: { context: { textObjects: [{ text: '文本笔记' }] }, hasInk: false },
  config: { visionEnabled: false, model: 'mock-model' },
  request: async () => ({ content: '### 页面主题\n文本笔记' }),
});
assert.match(summary.content, /文本笔记/);
assert.equal(memory.has('quizapp_ai_analyses'), false, 'learning tasks must not reuse the legacy analysis store');
assert.equal(memory.has(ai.STORAGE_KEY), true);

console.log(JSON.stringify({
  strictVisionGate: true,
  structuredSimilarQuestions: true,
  sourceDataImmutable: true,
  textNotebookSummary: true,
  separateStorage: true,
}, null, 2));
