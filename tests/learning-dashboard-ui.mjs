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
page.on('pageerror', error => pageErrors.push(error.message));

try {
  await page.goto(appUrl, { waitUntil: 'domcontentloaded' });
  await page.waitForFunction(() => Boolean(window.QuizReview && window.QuizExam && getVisibleBanks().length));
  await page.evaluate(() => {
    document.querySelectorAll('.app-dialog-mask').forEach(element => element.remove());
    localStorage.removeItem(QuizReview.STORAGE_KEY);
    const questions = buildQuestionSet(getVisibleBanks()).slice(0, 2);
    questions.forEach(question => QuizReview.add(questionKey(question), question));
    const question = questions[0];
    state.wrongBook = {
      [questionKey(question)]: {
        key: questionKey(question),
        question,
        lastAnswer: 'A',
        correctAnswer: getCorrectAnswer(question),
        wrongCount: 1,
        reasonTags: ['concept'],
        updatedAt: Date.now(),
      },
    };
    saveWrongBook();
    localStorage.setItem(QuizExam.HISTORY_KEY, JSON.stringify([{
      id: 'exam:dashboard-test',
      title: '综合模拟考试',
      total: 10,
      correct: 8,
      wrong: 2,
      unanswered: 0,
      score: 80,
      submittedAt: Date.now(),
      durationUsed: 600,
      items: [],
    }]));
    setMainTab('study');
  });

  await page.locator('.hub-study-status').waitFor({ state: 'visible' });
  const statusText = await page.locator('.hub-study-status').innerText();
  assert.match(statusText, /2\s*\n?\s*待复习/);
  assert.match(statusText, /1\s*\n?\s*已标错因/);
  assert.match(statusText, /80\s*\n?\s*最近考试/);
  await page.screenshot({ path: 'output/playwright/study-dashboard-tablet.png', fullPage: true });

  await page.getByRole('button', { name: '统计' }).click();
  await page.locator('.study-stats-page').waitFor({ state: 'visible' });
  assert.match(await page.locator('.learning-insight-grid').innerText(), /考试均分/);
  const conceptRow = page.locator('.wrong-reason-row').filter({ hasText: '概念不清' });
  assert.equal(await conceptRow.locator('b').innerText(), '1');
  await page.screenshot({ path: 'output/playwright/learning-insights-tablet.png', fullPage: true });

  await page.setViewportSize({ width: 390, height: 844 });
  await page.evaluate(() => renderStudyStatsPage());
  await page.locator('.study-stats-page').waitFor({ state: 'visible' });
  await page.screenshot({ path: 'output/playwright/learning-insights-mobile.png', fullPage: true });
  assert.deepEqual(pageErrors, []);
  console.log(JSON.stringify({
    studyStatus: true,
    reviewStats: true,
    wrongReasonDistribution: true,
    examStats: true,
    responsiveScreenshots: true,
  }, null, 2));
} finally {
  await browser.close();
}
