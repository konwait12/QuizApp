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
  await page.addInitScript(() => {
    localStorage.setItem('quizapp_ui_config', JSON.stringify({
      autoUpdateCheck: false,
      autoAnnouncementCheck: false,
      autoBankUpdateCheck: false,
    }));
    localStorage.setItem('quizapp_announcement_suppressed', '1');
  });
  await page.goto(appUrl, { waitUntil: 'domcontentloaded' });
  await page.waitForFunction(() => typeof openExamSetup === 'function' && getVisibleBanks().length > 0);
  await page.evaluate(() => {
    document.querySelectorAll('.app-dialog-mask').forEach(element => element.remove());
    localStorage.removeItem('quizapp_exam_history');
    localStorage.removeItem('quizapp_active_exam');
    openExamSetup();
  });
  await page.locator('[data-choice-id="examCount"] .choice-trigger').click();
  await page.locator('[data-choice-id="examCount"] .choice-option[data-choice-value="10"]').click();
  await page.getByRole('button', { name: '开始考试' }).click();
  await page.locator('.exam-question-card').waitFor({ state: 'visible' });
  assert.equal(await page.evaluate(() => state.examSession.questions.length), 10);

  await page.evaluate(() => {
    const correct = String(getCorrectAnswer(state.examSession.questions[0]) || '');
    correct.split('').forEach(letter => answerExamQuestion(letter));
  });
  await page.evaluate(() => {
    state.examSession.currentIndex = 1;
    const question = state.examSession.questions[1];
    const correct = getCorrectAnswer(question);
    const wrong = 'ABCDEFGH'.split('').find(letter => getOptions(question)[letter.charCodeAt(0) - 65] && !correct.includes(letter));
    answerExamQuestion(wrong || 'A');
  });
  await page.evaluate(() => requestSubmitExam());
  await page.locator('.app-dialog').waitFor({ state: 'visible' });
  const confirmButtons = page.locator('.app-dialog .app-dialog-actions button');
  assert.equal(await confirmButtons.count(), 2, 'submit confirmation should offer cancel and confirm');
  await confirmButtons.last().click();
  await page.locator('.exam-result-page').waitFor({ state: 'visible' });
  assert.equal(await page.locator('.exam-result-list details').count(), 9);

  await page.getByRole('button', { name: '返回学习页' }).click();
  await page.evaluate(() => openExamSetup());
  const historyButton = page.locator('.exam-history-list > button').first();
  await historyButton.waitFor({ state: 'visible' });
  await historyButton.click();
  await page.locator('.exam-history-page').waitFor({ state: 'visible' });
  assert.equal(await page.locator('.exam-history-page .exam-result-list details').count(), 10);
  assert.equal(await page.locator('.exam-history-page .exam-result-list details.correct').count(), 1);

  await page.getByRole('button', { name: '错题与未答' }).click();
  assert.equal(await page.locator('.exam-history-page .exam-result-list details').count(), 9);
  await page.screenshot({ path: 'output/playwright/exam-history-tablet.png', fullPage: true });

  await page.setViewportSize({ width: 390, height: 844 });
  await page.screenshot({ path: 'output/playwright/exam-history-mobile.png', fullPage: true });
  await page.locator('.topbar .btn-back').click();
  await page.locator('.exam-setup-page').waitFor({ state: 'visible' });
  assert.deepEqual(pageErrors, []);
  console.log(JSON.stringify({
    fullQuestionHistory: true,
    correctWrongUnanswered: true,
    filter: true,
    backRoute: true,
    responsiveScreenshots: true,
  }, null, 2));
} finally {
  await browser.close();
}
