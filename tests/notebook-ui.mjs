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
      localStorage.setItem('quizapp_ui_config', JSON.stringify({ autoUpdateCheck: false, autoAnnouncementCheck: false, autoBankUpdateCheck: false }));
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
  const zoomBehavior = await page.evaluate(() => {
    const viewport = state.notebookViewport;
    const rect = viewport.canvas.getBoundingClientRect();
    const anchor = { x: rect.left + rect.width / 2, y: rect.top + rect.height / 2 };
    const before = viewport.screenToPage(anchor.x, anchor.y);
    viewport.zoomAt(anchor.x, anchor.y, 1.7);
    const after = viewport.screenToPage(anchor.x, anchor.y);
    viewport.offsetX = 100000;
    viewport.offsetY = -100000;
    viewport.constrainViewport();
    const constrained = {
      left: viewport.offsetX,
      top: viewport.offsetY,
      scaledWidth: viewport.session.page.width * viewport.scale,
      scaledHeight: viewport.session.page.height * viewport.scale,
      viewWidth: rect.width,
      viewHeight: rect.height,
      margin: viewport.panMargin,
    };
    viewport.fit();
    return { before, after, constrained };
  });
  assert.ok(Math.abs(zoomBehavior.before.x - zoomBehavior.after.x) < .001, 'tablet zoom should stay anchored on X');
  assert.ok(Math.abs(zoomBehavior.before.y - zoomBehavior.after.y) < .001, 'tablet zoom should stay anchored on Y');
  assert.ok(zoomBehavior.constrained.left <= zoomBehavior.constrained.viewWidth - Math.min(zoomBehavior.constrained.viewWidth, zoomBehavior.constrained.scaledWidth) + zoomBehavior.constrained.margin);
  const minimumTop = zoomBehavior.constrained.scaledHeight <= zoomBehavior.constrained.viewHeight
    ? -zoomBehavior.constrained.margin
    : zoomBehavior.constrained.viewHeight - zoomBehavior.constrained.scaledHeight - zoomBehavior.constrained.margin;
  assert.ok(zoomBehavior.constrained.top >= minimumTop);
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
    setNotebookTool('select');
    state.notebookViewport.requestRender();
    const rect = state.notebookViewport.canvas.getBoundingClientRect();
    const point = (x, y) => ({
      x: rect.left + state.notebookViewport.offsetX + x * state.notebookViewport.scale,
      y: rect.top + state.notebookViewport.offsetY + y * state.notebookViewport.scale,
    });
    return {
      points: [point(80, 120), point(680, 120), point(680, 470), point(80, 470), point(80, 120)],
    };
  });
  await page.mouse.move(drag.points[0].x, drag.points[0].y);
  await page.mouse.down();
  for (const point of drag.points.slice(1)) await page.mouse.move(point.x, point.y, { steps: 5 });
  await page.mouse.up();
  assert.equal(await page.evaluate(() => state.notebookSession.getSelectionItems().length), 2);
  const objectCount = await page.evaluate(() => state.notebookSession.page.objects.length);
  await page.getByTitle('复制所选').click();
  await page.getByTitle('粘贴').click();
  assert.equal(await page.evaluate(() => state.notebookSession.page.objects.length), objectCount + 2);

  await page.getByLabel('马克笔').click();
  await page.getByLabel('直线模式').click();
  const line = await page.evaluate(() => {
    const viewport = state.notebookViewport;
    const rect = viewport.canvas.getBoundingClientRect();
    const point = (x, y) => ({
      x: rect.left + viewport.offsetX + x * viewport.scale,
      y: rect.top + viewport.offsetY + y * viewport.scale,
    });
    return { start: point(140, 760), end: point(640, 760) };
  });
  await page.mouse.move(line.start.x, line.start.y);
  await page.mouse.down();
  await page.mouse.move(line.end.x, line.end.y, { steps: 12 });
  await page.mouse.up();
  const markerStroke = await page.evaluate(() => state.notebookSession.layer.strokes.at(-1));
  assert.equal(markerStroke.tool, 'marker');
  assert.equal(markerStroke.points.length, 2, 'straight-line mode should retain only its endpoints');

  const layerCountBeforeMerge = await page.evaluate(() => {
    state.notebookSession.addLayer('待合并图层');
    state.notebookRightTab = 'layers';
    renderNotebookRightPanel();
    return state.notebookSession.page.layers.length;
  });
  await page.getByRole('button', { name: '向下合并' }).click();
  assert.equal(await page.evaluate(() => state.notebookSession.page.layers.length), layerCountBeforeMerge - 1);

  const snbxRoundTrip = await page.evaluate(async () => {
    const document = state.notebookSession.document;
    const dataUrl = `data:text/plain;base64,${btoa('asset-round-trip')}`;
    const result = await QuizNotebookExport.createSnbx(document, [{
      id: 'asset:test', documentId: document.id, pageId: document.pages[0].id, mimeType: 'text/plain', dataUrl,
    }]);
    const unpacked = await QuizNotebookExport.readSnbx(result.bytes);
    return {
      filename: result.filename,
      title: unpacked.document.title,
      assetCount: unpacked.assets.length,
      assetDataUrl: unpacked.assets[0]?.dataUrl || '',
    };
  });
  assert.match(snbxRoundTrip.filename, /\.snbx$/);
  assert.equal(snbxRoundTrip.assetCount, 1);
  assert.match(snbxRoundTrip.assetDataUrl, /^data:text\/plain;base64,/);

  await page.getByTitle('新增页面').click();
  await page.getByTitle('新增页面').click();
  assert.equal(await page.locator('.notebook-page-thumb').count(), 3);
  assert.equal(await page.locator('[data-notebook-page-preview]').count(), 3);
  const draggedPageId = await page.locator('.notebook-page-thumb').nth(2).getAttribute('data-page-id');
  await page.locator('.notebook-page-thumb').nth(2).dragTo(page.locator('.notebook-page-thumb').nth(0));
  assert.equal(await page.evaluate(() => state.notebookSession.document.pages[0].id), draggedPageId);
  const renamedPage = await page.evaluate(() => {
    const pageId = state.notebookSession.document.activePageId;
    return state.notebookSession.renamePage(pageId, '课堂推导');
  });
  assert.equal(renamedPage, true);
  assert.equal(await page.evaluate(() => state.notebookSession.page.name), '课堂推导');

  const thumbnailHasPixels = await page.locator('[data-notebook-page-preview]').first().evaluate(canvas => {
    const pixels = canvas.getContext('2d').getImageData(0, 0, canvas.width, canvas.height).data;
    return pixels.some((value, index) => index % 4 !== 3 && value !== 0);
  });
  assert.equal(thumbnailHasPixels, true);
  await page.screenshot({ path: 'output/playwright/notebook-tablet-advanced.png', fullPage: true });

  const dockState = await page.evaluate(() => {
    const dock = getNotebookDockConfig();
    dock.navigatorPosition = 'top';
    dock.leftOpen = true;
    dock.rightOpen = true;
    dock.leftWidth = 312;
    dock.rightWidth = 368;
    dock.panBoundary = 144;
    saveNotebookDockConfig();
    renderHandwritingPractice();
    return getNotebookDockConfig();
  });
  assert.equal(dockState.navigatorPosition, 'top');
  assert.equal(dockState.panBoundary, 144);
  assert.equal(await page.locator('.notebook-top-navigator').count(), 1);
  await page.getByTitle('收起顶部目录').click();
  assert.equal(await page.locator('.notebook-top-navigator').count(), 0);
  const collapsedRows = await page.locator('.notebook-center').evaluate(element => getComputedStyle(element).gridTemplateRows);
  assert.ok(!collapsedRows.split(' ').some(value => Number.parseFloat(value) >= 140 && Number.parseFloat(value) <= 160), `collapsed top navigator should not reserve its former row: ${collapsedRows}`);
  await page.locator('.notebook-dock-toggle.left').click();
  assert.equal(await page.locator('.notebook-top-navigator').count(), 1);
  const persistedDock = await page.evaluate(() => JSON.parse(localStorage.getItem(NOTEBOOK_DOCK_KEY)));
  assert.equal(persistedDock.navigatorPosition, 'top');
  assert.equal(persistedDock.leftOpen, true);
  await page.screenshot({ path: 'output/playwright/notebook-dock-top-verified.png', fullPage: true });

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
  const tabIds = await page.evaluate(async () => {
    const first = QuizNotebook.createDocument({ title: '高等数学笔记', kind: 'free', mode: 'edgeless' });
    const second = QuizNotebook.createDocument({ title: '英语阅读笔记', kind: 'free' });
    await getNotebookRepository().put(first);
    await getNotebookRepository().put(second);
    await loadNotebookDocuments();
    state.notebookOpenTabs = [first.id, second.id];
    renderHandwritingPractice();
    return [first.id, second.id];
  });
  assert.equal(await page.locator('.notebook-document-tab').count(), 2);
  await page.locator('.notebook-document-tab').filter({ hasText: '高等数学笔记' }).click();
  await page.waitForFunction(() => state.notebookSession?.document.title === '高等数学笔记');
  assert.equal(await page.evaluate(() => state.notebookSession.document.title), '高等数学笔记');
  assert.equal(await page.evaluate(() => state.notebookSession.document.mode), 'edgeless');
  assert.equal(await page.locator('.notebook-edgeless-status').count(), 1);
  const edgelessExpansion = await page.evaluate(() => {
    const viewport = state.notebookViewport;
    const object = state.notebookSession.addObject('text', { text: 'stable' }, { x: 900, y: 700, width: 120, height: 80 });
    viewport.scale = 1;
    viewport.offsetX = 480;
    viewport.offsetY = 400;
    const before = { x: object.x + viewport.offsetX, y: object.y + viewport.offsetY };
    const width = state.notebookSession.page.width;
    viewport.ensureEdgelessViewport();
    const after = { x: object.x + viewport.offsetX, y: object.y + viewport.offsetY };
    viewport.requestRender();
    return { before, after, expanded: state.notebookSession.page.width > width };
  });
  assert.deepEqual(edgelessExpansion.after, edgelessExpansion.before);
  assert.equal(edgelessExpansion.expanded, true);
  await page.locator('.notebook-document-tab').filter({ hasText: '英语阅读笔记' }).click();
  await page.waitForFunction(id => state.notebookSession?.document.id === id, tabIds[1]);
  assert.equal(await page.evaluate(() => state.notebookSession.document.id), tabIds[1]);
  await page.locator('.notebook-document-tab').filter({ hasText: '高等数学笔记' }).locator('[aria-label="关闭标签"]').click();
  assert.equal(await page.locator('.notebook-document-tab').count(), 1);

  await page.evaluate(() => showNotebookBatchExportDialog());
  await page.locator('[aria-label="批量导出笔记"]').waitFor({ state: 'visible' });
  await page.evaluate(ids => {
    document.querySelectorAll('[data-notebook-batch-id]').forEach(input => { input.checked = ids.includes(input.dataset.notebookBatchId); });
  }, tabIds);
  const downloadPromise = page.waitForEvent('download');
  await page.getByRole('button', { name: '导出 SNBX' }).click();
  const download = await downloadPromise;
  assert.match(download.suggestedFilename(), /QuizApp-笔记批量导出-.*\.zip$/);

  await page.locator('.notebook-topbar .btn-back').click();
  await page.locator('.quiz-topbar').waitFor({ state: 'visible' });
  await page.waitForFunction(expected => {
    const actual = document.querySelector('.page-slide.current-slide')?.scrollTop || 0;
    return Math.abs(actual - expected) <= 1;
  }, quizContext.scrollTop);
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
    markerAndStraightLine: true,
    mergeLayerDownUi: true,
    snbxRoundTrip: true,
    copyPaste: true,
    pageThumbnailPixels: true,
    pageDragReorder: true,
    pageRename: true,
    dockCollapseAndTopNavigation: true,
    multiDocumentTabs: true,
    edgelessCanvasUi: true,
    batchSnbxExport: true,
    anchoredZoomAndBoundedPan: true,
    responsiveScreenshots: true,
  }, null, 2));
} finally {
  await browser.close();
}
