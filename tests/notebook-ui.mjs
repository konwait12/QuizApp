import assert from 'node:assert/strict';
import path from 'node:path';
import { createRequire } from 'node:module';
import { pathToFileURL } from 'node:url';

const nodeModules = process.env.PLAYWRIGHT_NODE_MODULES;
if (!nodeModules) throw new Error('Set PLAYWRIGHT_NODE_MODULES to the bundled node_modules directory');
const require = createRequire(pathToFileURL(path.join(nodeModules, 'quizapp-playwright-resolver.cjs')));
const { chromium } = require('playwright');
const appUrl = process.env.QUIZAPP_URL || 'http://127.0.0.1:8160/';
const testBank = {
  id: 'notebook-ui-bank',
  name: '手写回归题库',
  subject: '手写回归',
  chapter: '第一章',
  questions: [
    { id: 'q1', q: '第一道测试题', options: ['选项 A', '选项 B'], ans: 'A', type: '单选' },
    { id: 'q2', q: '第二道测试题', options: ['选项 A', '选项 B'], ans: 'B', type: '单选' },
    { id: 'q3', q: '第三道测试题', options: ['选项 A', '选项 B'], ans: 'A', type: '单选' },
  ],
};

const browser = await chromium.launch({
  headless: true,
  ...(process.env.PLAYWRIGHT_CHROMIUM_EXECUTABLE ? { executablePath: process.env.PLAYWRIGHT_CHROMIUM_EXECUTABLE } : {}),
});
const page = await browser.newPage({ viewport: { width: 1280, height: 800 } });
const pageErrors = [];
page.on('pageerror', error => pageErrors.push(error.message));

