import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import { createRequire } from 'node:module';
import { pathToFileURL } from 'node:url';

const nodeModules = process.env.PLAYWRIGHT_NODE_MODULES;
if (!nodeModules) throw new Error('Set PLAYWRIGHT_NODE_MODULES to the bundled node_modules directory');
const require = createRequire(pathToFileURL(path.join(nodeModules, 'quizapp-playwright-resolver.cjs')));
const { chromium } = require('playwright');
const appUrl = process.env.QUIZAPP_URL || 'http://127.0.0.1:8160/';
const outputDirectory = path.resolve('output/playwright');
fs.mkdirSync(outputDirectory, { recursive: true });

const browser = await chromium.launch({
  headless: true,
  ...(process.env.PLAYWRIGHT_CHROMIUM_EXECUTABLE ? { executablePath: process.env.PLAYWRIGHT_CHROMIUM_EXECUTABLE } : {}),
});
const page = await browser.newPage({ viewport: { width: 1180, height: 820 } });
const errors = [];
page.on('pageerror', error => errors.push(error.message));

try {
  await page.addInitScript(() => {
    localStorage.setItem('quizapp_ui_config', JSON.stringify({ autoUpdateCheck: false, autoAnnouncementCheck: false }));
    localStorage.setItem('quizapp_announcement_suppressed', '1');
  });
  await page.goto(appUrl, { waitUntil: 'domcontentloaded' });
  await page.waitForFunction(() => Boolean(window.QuizNotebook && window.QuizPdfNotebook));
  await page.evaluate(async () => {
    document.querySelectorAll('.app-dialog-mask').forEach(element => element.remove());
    await openHandwritingPractice([], { freeMode: true });
    state.notebookSession.addPage({ name: '第 2 页' });
    state.notebookSession.addPage({ name: '第 3 页' });
    renderHandwritingPractice();
  });
  assert.equal(await page.locator('.notebook-page-thumb').count(), 3);

  await page.getByRole('button', { name: '添加当前页书签' }).click();
  await page.locator('.notebook-page-thumb').first().click();
  await page.getByRole('button', { name: '添加当前页书签' }).click();
  assert.equal(await page.locator('.notebook-page-bookmark').count(), 2);

  await page.getByRole('button', { name: '书签', exact: true }).click();
  assert.equal(await page.locator('.notebook-bookmark-item').count(), 2);
  await page.screenshot({ path: path.join(outputDirectory, 'notebook-bookmarks-tablet.png'), fullPage: true });

  page.once('dialog', async dialog => {
    assert.equal(dialog.type(), 'prompt');
    await dialog.accept('重点公式');
  });
  await page.getByRole('button', { name: '重命名书签' }).first().click();
  await page.waitForFunction(() => state.notebookSession.document.bookmarks.some(bookmark => bookmark.label === '重点公式'));
  assert.match(await page.locator('.notebook-bookmark-item').first().innerText(), /重点公式/);

  const targetPageId = await page.evaluate(() => {
    const bookmark = state.notebookSession.document.bookmarks.find(item => item.label === '重点公式');
    state.notebookSession.setPage(state.notebookSession.document.pages[1].id);
    renderHandwritingPractice();
    setNotebookLeftTab('bookmarks');
    return bookmark.pageId;
  });
  await page.getByRole('button', { name: /重点公式/ }).click();
  assert.equal(await page.evaluate(() => state.notebookSession.document.activePageId), targetPageId);

  await page.setViewportSize({ width: 390, height: 844 });
  await page.evaluate(() => {
    renderHandwritingPractice();
    toggleNotebookMobilePane('left');
    setNotebookLeftTab('bookmarks');
  });
  const panelBox = await page.locator('#notebookLeftPanel').boundingBox();
  assert.ok(panelBox && panelBox.x >= 0 && panelBox.width <= 391);
  await page.screenshot({ path: path.join(outputDirectory, 'notebook-bookmarks-mobile.png'), fullPage: true });

  const persistence = await page.evaluate(async () => {
    const notebook = state.notebookSession.document;
    await saveCurrentInkNote({ silent: true, throwOnError: true });
    const saved = await getNotebookRepository().get(notebook.id);
    const search = QuizPdfNotebook.documentSearchText(saved);
    const bookmarkedPage = saved.bookmarks[0].pageId;
    state.notebookSession.setPage(bookmarkedPage);
    const before = state.notebookSession.document.bookmarks.length;
    state.notebookSession.removePage();
    const after = state.notebookSession.document.bookmarks.length;
    state.notebookSession.undo();
    const restored = state.notebookSession.document.bookmarks.length;
    return { id: notebook.id, savedCount: saved.bookmarks.length, search, before, after, restored };
  });
  assert.equal(persistence.savedCount, 2);
  assert.match(persistence.search, /重点公式/);
  assert.equal(persistence.after, persistence.before - 1);
  assert.equal(persistence.restored, persistence.before);
  assert.deepEqual(errors, []);

  await page.evaluate(async documentId => {
    await getNotebookAssetRepository().deleteByDocument(documentId);
    await getNotebookRepository().delete(documentId);
  }, persistence.id);
  console.log(JSON.stringify({
    bookmarkToggle: true,
    bookmarkRename: true,
    bookmarkNavigation: true,
    persistenceAndSearch: true,
    pageDeletionCleanupAndUndo: true,
    responsiveScreenshots: true,
  }, null, 2));
} finally {
  await browser.close();
}
