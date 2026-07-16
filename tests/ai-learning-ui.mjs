import assert from 'node:assert/strict';
import path from 'node:path';
import { createRequire } from 'node:module';
import { pathToFileURL } from 'node:url';

const nodeModules = process.env.PLAYWRIGHT_NODE_MODULES;
if (!nodeModules) throw new Error('Set PLAYWRIGHT_NODE_MODULES to the bundled node_modules directory');
const require = createRequire(pathToFileURL(path.join(nodeModules, 'quizapp-playwright-resolver.cjs')));
const { chromium } = require('playwright');
const appUrl = process.env.QUIZAPP_URL || 'http://127.0.0.1:8160/';
const browser = await chromium.launch({
  headless: true,
  ...(process.env.PLAYWRIGHT_CHROMIUM_EXECUTABLE ? { executablePath: process.env.PLAYWRIGHT_CHROMIUM_EXECUTABLE } : {}),
});
const page = await browser.newPage({ viewport: { width: 1280, height: 800 } });
const pageErrors = [];
let requestCount = 0;
let notebookVisionRequest = false;
page.on('pageerror', error => pageErrors.push(error.message));

await page.route('https://mock.ai/**', async route => {
  requestCount += 1;
  const body = route.request().postDataJSON();
  const system = body.messages.filter(message => message.role === 'system').map(message => String(message.content || '')).join('\n');
  let content = '### 结论\n模拟题目分析。';
  if (system.includes('相似题生成器')) {
    content = JSON.stringify({
      questions: [
        { type: 'single', question: '与原题同知识点的新题一', options: ['选项甲', '选项乙', '选项丙'], answer: 'B', analysis: '选项乙符合核心概念。' },
        { type: 'bool', question: '与原题同知识点的新题二', options: ['正确', '错误'], answer: 'A', analysis: '该表述成立。' },
      ],
    });
  } else if (system.includes('错因归纳助手')) {
    content = '### 核心错因\n对核心概念的边界理解不清。\n### 答题偏差\n选择了与题干条件不匹配的选项。\n### 下次提醒\n- 先圈出题干限定词\n- 再对照概念定义';
  } else if (system.includes('笔记摘要助手')) {
    const user = body.messages.find(message => message.role === 'user')?.content;
    if (Array.isArray(user) && user.some(item => item.type === 'image_url')) notebookVisionRequest = true;
    content = '### 页面主题\n核心概念整理\n### 核心要点\n- 要点一\n- 要点二\n### 待确认内容\n手写细节需复核\n### 下一步复习\n完成一道相似题';
  }
  await route.fulfill({
    status: 200,
    contentType: 'application/json',
    body: JSON.stringify({ choices: [{ message: { content } }], usage: { prompt_tokens: 20, completion_tokens: 30 } }),
  });
});