try {
  await page.addInitScript(bank => {
    window.__QUIZAPP_EMBEDDED_BANKS__ = [bank];
    try {
      localStorage.setItem('quizapp_ui_config', JSON.stringify({ autoUpdateCheck: false, autoAnnouncementCheck: false }));
      localStorage.setItem('quizapp_announcement_suppressed', '1');
    } catch(e) {}
  }, testBank);
  await page.goto(appUrl, { waitUntil: 'domcontentloaded' });
  await page.waitForFunction(() => typeof startAllPractice === 'function' && typeof loadBundledBanks === 'function');
  const bankDiagnostics = await page.evaluate(async bank => {
    if (!Array.isArray(window.__QUIZAPP_EMBEDDED_BANKS__)) window.__QUIZAPP_EMBEDDED_BANKS__ = [bank];
    if (!getVisibleBanks().length) await loadBundledBanks();
    return {
      embedded: Array.isArray(window.__QUIZAPP_EMBEDDED_BANKS__),
      embeddedCount: window.__QUIZAPP_EMBEDDED_BANKS__?.length || 0,
      visibleCount: getVisibleBanks().length,
      loadError: state.defaultLoadError || '',
    };
  }, testBank);
  assert.ok(bankDiagnostics.visibleCount > 0, `embedded test bank should load: ${JSON.stringify(bankDiagnostics)}`);
  await page.evaluate(() => {
    document.querySelectorAll('.app-dialog-mask').forEach(element => element.remove());
    startAllPractice(false, true);
  });
  await page.locator('.quiz-topbar').waitFor({ state: 'visible' });
  const quizContext = await page.evaluate(() => {
    state.mode = 'page';
    state.currentIndex = Math.min(2, state.activeSession.questions.length - 1);
    state.answers[state.currentIndex] = 'A';
    renderQuiz();
    document.querySelectorAll('.app-dialog-mask').forEach(element => element.remove());
    const scroller = document.querySelector('.page-slide.current-slide');
    scroller.scrollTop = Math.min(72, Math.max(0, scroller.scrollHeight - scroller.clientHeight));
    return {
      index: state.currentIndex,
      answer: state.answers[state.currentIndex],
      mode: state.mode,
      scrollTop: scroller.scrollTop,
      answered: computeQuizProgress(state.activeSession).answered,
    };
  });

  await page.getByLabel('当前题目手写笔记').click();
  await page.locator('.notebook-view').waitFor({ state: 'visible' });
  await page.waitForFunction(() => Boolean(state.notebookViewport && state.notebookSession));
  assert.equal(await page.evaluate(() => state.inkSession.returnToQuiz), true);
  const routeQuizViewport = await page.evaluate(() => state.navStack.at(-1)?.quizViewport || null);
  assert.ok(
    routeQuizViewport && Math.abs(routeQuizViewport.scrollTop - quizContext.scrollTop) <= 1,
    `quiz viewport snapshot should match the entry position: ${JSON.stringify({ before: quizContext, snapshot: routeQuizViewport })}`,
  );

  const drag = await page.evaluate(() => {
    const session = state.notebookSession;
    session.addObject('text', { text: 'Concept A' }, { x: 120, y: 160, width: 220, height: 120 });
    session.addObject('text', { text: 'Concept B' }, { x: 400, y: 300, width: 220, height: 120 });
    state.notebookViewport.setTool('select');
    state.notebookViewport.requestRender();
    const rect = state.notebookViewport.canvas.getBoundingClientRect();
    const point = (x, y) => ({
      x: rect.left + state.notebookViewport.offsetX + x * state.notebookViewport.scale,
      y: rect.top + state.notebookViewport.offsetY + y * state.notebookViewport.scale,
    });
    return { start: point(80, 120), end: point(680, 470) };
  });
  await page.mouse.move(drag.start.x, drag.start.y);
  await page.mouse.down();
  await page.mouse.move(drag.end.x, drag.end.y, { steps: 10 });
  await page.mouse.up();
  assert.equal(await page.evaluate(() => state.notebookSession.getSelectionItems().length), 2);

  const objectCount = await page.evaluate(() => state.notebookSession.page.objects.length);
  await page.getByTitle('复制所选').click();
  await page.getByTitle('粘贴').click();
  assert.equal(await page.evaluate(() => state.notebookSession.page.objects.length), objectCount + 2);

  await page.getByTitle('新增页面').click();
  await page.getByTitle('新增页面').click();
  assert.equal(await page.locator('.notebook-page-thumb').count(), 3);
  assert.equal(await page.locator('[data-notebook-page-preview]').count(), 3);
  const draggedPageId = await page.locator('.notebook-page-thumb').nth(2).getAttribute('data-page-id');
  await page.locator('.notebook-page-thumb').nth(2).dragTo(page.locator('.notebook-page-thumb').nth(0));
  assert.equal(await page.evaluate(() => state.notebookSession.document.pages[0].id), draggedPageId);

  const thumbnailHasPixels = await page.locator('[data-notebook-page-preview]').first().evaluate(canvas => {
    const pixels = canvas.getContext('2d').getImageData(0, 0, canvas.width, canvas.height).data;
    return pixels.some((value, index) => index % 4 !== 3 && value !== 0);
  });
  assert.equal(thumbnailHasPixels, true);
  await page.screenshot({ path: 'output/playwright/notebook-tablet-advanced.png', fullPage: true });

  await page.setViewportSize({ width: 390, height: 844 });
  await page.evaluate(() => renderHandwritingPractice());
  await page.locator('.notebook-view').waitFor({ state: 'visible' });
  await page.waitForTimeout(100);
  const mobileFit = await page.evaluate(() => {
    const viewport = state.notebookViewport;
    const rect = viewport.canvas.getBoundingClientRect();
    return {
      left: viewport.offsetX,
      right: viewport.offsetX + viewport.session.page.width * viewport.scale,
      width: rect.width,
    };
  });
  assert.ok(mobileFit.left >= 0, 'mobile page should not be clipped on the left');
  assert.ok(mobileFit.right <= mobileFit.width + 1, 'mobile page should fit inside the canvas width');
  await page.screenshot({ path: 'output/playwright/notebook-mobile-advanced.png', fullPage: true });

  await page.setViewportSize({ width: 1280, height: 800 });
  await page.locator('.notebook-topbar .btn-back').click();
  await page.locator('.quiz-topbar').waitFor({ state: 'visible' });
  await page.waitForTimeout(80);
  const restoredQuizContext = await page.evaluate(() => ({
    index: state.currentIndex,
    answer: state.answers[state.currentIndex],
    mode: state.mode,
    scrollTop: document.querySelector('.page-slide.current-slide')?.scrollTop || 0,
    answered: computeQuizProgress(state.activeSession).answered,
  }));
  assert.equal(restoredQuizContext.index, quizContext.index);
  assert.equal(restoredQuizContext.answer, quizContext.answer);
  assert.equal(restoredQuizContext.mode, quizContext.mode);
  assert.equal(restoredQuizContext.answered, quizContext.answered);
  assert.ok(
    Math.abs(restoredQuizContext.scrollTop - quizContext.scrollTop) <= 1,
    `quiz viewport should be restored: ${JSON.stringify({ before: quizContext, after: restoredQuizContext })}`,
  );
  assert.deepEqual(pageErrors, []);
  console.log(JSON.stringify({
    quizEntryAndReturn: true,
    lassoMultiSelect: true,
    copyPaste: true,
    pageThumbnailPixels: true,
    pageDragReorder: true,
    responsiveScreenshots: true,
  }, null, 2));
} finally {
  await browser.close();
}
