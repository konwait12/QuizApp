import assert from 'node:assert/strict';
import path from 'node:path';
import { createRequire } from 'node:module';
import { pathToFileURL } from 'node:url';

const nodeModules = process.env.PLAYWRIGHT_NODE_MODULES;
if (!nodeModules) throw new Error('Set PLAYWRIGHT_NODE_MODULES to the bundled node_modules directory');
const require = createRequire(pathToFileURL(path.join(nodeModules, 'quizapp-playwright-resolver.cjs')));
const { chromium } = require('playwright');
const appUrl = process.env.QUIZAPP_URL || 'http://127.0.0.1:8160/';
const fixture = path.resolve('tests/fixtures/notebook-pdf-test.pdf');

const browser = await chromium.launch({
  headless: true,
  ...(process.env.PLAYWRIGHT_CHROMIUM_EXECUTABLE ? { executablePath: process.env.PLAYWRIGHT_CHROMIUM_EXECUTABLE } : {}),
});
const page = await browser.newPage({ viewport: { width: 1180, height: 820 } });
const pageErrors = [];
page.on('pageerror', error => pageErrors.push(error.message));

try {
  await page.addInitScript(() => {
    localStorage.setItem('quizapp_ui_config', JSON.stringify({ autoUpdateCheck: false, autoAnnouncementCheck: false, autoBankUpdateCheck: false }));
    localStorage.setItem('quizapp_announcement_suppressed', '1');
  });
  await page.goto(appUrl, { waitUntil: 'domcontentloaded' });
  await page.waitForFunction(() => Boolean(window.pdfjsLib && window.QuizPdfNotebook && window.QuizNotebook && window.QuizBackup));
  const pdfNavigationMetadata = await page.evaluate(async () => {
    const fakePdf = {
      async getOutline() { return [{ title: '第一节', dest: [{ num: 3, gen: 0 }, { name: 'XYZ' }], items: [{ title: '子节', dest: 'named', items: [] }] }]; },
      async getDestination(name) { return name === 'named' ? [{ num: 7, gen: 0 }, { name: 'XYZ' }] : null; },
      async getPageIndex(reference) { return reference.num === 3 ? 0 : 1; },
    };
    const outline = await QuizPdfNotebook.extractPdfOutline(fakePdf);
    const links = await QuizPdfNotebook.extractPageLinks(fakePdf, {
      async getAnnotations() { return [{ subtype: 'Link', rect: [10, 20, 110, 60], dest: 'named', title: '下一节' }]; },
    }, {
      width: 200,
      height: 100,
      convertToViewportRectangle(rect) { return rect; },
    }, 1200, 600);
    return { outline, links };
  });
  assert.deepEqual(pdfNavigationMetadata.outline.map(item => [item.title, item.pageNumber, item.depth]), [['第一节', 1, 0], ['子节', 2, 1]]);
  assert.equal(pdfNavigationMetadata.links[0].targetPage, 2);
  assert.deepEqual(pdfNavigationMetadata.links[0].rect, { x: 60, y: 120, width: 600, height: 240 });
  await page.evaluate(() => document.querySelectorAll('.app-dialog-mask').forEach(element => element.remove()));
  await page.evaluate(() => openHandwritingPractice([], { freeMode: true }));
  await page.locator('.notebook-view').waitFor({ state: 'visible' });
  await page.waitForFunction(() => Boolean(state.notebookSession && state.notebookViewport));

  const documentId = await page.evaluate(() => state.notebookSession.document.id);
  await page.locator('#notebookPdfInput').setInputFiles(fixture);
  await page.getByRole('heading', { name: '导入 PDF 页面' }).waitFor({ state: 'visible' });
  assert.match(await page.locator('.app-dialog').innerText(), /2 页/);
  await page.getByRole('button', { name: '开始导入' }).click();
  await page.waitForFunction(() => state.notebookSession?.document.pages.length === 2 && !state.pendingPdfImport, null, { timeout: 60000 });
  await page.waitForFunction(() => Array.from(state.notebookViewport?.assetImageCache?.values?.() || []).some(item => item.image?.complete), null, { timeout: 30000 });

  const imported = await page.evaluate(async () => {
    const pages = state.notebookSession.document.pages;
    const assets = await getNotebookAssetRepository().listByDocument(state.notebookSession.document.id);
    const backup = await QuizBackup.create({ appVersion: APP_VERSION, databaseName: INK_DB_NAME });
    const inspection = await QuizBackup.inspect(backup);
    return {
      pageCount: pages.length,
      backgrounds: pages.map(page => ({
        type: page.background.type,
        assetId: page.background.assetId,
        searchText: page.background.searchText,
      })),
      assets: assets.map(asset => ({ id: asset.id, type: asset.type, dataUrl: asset.dataUrl.slice(0, 32) })),
      backupAssetCount: inspection.counts.recordCounts.notebook_assets || 0,
      backupValid: inspection.valid,
    };
  });
  assert.equal(imported.pageCount, 2);
  assert.ok(imported.backgrounds.every(item => item.type === 'pdf' && item.assetId));
  assert.match(imported.backgrounds[0].searchText, /QuizApp PDF Search Alpha/);
  assert.match(imported.backgrounds[1].searchText, /QuizApp PDF Search Beta/);
  assert.equal(imported.assets.length, 2);
  assert.ok(imported.assets.every(item => item.type === 'pdf-page' && item.dataUrl.startsWith('data:image/jpeg')));
  assert.ok(imported.backupAssetCount >= 2);
  assert.equal(imported.backupValid, true);

  const canvasBox = await page.locator('#notebookCanvas').boundingBox();
  assert.ok(canvasBox);
  await page.mouse.move(canvasBox.x + canvasBox.width * .45, canvasBox.y + canvasBox.height * .42);
  await page.mouse.down();
  await page.mouse.move(canvasBox.x + canvasBox.width * .62, canvasBox.y + canvasBox.height * .52, { steps: 12 });
  await page.mouse.up();
  assert.ok(await page.evaluate(() => state.notebookSession.layer.strokes.length > 0));

  const canvasHasVariation = await page.locator('#notebookCanvas').evaluate(canvas => {
    const context = canvas.getContext('2d');
    const pixels = context.getImageData(0, 0, canvas.width, canvas.height).data;
    let dark = 0;
    let light = 0;
    for (let index = 0; index < pixels.length; index += 64) {
      const value = pixels[index] + pixels[index + 1] + pixels[index + 2];
      if (value < 420) dark++;
      if (value > 700) light++;
    }
    return dark > 10 && light > 10;
  });
  assert.equal(canvasHasVariation, true);

  await page.evaluate(() => {
    getNotebookDockConfig().leftOpen = true;
    renderHandwritingPractice();
    setNotebookLeftTab('notebooks');
  });
  await page.locator('#notebookLeftPanel .notebook-search').fill('Beta');
  assert.equal(await page.locator('#notebookLeftPanel .notebook-list-item').count(), 1);
  await page.waitForTimeout(1700);
  await page.screenshot({ path: 'output/playwright/pdf-notebook-tablet.png', fullPage: true });

  await page.waitForFunction(() => getVisibleBanks().length > 0);
  await page.evaluate(async () => {
    const linked = QuizNotebook.createDocument({ id: 'linked-knowledge-test', title: 'Linked knowledge note', kind: 'free' });
    await getNotebookRepository().put(linked);
    await loadNotebookDocuments();
    getNotebookDockConfig().rightOpen = true;
    renderHandwritingPractice();
    setNotebookRightTab('info');
  });
  await page.getByRole('button', { name: '添加', exact: true }).click();
  await page.locator('.app-dialog input').fill('PDF重点');
  await page.getByRole('button', { name: '确定', exact: true }).click();
  await page.waitForFunction(() => state.notebookSession.document.tags.includes('PDF重点'));

  await page.getByRole('button', { name: '关联题目', exact: true }).click();
  await page.locator('#notebookQuestionLinkResults button').first().click();
  await page.waitForFunction(() => state.notebookSession.document.links.some(link => link.type === 'question'));

  await page.getByRole('button', { name: '关联笔记', exact: true }).click();
  await page.locator('.notebook-link-picker button').filter({ hasText: 'Linked knowledge note' }).click();
  await page.waitForFunction(() => state.notebookSession.document.links.some(link => link.type === 'notebook'));

  await page.getByRole('button', { name: '外部资料', exact: true }).click();
  await page.locator('.app-dialog input').fill('https://example.com/study');
  await page.getByRole('button', { name: '确定', exact: true }).click();
  await page.waitForFunction(() => state.notebookSession.document.links.some(link => link.type === 'url'));
  const metadata = await page.evaluate(() => ({
    tags: [...state.notebookSession.document.tags],
    linkTypes: state.notebookSession.document.links.map(link => link.type).sort(),
    indexed: state.notebookSearchIndex.get(state.notebookSession.document.id),
  }));
  assert.deepEqual(metadata.tags, ['PDF重点']);
  assert.deepEqual(metadata.linkTypes, ['notebook', 'question', 'url']);
  assert.match(metadata.indexed, /pdf重点/);
  assert.match(metadata.indexed, /example\.com/);
  await page.waitForTimeout(1700);
  await page.screenshot({ path: 'output/playwright/notebook-knowledge-links-tablet.png', fullPage: true });

  await page.setViewportSize({ width: 390, height: 844 });
  await page.evaluate(() => {
    state.notebookMobilePane = '';
    renderHandwritingPractice();
  });
  await page.locator('.notebook-view').waitFor({ state: 'visible' });
  await page.waitForTimeout(250);
  const mobileLayout = await page.evaluate(() => ({
    bodyWidth: document.body.scrollWidth,
    viewportWidth: window.innerWidth,
    pageCount: state.notebookSession.document.pages.length,
  }));
  assert.ok(mobileLayout.bodyWidth <= mobileLayout.viewportWidth + 1);
  assert.equal(mobileLayout.pageCount, 2);
  await page.screenshot({ path: 'output/playwright/pdf-notebook-mobile.png', fullPage: true });

  await page.evaluate(async () => {
    const firstPage = state.notebookSession.document.pages[0];
    state.notebookSession.setPage(firstPage.id);
    await setNotebookBackground('plain');
  });
  assert.equal(await page.evaluate(async () => (await getNotebookAssetRepository().listByDocument(state.notebookSession.document.id)).length), 1);
  await page.evaluate(() => {
    const pdfPage = state.notebookSession.document.pages.find(item => item.background?.type === 'pdf');
    state.notebookSession.setPage(pdfPage.id);
    confirmRemoveNotebookPage();
  });
  await page.getByRole('button', { name: '确认' }).click();
  await page.waitForFunction(async () => state.notebookSession.document.pages.length === 1 && (await getNotebookAssetRepository().listByDocument(state.notebookSession.document.id)).length === 0);

  await page.evaluate(async id => {
    await getNotebookAssetRepository().deleteByDocument(id);
    await getNotebookRepository().delete(id);
    await getNotebookRepository().delete('linked-knowledge-test');
  }, documentId);
  assert.deepEqual(pageErrors, []);
  console.log(JSON.stringify({
    pdfJsOfflineImport: true,
    pdfOutlineNavigation: true,
    pdfInternalLinks: true,
    twoPageAnnotation: true,
    extractedTextSearch: true,
    tagsAndKnowledgeLinks: true,
    separateAssetStorage: true,
    assetLifecycleCleanup: true,
    backupIncludesPdfAssets: true,
    responsiveScreenshots: true,
  }, null, 2));
} finally {
  await browser.close();
}
