import assert from 'node:assert/strict';
import path from 'node:path';
import { createRequire } from 'node:module';
import { pathToFileURL } from 'node:url';

const nodeModules = process.env.PLAYWRIGHT_NODE_MODULES;
if (!nodeModules) throw new Error('Set PLAYWRIGHT_NODE_MODULES to the bundled node_modules directory');
const require = createRequire(pathToFileURL(path.join(nodeModules, 'quizapp-playwright-resolver.cjs')));
const { chromium } = require('playwright');

const browser = await chromium.launch({
  headless: true,
  ...(process.env.PLAYWRIGHT_CHROMIUM_EXECUTABLE ? { executablePath: process.env.PLAYWRIGHT_CHROMIUM_EXECUTABLE } : {}),
});
const page = await browser.newPage({ viewport: { width: 1180, height: 820 } });
const errors = [];
page.on('pageerror', error => errors.push(error.message));

try {
  await page.addInitScript(() => {
    localStorage.setItem('quizapp_ui_config', JSON.stringify({ autoUpdateCheck: false, autoAnnouncementCheck: false, autoBankUpdateCheck: false }));
    localStorage.setItem('quizapp_announcement_suppressed', '1');
  });
  await page.goto(pathToFileURL(path.resolve('index.html')).href, { waitUntil: 'domcontentloaded' });
  await page.waitForFunction(() => Boolean(window.pdfjsLib && window.QuizPdfNotebook && window.QuizNotebook));
  await page.evaluate(async () => {
    document.querySelectorAll('.app-dialog-mask').forEach(element => element.remove());
    await openHandwritingPractice([], { freeMode: true });
  });
  await page.locator('#notebookPdfInput').setInputFiles(path.resolve('tests/fixtures/notebook-pdf-test.pdf'));
  await page.locator('.app-dialog').waitFor({ state: 'visible' });
  await page.locator('.app-dialog-actions button').last().click();
  await page.waitForFunction(() => state.notebookSession?.document.pages.length === 2 && !state.pendingPdfImport, null, { timeout: 60000 });
  const result = await page.evaluate(() => ({
    id: state.notebookSession.document.id,
    pageTypes: state.notebookSession.document.pages.map(item => item.background.type),
  }));
  assert.deepEqual(result.pageTypes, ['pdf', 'pdf']);
  assert.deepEqual(errors, []);
  await page.evaluate(async id => {
    await getNotebookAssetRepository().deleteByDocument(id);
    await getNotebookRepository().delete(id);
  }, result.id);
  console.log(JSON.stringify({ fileProtocolPdfImport: true, pageErrors: false }, null, 2));
} finally {
  await browser.close();
}
