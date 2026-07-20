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
  await page.waitForFunction(() => typeof openFreeNotebookLibrary === 'function' && !state.loadingDefaults);
  await page.evaluate(async () => {
    const stale = QuizNotebook.createDocument({
      id: 'builtin-pdf:zhangyu:math1:basic',
      title: '旧版数学笔记',
      kind: 'free',
    });
    stale.pages[0].bodyText = '旧记录正文';
    stale.pages[0].layers[0].strokes.push({
      id: 'legacy-stroke',
      tool: 'pen',
      color: '#101113',
      size: 3,
      points: [{ x: 20, y: 20, pressure: .5 }, { x: 60, y: 60, pressure: .5 }],
    });
    await getNotebookRepository().put(stale);
  });
  await page.evaluate(() => openFreeNotebookLibrary());
  const repairedBuiltIns = await page.evaluate(() => {
    const documentItem = state.notebookDocuments.find(item => item.id === 'builtin-pdf:zhangyu:math1:basic');
    return {
      count: state.notebookDocuments.filter(item => item.builtIn && item.pdfSource).length,
      builtIn: documentItem?.builtIn,
      sourceUrl: documentItem?.pdfSource?.url || '',
      pageCount: documentItem?.pages?.length || 0,
      firstPageSourceUrl: documentItem?.pages?.[0]?.background?.sourceUrl || '',
      bodyText: documentItem?.pages?.[0]?.bodyText || '',
      hasLegacyStroke: documentItem?.pages?.[0]?.layers?.some(layer => layer.strokes?.some(stroke => stroke.id === 'legacy-stroke')),
    };
  });
  assert.equal(repairedBuiltIns.count, 9);
  assert.equal(repairedBuiltIns.builtIn, true);
  assert.match(repairedBuiltIns.sourceUrl, /数学一-基础篇\.pdf$/);
  assert.equal(repairedBuiltIns.pageCount, 303);
  assert.equal(repairedBuiltIns.firstPageSourceUrl, repairedBuiltIns.sourceUrl);
  assert.equal(repairedBuiltIns.bodyText, '旧记录正文');
  assert.equal(repairedBuiltIns.hasLegacyStroke, true);
  await page.evaluate(() => openNotebookFromLibrary('builtin-pdf:zhangyu:math1:basic'));
  await page.waitForFunction(() => state.notebookSession?.document?.id === 'builtin-pdf:zhangyu:math1:basic' && Boolean(state.notebookSession.page.background.assetId), null, { timeout: 60000 });
  await page.locator('#notebookContinuousScroll').waitFor({ state: 'visible' });
  const initialReader = await page.evaluate(() => ({
    flow: document.getElementById('notebookContinuousScroll')?.dataset.pageFlow,
    pageElements: document.querySelectorAll('.notebook-continuous-page').length,
    canvases: document.querySelectorAll('.notebook-continuous-page canvas').length,
    scrollHeight: document.getElementById('notebookContinuousScroll')?.scrollHeight || 0,
    clientHeight: document.getElementById('notebookContinuousScroll')?.clientHeight || 0,
  }));
  assert.equal(initialReader.flow, 'vertical');
  assert.equal(initialReader.pageElements, 303, 'the PDF reader should expose the complete document as a lightweight page flow');
  assert.ok(initialReader.canvases <= 7, 'a 303-page PDF must not allocate hundreds of canvases');
  assert.ok(initialReader.scrollHeight > initialReader.clientHeight * 20, 'the complete document should be scrollable without paging buttons');

  const wheelBefore = await page.evaluate(() => ({
    scrollTop: document.getElementById('notebookContinuousScroll').scrollTop,
    scale: state.notebookViewport.scale,
  }));
  const builtInCanvas = page.locator('#notebookCanvas');
  const builtInCanvasBox = await builtInCanvas.boundingBox();
  assert.ok(builtInCanvasBox);
  await page.mouse.move(builtInCanvasBox.x + builtInCanvasBox.width / 2, builtInCanvasBox.y + builtInCanvasBox.height / 2);
  await page.mouse.wheel(0, 520);
  await page.waitForTimeout(250);
  const wheelAfter = await page.evaluate(() => ({
    scrollTop: document.getElementById('notebookContinuousScroll').scrollTop,
    scale: state.notebookViewport.scale,
  }));
  assert.ok(wheelAfter.scrollTop > wheelBefore.scrollTop + 120, 'ordinary wheel input over the page should scroll the document');
  assert.equal(wheelAfter.scale, wheelBefore.scale, 'ordinary wheel input must not zoom the handwriting canvas');

  await page.keyboard.down('Control');
  await page.mouse.wheel(0, -180);
  await page.keyboard.up('Control');
  await page.waitForTimeout(100);
  assert.ok(await page.evaluate(scale => state.notebookViewport.scale > scale, wheelAfter.scale), 'Ctrl+wheel should remain an explicit page zoom gesture');

  await page.evaluate(() => {
    const targetId = state.notebookSession.document.pages[9].id;
    const scroller = document.getElementById('notebookContinuousScroll');
    const target = scroller.querySelector(`[data-continuous-page-id="${CSS.escape(targetId)}"]`);
    scroller.scrollTop = target.offsetTop - Math.max(0, (scroller.clientHeight - target.offsetHeight) / 2);
    scroller.dispatchEvent(new Event('scroll'));
  });
  await page.waitForFunction(() => state.notebookSession?.page?.background?.sourcePage === 10, null, { timeout: 60000 });
  const pageTenReaderState = await page.evaluate(() => ({
    pageStatus: document.querySelector('.notebook-page-status span')?.textContent,
    pageElements: document.querySelectorAll('.notebook-continuous-page').length,
    scrollTop: document.getElementById('notebookContinuousScroll')?.scrollTop || 0,
  }));
  assert.equal(pageTenReaderState.pageStatus, '10/303');
  assert.equal(pageTenReaderState.pageElements, 303);
  assert.ok(pageTenReaderState.scrollTop > 0, 'activating a page from scrolling must preserve the document position');

  await page.evaluate(() => setNotebookPageFlow('horizontal'));
  await page.waitForFunction(() => document.getElementById('notebookContinuousScroll')?.dataset.pageFlow === 'horizontal');
  const horizontalReader = await page.evaluate(() => {
    const scroller = document.getElementById('notebookContinuousScroll');
    const scrollerRect = scroller.getBoundingClientRect();
    const active = scroller.querySelector('.notebook-continuous-page.active');
    const activeRect = active.getBoundingClientRect();
    return {
      pageElements: scroller.querySelectorAll('.notebook-continuous-page').length,
      scrollSnapType: getComputedStyle(scroller).scrollSnapType,
      scrollWidth: scroller.scrollWidth,
      clientWidth: scroller.clientWidth,
      clientHeight: scroller.clientHeight,
      activeHeight: activeRect.height,
      activeVisible: activeRect.right > scrollerRect.left && activeRect.left < scrollerRect.right,
    };
  });
  assert.equal(horizontalReader.pageElements, 303);
  assert.equal(horizontalReader.scrollSnapType, 'none');
  assert.ok(horizontalReader.scrollWidth > horizontalReader.clientWidth * 20);
  assert.ok(horizontalReader.activeHeight > horizontalReader.clientHeight * .72, 'horizontal reading should use full-size pages instead of a thumbnail strip');
  assert.equal(horizontalReader.activeVisible, true, 'switching page flow should keep the current page visible');
  const horizontalBefore = await page.evaluate(() => document.getElementById('notebookContinuousScroll').scrollLeft);
  await page.evaluate(() => document.getElementById('notebookContinuousScroll').dispatchEvent(new WheelEvent('wheel', {
    deltaY: 520,
    bubbles: true,
    cancelable: true,
  })));
  await page.waitForTimeout(250);
  await page.waitForFunction(() => state.notebookSession?.page?.background?.sourcePage === 11, null, { timeout: 60000 });
  const horizontalAfter = await page.evaluate(() => document.getElementById('notebookContinuousScroll').scrollLeft);
  assert.ok(horizontalAfter > horizontalBefore + 120, `vertical wheel input should turn pages in horizontal mode (${horizontalBefore} -> ${horizontalAfter})`);
  await page.screenshot({ path: 'output/playwright/built-in-math-pdf-horizontal.png', fullPage: true });
  await page.evaluate(() => setNotebookPageFlow('vertical'));
  await page.waitForFunction(() => document.getElementById('notebookContinuousScroll')?.dataset.pageFlow === 'vertical');
  await page.evaluate(() => setNotebookPage(state.notebookSession.document.pages[0].id, 'navigation'));
  await page.waitForFunction(() => state.notebookSession?.page?.background?.sourcePage === 1, null, { timeout: 60000 });

  const builtInFirstPage = await page.evaluate(async () => ({
    totalPages: state.notebookSession.document.pages.length,
    activeSourcePage: state.notebookSession.page.background.sourcePage,
    cachedSourcePages: (await getNotebookAssetRepository().listByDocument(state.notebookSession.document.id)).map(asset => asset.pageId),
    firstPageId: state.notebookSession.page.id,
  }));
  assert.equal(builtInFirstPage.totalPages, 303);
  assert.equal(builtInFirstPage.activeSourcePage, 1);
  assert.ok(builtInFirstPage.cachedSourcePages.includes(builtInFirstPage.firstPageId));
  await page.evaluate(() => setNotebookPage(state.notebookSession.document.pages[1].id));
  await page.waitForFunction(() => state.notebookSession?.page?.background?.sourcePage === 2 && Boolean(state.notebookSession.page.background.assetId), null, { timeout: 60000 });
  const builtInSecondPage = await page.evaluate(async firstPageId => ({
    activeSourcePage: state.notebookSession.page.background.sourcePage,
    cachedPageIds: (await getNotebookAssetRepository().listByDocument(state.notebookSession.document.id)).map(asset => asset.pageId),
    firstPageId,
    secondPageId: state.notebookSession.page.id,
  }), builtInFirstPage.firstPageId);
  assert.equal(builtInSecondPage.activeSourcePage, 2);
  assert.ok(builtInSecondPage.cachedPageIds.includes(builtInSecondPage.firstPageId), 'page navigation should retain an already rendered page');
  assert.ok(builtInSecondPage.cachedPageIds.includes(builtInSecondPage.secondPageId), 'page navigation should cache the newly opened page');
  await page.evaluate(() => setNotebookPage(state.notebookSession.document.pages[9].id));
  await page.waitForFunction(() => state.notebookSession?.page?.background?.sourcePage === 10 && Boolean(state.notebookSession.page.background.assetId), null, { timeout: 60000 });
  await page.screenshot({ path: 'output/playwright/built-in-math-pdf-page-10.png', fullPage: true });
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
  const importDocumentId = await page.evaluate(async () => {
    const documentItem = QuizNotebook.createDocument({ title: 'PDF 导入测试', kind: 'free' });
    await getNotebookRepository().put(documentItem);
    return documentItem.id;
  });
  await page.evaluate(id => openHandwritingPractice([], { freeMode: true, documentId: id }), importDocumentId);
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
    state.notebookMobilePane = 'left';
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
    state.notebookMobilePane = 'right';
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
  const protectedPdfPaper = await page.evaluate(async () => ({
    type: state.notebookSession.page.background.type,
    assets: (await getNotebookAssetRepository().listByDocument(state.notebookSession.document.id)).length,
  }));
  assert.deepEqual(protectedPdfPaper, { type: 'pdf', assets: 2 }, 'ordinary paper templates must not overwrite PDF pages or delete their assets');
  await page.evaluate(() => {
    const pdfPage = state.notebookSession.document.pages.find(item => item.background?.type === 'pdf');
    state.notebookSession.setPage(pdfPage.id);
    confirmRemoveNotebookPage();
  });
  await page.getByRole('button', { name: '确认' }).click();
  await page.waitForFunction(async () => state.notebookSession.document.pages.length === 1 && (await getNotebookAssetRepository().listByDocument(state.notebookSession.document.id)).length === 1);

  await page.evaluate(async id => {
    await getNotebookAssetRepository().deleteByDocument(id);
    await getNotebookRepository().delete(id);
    await getNotebookRepository().delete('linked-knowledge-test');
  }, documentId);
  assert.deepEqual(pageErrors, []);
  console.log(JSON.stringify({
    pdfJsOfflineImport: true,
    builtInPdfLazyLoading: true,
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
