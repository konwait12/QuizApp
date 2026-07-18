import assert from 'node:assert/strict';
import path from 'node:path';
import { createRequire } from 'node:module';
import { pathToFileURL } from 'node:url';

const nodeModules = process.env.PLAYWRIGHT_NODE_MODULES;
if (!nodeModules) throw new Error('Set PLAYWRIGHT_NODE_MODULES to the bundled node_modules directory');
const require = createRequire(pathToFileURL(path.join(nodeModules, 'quizapp-playwright-resolver.cjs')));
const { chromium } = require('playwright');
const appUrl = process.env.QUIZAPP_URL || 'http://127.0.0.1:8139/';
const browser = await chromium.launch({
  headless: true,
  ...(process.env.PLAYWRIGHT_CHROMIUM_EXECUTABLE ? { executablePath: process.env.PLAYWRIGHT_CHROMIUM_EXECUTABLE } : {}),
});
const page = await browser.newPage({ viewport: { width: 1280, height: 800 } });
const pageErrors = [];
page.on('pageerror', error => pageErrors.push(error.message));

const subject = '__EDIT_TEST_SUBJECT__';
const chapter = '__EDIT_TEST_CHAPTER__';
const bankId = 'imported:edit-library-test';

try {
  await page.addInitScript(() => {
    localStorage.setItem('quizapp_ui_config', JSON.stringify({ autoUpdateCheck: false, autoAnnouncementCheck: false, autoBankUpdateCheck: false }));
    localStorage.setItem('quizapp_announcement_suppressed', '1');
  });
  await page.goto(appUrl, { waitUntil: 'domcontentloaded' });
  await page.waitForFunction(() => typeof renderHome === 'function' && !state.loadingDefaults && getVisibleBanks().length > 0);
  await page.evaluate(({ subject, chapter, bankId }) => {
    document.querySelectorAll('.app-dialog-mask').forEach(element => element.remove());
    state.recycleBin = [];
    state.banks = state.banks.filter(bank => bank.id !== bankId);
    state.libraryNodes = state.libraryNodes.filter(node => node.path?.[0] !== subject);
    state.banks.push({
      id: bankId,
      name: '编辑模式测试题库',
      subject,
      chapter,
      path: [subject, chapter],
      hierarchy: [subject, chapter],
      bundled: false,
      questions: [{ q: '测试题目', opts: ['选项 A', '选项 B'], ans: 'A', type: 'single' }],
    });
    ensureLibraryPath([subject, chapter]);
    markBankHidden(bankId, false);
    state.mainTab = 'library';
    state.view = 'home';
    state.editMode = false;
    window.askConfirm = (_message, callback) => callback();
    renderHome();
  }, { subject, chapter, bankId });

  const normalSubjectCount = await page.locator('.subject-card').count();
  const normalMetricCount = await page.locator('.metric-card').count();
  await page.evaluate(() => toggleEditMode());
  assert.equal(await page.locator('.subject-card').count(), normalSubjectCount, 'edit mode must preserve the visible subject cards');
  assert.equal(await page.locator('.metric-card').count(), normalMetricCount, 'edit mode must preserve the visible metric cards');
  assert.ok(await page.locator('.subject-card.editing .edit-remove').count() > 0, 'edit mode should add remove controls');
  assert.equal(await page.locator('.action-panel').count(), 0, 'normal tool panels should not appear inside edit mode');
  assert.ok(await page.locator('.editable-card[data-drag-kind][data-drag-key]').count() > 0, 'editable cards should expose drag metadata');

  const subjectCards = page.locator('.subject-card.editing[data-drag-kind="subject"]');
  if (await subjectCards.count() > 1) {
    const source = subjectCards.nth(0);
    const target = subjectCards.nth(1);
    const sourceKey = await source.getAttribute('data-drag-key');
    const targetKey = await target.getAttribute('data-drag-key');
    const sourceBox = await source.boundingBox();
    const targetBox = await target.boundingBox();
    assert.ok(sourceBox && targetBox, 'subject cards should have drag bounds');
    await page.mouse.move(sourceBox.x + sourceBox.width / 2, sourceBox.y + sourceBox.height / 2);
    await page.mouse.down();
    await page.waitForTimeout(320);
    assert.equal(await source.evaluate(element => element.classList.contains('dragging')), true, 'long press should start dragging');
    await page.mouse.move(targetBox.x + targetBox.width / 2, targetBox.y + targetBox.height / 2, { steps: 4 });
    const movedBox = await source.boundingBox();
    assert.ok(movedBox && Math.abs((movedBox.x + movedBox.width / 2) - (targetBox.x + targetBox.width / 2)) < 12, 'dragged card should follow the pointer');
    await page.mouse.up();
    await page.waitForTimeout(80);
    const subjectOrder = await page.evaluate(() => state.uiConfig.subjectOrder);
    assert.ok(subjectOrder.indexOf(sourceKey) >= 0 && subjectOrder.indexOf(targetKey) >= 0, 'dragged subjects should remain in the saved order');
    assert.ok(subjectOrder.indexOf(sourceKey) >= subjectOrder.indexOf(targetKey), 'dragging the first subject to the second should reorder them');
    assert.equal(await page.evaluate(() => state.view), 'home', 'drag release must not open the subject');
  }

  await page.evaluate(subject => deleteLibraryPath([subject]), subject);
  assert.equal(await page.evaluate(bankId => state.bankMeta[bankId]?.hidden, bankId), true, 'deleted imported bank should be hidden');
  assert.equal(await page.evaluate(() => state.recycleBin.length), 1, 'deleted subject should enter the recycle bin');

  await page.evaluate(() => renderSettings());
  await page.locator('.recycle-box').evaluate(element => { element.open = true; });
  await page.locator('.recycle-item').first().evaluate(element => { element.open = true; });
  const recycleText = await page.locator('.recycle-box').innerText();
  assert.match(recycleText, /编辑模式测试题库/);
  assert.match(recycleText, /1 题 · 导入题库/);
  assert.match(recycleText, /__EDIT_TEST_SUBJECT__ \/ __EDIT_TEST_CHAPTER__/);
  await page.locator('.recycle-box').scrollIntoViewIfNeeded();
  await page.screenshot({ path: 'output/playwright/edit-library-recycle-tablet.png' });

  const recycleId = await page.evaluate(() => state.recycleBin[0].id);
  await page.evaluate(id => restoreRecycleItem(id), recycleId);
  assert.equal(await page.evaluate(bankId => state.bankMeta[bankId]?.hidden, bankId), false, 'restore should reveal the bank');
  assert.equal(await page.evaluate(() => state.recycleBin.length), 0, 'restored item should leave the recycle bin');

  await page.evaluate(subject => deleteLibraryPath([subject]), subject);
  const purgeId = await page.evaluate(() => state.recycleBin[0].id);
  await page.evaluate(id => purgeRecycleItem(id), purgeId);
  assert.equal(await page.evaluate(bankId => state.banks.some(bank => bank.id === bankId), bankId), false, 'purge should remove imported bank data');
  assert.equal(await page.evaluate(() => state.recycleBin.length), 0, 'purged item should leave the recycle bin');

  await page.setViewportSize({ width: 390, height: 844 });
  await page.evaluate(() => { state.editMode = true; state.mainTab = 'library'; renderHome(); });
  assert.ok(await page.evaluate(() => document.documentElement.scrollWidth - document.documentElement.clientWidth <= 1));
  await page.screenshot({ path: 'output/playwright/edit-library-mobile.png' });

  assert.deepEqual(pageErrors, []);
  console.log(JSON.stringify({
    layoutPreserved: true,
    editControls: true,
    longPressDrag: true,
    recycleDetails: true,
    restore: true,
    permanentDelete: true,
    responsive: true,
  }, null, 2));
} finally {
  await browser.close();
}
