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
    localStorage.setItem('quizapp_ui_config', JSON.stringify({ autoUpdateCheck: false, autoAnnouncementCheck: false, autoBankUpdateCheck: false }));
    localStorage.setItem('quizapp_announcement_suppressed', '1');
  });
  await page.goto(appUrl, { waitUntil: 'domcontentloaded' });
  await page.waitForFunction(() => Boolean(window.QuizReview && window.QuizExam && getVisibleBanks().length));
  await page.evaluate(() => {
    document.querySelectorAll('.app-dialog-mask').forEach(element => element.remove());
    localStorage.removeItem(QuizReview.STORAGE_KEY);
    const questions = buildQuestionSet(getVisibleBanks()).slice(0, 2);
    questions.forEach(question => QuizReview.add(questionKey(question), question));
    const question = questions[0];
    const subject = question.sourceSubject || question.sourcePath?.[0] || getSubjectGroups()[0]?.subject || '未分类';
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
    state.studyStats[getTodayKey()] = {
      seconds: 180,
      subjects: { [subject]: 180 },
      modes: { '顺序': 120, '题目笔记': 60 },
      updatedAt: Date.now(),
    };
    setMainTab('study');
  });

  await page.locator('.hub-study-status').waitFor({ state: 'visible' });
  assert.ok(await page.locator('.study-subject-chip').count() > 0, 'study page should expose subjects as in-place selectors');
  assert.equal(await page.locator('.hub-study-subjects .subject-card').count(), 0, 'study page should not reuse library navigation cards');
  const subjectChips = page.locator('.study-subject-chip');
  const targetSubject = await subjectChips.count() > 1 ? subjectChips.nth(1) : subjectChips.first();
  await targetSubject.click();
  assert.equal(await page.evaluate(() => state.view), 'home', 'selecting a study subject should stay on the study dashboard');
  assert.equal(await page.evaluate(() => state.mainTab), 'study');
  assert.equal(await page.locator('.study-subject-chip.active').count(), 1);
  assert.ok(await page.locator('.study-scope-chip').count() > 0, 'selected subject should expose its available ranges');
  const scopeSelect = page.locator('[data-choice-id="studyScopeSelect"]');
  await scopeSelect.locator('.choice-trigger').click();
  await scopeSelect.locator('.choice-menu').waitFor({ state: 'visible' });
  await page.screenshot({ path: 'output/playwright/study-scope-dropdown-open-tablet.png', fullPage: false });
  assert.equal(
    await scopeSelect.locator('.choice-option').count(),
    await page.locator('.study-scope-chip').count(),
    'study scope dropdown should mirror the horizontal scope shortcuts',
  );
  if (await scopeSelect.locator('.choice-option').count() > 1) {
    await scopeSelect.locator('.choice-option').nth(1).click();
    assert.equal(await page.evaluate(() => state.view), 'home', 'dropdown scope selection should stay on the study dashboard');
    assert.equal(await page.evaluate(() => state.mainTab), 'study');
  }
  const scopes = page.locator('.study-scope-chip');
  if (await scopes.count() > 1) await scopes.nth(1).click();
  assert.equal(await page.evaluate(() => state.view), 'home', 'selecting a chapter should only refresh the dashboard');
  assert.ok(await page.locator('.study-mode-entry').count() >= 8, 'study actions should be available below the selected range');
  const statusText = await page.locator('.hub-study-status').innerText();
  assert.match(statusText, /2\s*\n?\s*待复习/);
  assert.match(statusText, /自由笔记/);
  assert.match(statusText, /80\s*\n?\s*最近考试/);
  await page.screenshot({ path: 'output/playwright/study-dashboard-tablet.png', fullPage: true });

  const endfieldContrast = await page.evaluate(() => {
    state.uiConfig.systemTheme = 'endfield';
    applyUiConfig();
    renderHome();
    const activeSubject = document.querySelector('.study-subject-chip.active');
    const primaryIcon = document.querySelector('.study-mode-entry .icon');
    return {
      activeSubjectColor: getComputedStyle(activeSubject).color,
      iconColor: getComputedStyle(primaryIcon).color,
      onPrimary: getComputedStyle(document.documentElement).getPropertyValue('--on-primary').trim(),
    };
  });
  assert.equal(endfieldContrast.onPrimary, '#101113', 'Endfield primary controls should use dark foreground on yellow');
  assert.notEqual(endfieldContrast.activeSubjectColor, 'rgb(253, 252, 0, 0)', 'active study selector should remain visible');
  await page.screenshot({ path: 'output/playwright/study-dashboard-endfield-tablet.png', fullPage: true });
  await page.evaluate(() => {
    state.uiConfig.systemTheme = 'standard';
    applyUiConfig();
    renderHome();
  });

  await page.getByRole('button', { name: '顺序练习' }).click();
  await page.locator('.quiz-topbar').waitFor({ state: 'visible' });
  assert.equal(await page.evaluate(() => state.view), 'quiz', 'a study mode action should enter the learning flow');
  await page.locator('.quiz-topbar .btn-back').click();
  await page.locator('.study-console').waitFor({ state: 'visible' });
  assert.equal(await page.evaluate(() => state.mainTab), 'study', 'returning from a study mode should restore the study dashboard');

  await page.setViewportSize({ width: 390, height: 844 });
  await page.screenshot({ path: 'output/playwright/study-dashboard-mobile.png', fullPage: true });
  const mobileOverflow = await page.evaluate(() => document.documentElement.scrollWidth - document.documentElement.clientWidth);
  assert.ok(mobileOverflow <= 1, `study dashboard should not overflow horizontally: ${mobileOverflow}`);
  await page.setViewportSize({ width: 1280, height: 800 });
  await page.evaluate(() => renderHome());

  await page.getByRole('button', { name: '统计' }).click();
  await page.locator('.study-stats-page').waitFor({ state: 'visible' });
  assert.match(await page.locator('.learning-insight-grid').innerText(), /考试均分/);
  assert.ok(await page.locator('.subject-study-card').count() > 0, 'subject-level study statistics should be visible');
  assert.match(await page.locator('.subject-study-card').first().innerText(), /顺序/);
  assert.match(await page.locator('.mode-study-summary').innerText(), /题目笔记/);
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
    subjectFirstFlow: true,
    scopeDropdown: true,
    inPlaceSubjectAndScopeSelection: true,
    modeEntryAndReturn: true,
    subjectStatistics: true,
    responsiveScreenshots: true,
    endfieldContrast: true,
  }, null, 2));
} finally {
  await browser.close();
}