try {
  await page.goto(appUrl, { waitUntil: 'domcontentloaded' });
  await page.waitForFunction(() => Boolean(window.QuizAiLearning && getVisibleBanks().length));
  await page.evaluate(() => {
    document.querySelectorAll('.app-dialog-mask').forEach(element => element.remove());
    QuizAiLearning.clear();
    state.aiAnalyses = {};
    state.aiConfig = {
      ...state.aiConfig,
      provider: 'custom',
      baseUrl: 'https://mock.ai',
      apiKey: 'test-key',
      model: 'mock-model',
      visionEnabled: false,
    };
    const questions = buildQuestionSet(getVisibleBanks()).filter(question => !question.questionImages?.length).slice(0, 3);
    window.__aiTestQuestions = questions;
    startQuestionPractice({ subject: '测试', title: 'AI 学习测试', subtitle: 'AI', questions, shuffled: false, forceNew: true });
  });

  await page.evaluate(() => analyzeQuestionWithAi(0, false));
  assert.equal(await page.evaluate(() => Boolean(state.aiAnalyses[questionKey(state.activeSession.questions[0])])), true);
  await page.evaluate(() => generateSimilarQuestions(0, false));
  assert.equal(await page.locator('#card-0 .ai-similar-card').count(), 2);
  await page.locator('#card-0 .ai-similar-card').first().getByRole('button', { name: '显示答案' }).click();
  assert.match(await page.locator('#card-0 .ai-similar-answer').innerText(), /答案 B/);

  await page.evaluate(() => {
    const question = state.activeSession.questions[0];
    const key = questionKey(question);
    const correct = getCorrectAnswer(question);
    const wrong = 'ABCDEFGH'.split('').find(letter => getOptions(question)[letter.charCodeAt(0) - 65] && !correct.includes(letter)) || 'A';
    state.answers[0] = wrong;
    state.wrongBook[key] = {
      key,
      question,
      lastAnswer: wrong,
      correctAnswer: correct,
      wrongCount: 1,
      reasonTags: ['concept'],
      reasonNote: '容易混淆两个概念',
      updatedAt: Date.now(),
    };
    saveWrongBook();
    renderQuiz();
  });
  await page.evaluate(() => summarizeWrongReasonWithAi(0, false));
  assert.equal(await page.evaluate(() => state.wrongBook[questionKey(state.activeSession.questions[0])].reasonTags[0]), 'concept');
  assert.match(await page.locator('#card-0 .ai-wrong-reason').innerText(), /AI 错因建议/);
  await page.locator('#card-0').screenshot({ path: 'output/playwright/ai-learning-question-tablet.png' });
  await page.setViewportSize({ width: 390, height: 844 });
  await page.evaluate(() => renderQuiz());
  await page.locator('#card-0 .question-ai-panel').screenshot({ path: 'output/playwright/ai-learning-question-mobile.png' });
  await page.setViewportSize({ width: 1440, height: 900 });
  await page.evaluate(() => renderQuiz());
  await page.locator('#card-0 .question-ai-panel').screenshot({ path: 'output/playwright/ai-learning-question-desktop.png' });
  await page.setViewportSize({ width: 1280, height: 800 });
  await page.evaluate(() => renderQuiz());

  const beforeBlockedRequest = requestCount;
  await page.evaluate(() => {
    const imageQuestion = normalizeQuestion({
      id: 'image-dependent-test',
      type: 'subjective',
      q: '图片题',
      options: [],
      ans: 'A',
      questionImages: ['data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mP8/x8AAusB9Y9Zl1sAAAAASUVORK5CYII='],
    }, 0);
    imageQuestion.sourceBankId = 'test';
    imageQuestion.sourceIndex = 0;
    state.activeSession.questions = [imageQuestion];
    state.currentIndex = 0;
    state.aiConfig.visionEnabled = false;
    renderQuiz();
    return analyzeQuestionWithAi(0, false);
  });
  assert.equal(requestCount, beforeBlockedRequest, 'image-dependent analysis must be blocked before the API request');
  assert.match(await page.locator('.app-dialog').innerText(), /支持图片/);
  await page.evaluate(() => closeAppDialog());

  await page.evaluate(async () => {
    state.activeSession.questions = window.__aiTestQuestions;
    state.currentIndex = 0;
    await openHandwritingPractice([], { freeMode: true });
    state.notebookSession.addObject('text', { text: '概念一：测试文本笔记' }, { x: 120, y: 160, width: 500, height: 160 });
    state.notebookRightTab = 'ai';
    state.notebookAiMode = 'summary';
    state.aiConfig.visionEnabled = false;
    renderHandwritingPractice();
  });
  await page.evaluate(() => summarizeCurrentNotebookPage(false));
  assert.match(await page.locator('#notebookRightPanel .ai-learning-result').innerText(), /AI 笔记摘要/);

  await page.evaluate(() => {
    const layer = state.notebookSession.layer;
    layer.strokes.push({
      id: 'stroke:test', tool: 'pen', color: '#202522', size: 6, pointerType: 'pen',
      points: [{ x: 180, y: 420, pressure: .5 }, { x: 500, y: 520, pressure: .6 }],
      bounds: { x: 174, y: 414, width: 332, height: 112 },
    });
    state.notebookSession.page.updatedAt = Date.now();
    renderNotebookRightPanel();
  });
  assert.match(await page.locator('#notebookRightPanel').innerText(), /更新摘要/);
  const beforeNotebookBlocked = requestCount;
  await page.evaluate(() => summarizeCurrentNotebookPage(true));
  assert.equal(requestCount, beforeNotebookBlocked, 'handwritten summary must be blocked without vision');
  assert.match(await page.locator('.app-dialog').innerText(), /手写笔迹/);
  await page.evaluate(() => {
    closeAppDialog();
    state.aiConfig.visionEnabled = true;
  });
  await page.evaluate(() => summarizeCurrentNotebookPage(true));
  assert.equal(notebookVisionRequest, true, 'handwritten notebook summary must include an image input');
  await page.locator('#notebookRightPanel').screenshot({ path: 'output/playwright/ai-notebook-summary-tablet.png' });

  await page.setViewportSize({ width: 390, height: 844 });
  await page.evaluate(() => {
    state.notebookMobilePane = 'right';
    renderHandwritingPractice();
  });
  await page.screenshot({ path: 'output/playwright/ai-notebook-summary-mobile.png', fullPage: true });

  const separation = await page.evaluate(() => ({
    learningStore: Boolean(localStorage.getItem(QuizAiLearning.STORAGE_KEY)),
    builtinUntouched: Boolean(window.__aiTestQuestions[0].explanations?.builtin),
    generatedInBank: Object.prototype.hasOwnProperty.call(window.__aiTestQuestions[0], 'similarQuestions'),
  }));
  assert.equal(separation.learningStore, true);
  assert.equal(separation.generatedInBank, false);
  assert.deepEqual(pageErrors, []);
  console.log(JSON.stringify({
    questionAnalysis: true,
    structuredSimilarQuestions: true,
    wrongReasonSuggestion: true,
    strictVisionBlocking: true,
    notebookTextSummary: true,
    notebookVisionSummary: true,
    separateStorage: true,
    responsiveScreenshots: true,
  }, null, 2));
} finally {
  await browser.close();
}
