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
const consoleErrors = [];
const requestFailures = [];
page.on('pageerror', error => pageErrors.push(error.message));
page.on('console', message => { if (message.type() === 'error') consoleErrors.push(message.text()); });
page.on('requestfailed', request => requestFailures.push(`${request.method()} ${request.url()} ${request.failure()?.errorText || ''}`));

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
  const questionStrip = page.locator('.notebook-question-strip');
  await questionStrip.waitFor({ state: 'visible' });
  assert.equal(await questionStrip.getAttribute('open'), '');
  assert.match(await questionStrip.innerText(), new RegExp(statefulText(await page.evaluate(() => getCurrentNotebookQuestion().q)).slice(0, 12)));
  assert.equal(await page.locator('.notebook-workspace.note-editor-layout:not(.show-right)').count(), 1, 'question handwriting should keep the right drawer closed by default');
  const questionToolbarPlacement = await page.evaluate(() => {
    const stripRect = document.querySelector('.notebook-question-strip').getBoundingClientRect();
    const toolbarRect = document.querySelector('.notebook-toolbar-stack').getBoundingClientRect();
    return { stripBottom: stripRect.bottom, toolbarTop: toolbarRect.top };
  });
  assert.ok(questionToolbarPlacement.toolbarTop >= questionToolbarPlacement.stripBottom, `writing tools should start below the expanded question: ${JSON.stringify(questionToolbarPlacement)}`);
  const canvasWidthBeforeDrawer = await page.locator('.notebook-center').evaluate(element => element.getBoundingClientRect().width);
  await page.getByRole('button', { name: '资料与目录' }).click();
  await page.locator('.notebook-workspace.show-left').waitFor({ state: 'visible' });
  assert.equal(await page.locator('.notebook-center').evaluate(element => element.getBoundingClientRect().width), canvasWidthBeforeDrawer, 'opening the directory drawer must not shrink the writing canvas');
  const compactQuestionHeight = await page.locator('.notebook-list-item').first().evaluate(element => element.getBoundingClientRect().height);
  assert.ok(compactQuestionHeight <= 40, `question picker should stay compact: ${compactQuestionHeight}`);
  await page.getByRole('button', { name: '关闭目录' }).click();
  const initialPagePosition = await page.locator('.notebook-continuous-scroll').evaluate(scroller => {
    const pageElement = scroller.querySelector('.notebook-continuous-page.active');
    const scrollerRect = scroller.getBoundingClientRect();
    const pageRect = pageElement.getBoundingClientRect();
    return { pageTop: pageRect.top, viewportTop: scrollerRect.top, viewportBottom: scrollerRect.bottom };
  });
  assert.ok(initialPagePosition.pageTop >= initialPagePosition.viewportTop - 1, `initial notebook page should open at its top: ${JSON.stringify(initialPagePosition)}`);
  assert.ok(initialPagePosition.pageTop < initialPagePosition.viewportBottom, 'initial notebook page should be visible');
  await page.getByTitle('更多操作').click();
  await page.getByRole('button', { name: '纸张模板', exact: true }).click();
  const paperDialog = page.getByRole('dialog', { name: '纸张模板' });
  await paperDialog.waitFor({ state: 'visible' });
  assert.equal(await paperDialog.locator('[data-notebook-paper-template]').count(), 8, 'paper picker should expose the StarNote-inspired template groups');
  const paperScrollLayout = await paperDialog.evaluate(dialog => {
    const groups = dialog.querySelector('.notebook-paper-groups');
    return {
      dialogOverflow: getComputedStyle(dialog).overflowY,
      groupsOverflow: getComputedStyle(groups).overflowY,
      rows: getComputedStyle(dialog).gridTemplateRows.split(' ').length,
    };
  });
  assert.equal(paperScrollLayout.dialogOverflow, 'hidden', 'paper dialog should not create a second scroll container');
  assert.equal(paperScrollLayout.groupsOverflow, 'auto', 'only the paper template list should scroll');
  assert.equal(paperScrollLayout.rows, 5, 'paper dialog should keep its header, scope, list, spacing, and actions in separate rows');
  await paperDialog.getByRole('button', { name: '新增页', exact: true }).click();
  await paperDialog.getByRole('button', { name: '康奈尔', exact: true }).click();
  await paperDialog.locator('#notebookPaperSpacing').evaluate(element => {
    element.value = '52';
    element.dispatchEvent(new Event('change', { bubbles: true }));
  });
  await page.waitForFunction(() => state.notebookSession.document.defaultPageBackground?.template === 'cornell' && state.notebookSession.document.defaultPageBackground?.spacing === 52);
  assert.equal(await page.evaluate(() => state.notebookSession.page.background.template), 'grid', 'new-page paper settings should not alter the current page');
  await page.screenshot({ path: 'output/playwright/notebook-paper-templates.png', fullPage: true });
  await paperDialog.getByRole('button', { name: '完成' }).click();

  await page.getByTitle('更多操作').click();
  await page.getByRole('button', { name: '自定义工具栏', exact: true }).click();
  let toolbarDialog = page.getByRole('dialog', { name: '自定义工具栏' });
  await toolbarDialog.waitFor({ state: 'visible' });
  await page.screenshot({ path: 'output/playwright/notebook-custom-toolbar.png', fullPage: true });
  await toolbarDialog.getByRole('checkbox', { name: '显示马克笔' }).uncheck();
  assert.equal(await page.locator('[data-notebook-tool="marker"]').count(), 0, 'hidden notebook tools should leave the primary toolbar');
  assert.equal(await page.evaluate(() => JSON.parse(localStorage.getItem(UI_CONFIG_KEY)).notebookToolbarTools.includes('marker')), false);
  await toolbarDialog.getByRole('button', { name: '下移钢笔' }).click();
  toolbarDialog = page.getByRole('dialog', { name: '自定义工具栏' });
  assert.equal(await page.locator('[data-notebook-tool]').first().getAttribute('data-notebook-tool'), 'highlighter', 'toolbar order should update immediately');
  await toolbarDialog.getByRole('button', { name: '恢复默认' }).click();
  toolbarDialog = page.getByRole('dialog', { name: '自定义工具栏' });
  assert.equal(await page.locator('[data-notebook-tool]').first().getAttribute('data-notebook-tool'), 'pen');
  assert.equal(await page.locator('[data-notebook-tool="marker"]').count(), 1, 'reset should restore hidden tools');
  await toolbarDialog.getByRole('button', { name: '完成' }).click();
  await page.getByTitle('工具设置').click();
  await page.getByRole('button', { name: '快捷笔盒' }).click();
  const penCaseDialog = page.getByRole('dialog', { name: '快捷笔盒' });
  await penCaseDialog.waitFor({ state: 'visible' });
  assert.equal(await penCaseDialog.locator('[data-notebook-pen-preset]').count(), 4, 'quick pen case should expose four useful presets');
  assert.match(await penCaseDialog.locator('.notebook-pen-case-grid').evaluate(element => getComputedStyle(element).gridTemplateColumns), /\S+\s+\S+/, 'desktop pen case should use two compact columns');
  await page.screenshot({ path: 'output/playwright/notebook-pen-case.png', fullPage: true });
  await penCaseDialog.getByRole('button', { name: '黄色重点' }).click();
  const selectedPenPreset = await page.evaluate(() => ({
    tool: state.inkTool,
    color: state.inkColor,
    size: state.inkSize,
    viewportTool: state.notebookViewport.tool,
    viewportColor: state.notebookViewport.color,
    viewportSize: state.notebookViewport.size,
  }));
  assert.deepEqual(selectedPenPreset, {
    tool: 'highlighter',
    color: '#f2c94c',
    size: 12,
    viewportTool: 'highlighter',
    viewportColor: '#f2c94c',
    viewportSize: 12,
  }, 'a pen-case preset should switch tool, color, and width atomically');
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
    state.notebookMobilePane = 'right';
    renderHandwritingPractice();
    state.notebookSession.addLayer('待合并图层');
    state.notebookRightTab = 'layers';
    renderNotebookRightPanel();
    return state.notebookSession.page.layers.length;
  });
  await page.getByRole('button', { name: '向下合并' }).click();
  assert.equal(await page.evaluate(() => state.notebookSession.page.layers.length), layerCountBeforeMerge - 1);
  await page.getByRole('button', { name: '关闭面板' }).click();

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

  await page.getByTitle('在末尾新增页面', { exact: true }).evaluate(button => button.click());
  await page.getByTitle('在末尾新增页面', { exact: true }).evaluate(button => button.click());
  assert.equal(await page.locator('.notebook-page-thumb').count(), 3);
  const pageTemplates = await page.evaluate(() => state.notebookSession.document.pages.map(item => ({ template: item.background.template, spacing: item.background.spacing })));
  assert.deepEqual(pageTemplates, [
    { template: 'grid', spacing: 40 },
    { template: 'cornell', spacing: 52 },
    { template: 'cornell', spacing: 52 },
  ], 'new pages should use the separately configured paper default');
  await page.screenshot({ path: 'output/playwright/notebook-cornell-pages.png', fullPage: true });
  await page.evaluate(async () => {
    state.notebookPaperScope = 'all';
    await applyNotebookPaperTemplate('grid');
  });
  assert.equal(await page.evaluate(() => state.notebookSession.document.pages.every(item => item.background.template === 'grid')), true, 'whole-notebook paper changes should update every regular page');
  assert.equal(await page.locator('.notebook-continuous-page').count(), 3);
  const verticalFlow = await page.locator('.notebook-continuous-scroll').evaluate(element => ({
    flow: element.dataset.pageFlow,
    overflow: element.scrollHeight > element.clientHeight,
  }));
  assert.equal(verticalFlow.flow, 'vertical');
  assert.equal(verticalFlow.overflow, true, 'three notebook pages should form a vertically scrollable document');
  const preservedPageScroll = await page.evaluate(() => {
    const scroller = document.getElementById('notebookContinuousScroll');
    const target = scroller.querySelectorAll('.notebook-continuous-page')[1];
    scroller.scrollTop = target.offsetTop + 72;
    const before = scroller.scrollTop;
    const targetId = target.dataset.continuousPageId;
    target.dispatchEvent(new MouseEvent('click', { bubbles: true }));
    return { before, targetId };
  });
  await page.waitForFunction(targetId => document.querySelector('.notebook-continuous-page.active')?.dataset.continuousPageId === targetId, preservedPageScroll.targetId);
  const restoredPageScroll = await page.locator('.notebook-continuous-scroll').evaluate(scroller => scroller.scrollTop);
  assert.ok(Math.abs(restoredPageScroll - preservedPageScroll.before) <= 1, `selecting a visible continuous page should preserve the reading position: ${JSON.stringify({ before: preservedPageScroll.before, after: restoredPageScroll })}`);
  await page.getByTitle('更多操作').click();
  await page.getByRole('button', { name: '页面设置', exact: true }).click();
  await page.getByRole('button', { name: '横向', exact: true }).click();
  await page.waitForFunction(() => state.notebookSession.page.width > state.notebookSession.page.height);
  await page.waitForFunction(() => {
    const element = document.querySelector('.notebook-continuous-page.active');
    return element && parseFloat(element.style.getPropertyValue('--notebook-page-ratio')) > 1 && element.getBoundingClientRect().width > 0;
  });
  const landscapePaperBounds = await page.locator('.notebook-continuous-page.active').evaluate(element => {
    const rect = element.getBoundingClientRect();
    return { width: rect.width, height: rect.height, ratio: getComputedStyle(element).aspectRatio, style: element.getAttribute('style') };
  });
  assert.ok(landscapePaperBounds.width > landscapePaperBounds.height, `horizontal paper should be physically wider than it is tall: ${JSON.stringify(landscapePaperBounds)}`);
  await page.getByTitle('更多操作').click();
  await page.getByRole('button', { name: '页面设置', exact: true }).click();
  await page.getByRole('button', { name: '左右翻页', exact: true }).click();
  const horizontalFlow = await page.locator('.notebook-continuous-scroll').evaluate(element => ({
    flow: element.dataset.pageFlow,
    overflow: element.scrollWidth > element.clientWidth,
  }));
  assert.equal(horizontalFlow.flow, 'horizontal');
  assert.equal(horizontalFlow.overflow, true, 'horizontal mode should lay pages out to the right');
  await page.screenshot({ path: 'output/playwright/notebook-horizontal-flow.png', fullPage: true });
  await page.getByTitle('更多操作').click();
  await page.getByRole('button', { name: '页面设置', exact: true }).click();
  await page.getByRole('button', { name: '上下翻页', exact: true }).click();
  assert.equal(await page.locator('[data-notebook-page-preview]').count(), 5);
  await page.getByTitle('页面概览').click();
  const pageSheet = page.getByRole('dialog', { name: '页面管理' });
  await pageSheet.waitFor({ state: 'visible' });
  const draggedPageId = await pageSheet.locator('.notebook-page-thumb').nth(2).getAttribute('data-page-id');
  await pageSheet.locator('.notebook-page-thumb').nth(2).dragTo(pageSheet.locator('.notebook-page-thumb').nth(0));
  assert.equal(await page.evaluate(() => state.notebookSession.document.pages[0].id), draggedPageId);
  const renamedPage = await page.evaluate(() => {
    const pageId = state.notebookSession.document.activePageId;
    return state.notebookSession.renamePage(pageId, '课堂推导');
  });
  assert.equal(renamedPage, true);
  assert.equal(await page.evaluate(() => state.notebookSession.page.name), '课堂推导');

  const thumbnailHasPixels = await pageSheet.locator('.notebook-page-thumb [data-notebook-page-preview]').first().evaluate(canvas => {
    const pixels = canvas.getContext('2d').getImageData(0, 0, canvas.width, canvas.height).data;
    return pixels.some((value, index) => index % 4 !== 3 && value !== 0);
  });
  assert.equal(thumbnailHasPixels, true);
  await pageSheet.getByRole('button', { name: '完成' }).click();
  await page.screenshot({ path: 'output/playwright/notebook-tablet-advanced.png', fullPage: true });

  assert.equal(await page.locator('[data-notebook-resizer]').count(), 0, 'the StarNote-style editor should not expose layout resize handles');
  const canvasWidthBeforePanel = await page.locator('.notebook-center').evaluate(element => element.getBoundingClientRect().width);
  await page.getByRole('button', { name: '解析、图层与信息' }).click();
  await page.locator('.notebook-workspace.show-right').waitFor({ state: 'visible' });
  assert.equal(await page.locator('.notebook-center').evaluate(element => element.getBoundingClientRect().width), canvasWidthBeforePanel, 'opening the inspector drawer must overlay instead of resizing the canvas');
  await page.screenshot({ path: 'output/playwright/notebook-starnote-drawer.png', fullPage: true });
  await page.getByRole('button', { name: '关闭面板' }).click();
  await page.getByRole('button', { name: '收起工具栏' }).click();
  assert.equal(await page.locator('[data-notebook-tool]').count(), 1, 'collapsed writing rail should retain only the active tool');
  await page.getByRole('button', { name: '展开工具栏' }).click();
  assert.ok(await page.locator('[data-notebook-tool]').count() >= 5, 'expanded writing rail should restore the configured tool set');

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
  await page.evaluate(() => showNotebookPaperMenu());
  const mobilePaperDialog = page.getByRole('dialog', { name: '纸张模板' });
  await mobilePaperDialog.waitFor({ state: 'visible' });
  const mobilePaperBounds = await mobilePaperDialog.boundingBox();
  assert.ok(mobilePaperBounds && mobilePaperBounds.x >= 0 && mobilePaperBounds.x + mobilePaperBounds.width <= 391, 'mobile paper sheet should fit the viewport');
  await page.screenshot({ path: 'output/playwright/notebook-paper-templates-mobile.png', fullPage: true });
  const mobilePaperScroll = await mobilePaperDialog.evaluate(dialog => {
    const list = dialog.querySelector('.notebook-paper-groups');
    const actions = dialog.querySelector('.app-dialog-actions');
    const before = actions.getBoundingClientRect().top;
    list.scrollTop = list.scrollHeight;
    const after = actions.getBoundingClientRect().top;
    return {
      dialogScrollable: dialog.scrollHeight > dialog.clientHeight + 1,
      listScrollable: list.scrollHeight > list.clientHeight + 1,
      actionShift: Math.abs(after - before),
    };
  });
  assert.equal(mobilePaperScroll.dialogScrollable, false, 'mobile paper sheet should not scroll as a whole');
  assert.equal(mobilePaperScroll.listScrollable, true, 'mobile template list should remain scrollable');
  assert.ok(mobilePaperScroll.actionShift < 1, `mobile paper actions should stay fixed while templates scroll: ${JSON.stringify(mobilePaperScroll)}`);
  await page.screenshot({ path: 'output/playwright/notebook-paper-templates-mobile-scrolled.png', fullPage: true });
  await mobilePaperDialog.getByRole('button', { name: '完成' }).click();
  await page.evaluate(() => showNotebookPenCaseMenu());
  const mobilePenCaseDialog = page.getByRole('dialog', { name: '快捷笔盒' });
  await mobilePenCaseDialog.waitFor({ state: 'visible' });
  assert.doesNotMatch(await mobilePenCaseDialog.locator('.notebook-pen-case-grid').evaluate(element => getComputedStyle(element).gridTemplateColumns), /\S+\s+\S+/, 'mobile pen case should collapse to one column');
  await page.screenshot({ path: 'output/playwright/notebook-pen-case-mobile.png', fullPage: true });
  await mobilePenCaseDialog.getByRole('button', { name: '关闭' }).click();

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
    viewport.scale = 1;
    viewport.offsetX = 480;
    viewport.offsetY = 400;
    const width = state.notebookSession.page.width;
    viewport.ensureEdgelessViewport();
    const widthAfterPan = state.notebookSession.page.width;
    viewport.ensureEdgelessBounds(width + 200, 800, width + 260, 860);
    viewport.requestRender();
    return { panExpanded: widthAfterPan > width, contentExpanded: state.notebookSession.page.width > widthAfterPan };
  });
  assert.equal(edgelessExpansion.panExpanded, false, 'panning should not grow the edgeless canvas');
  assert.equal(edgelessExpansion.contentExpanded, true, 'content near the edge should grow the edgeless canvas');
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
  assert.deepEqual(consoleErrors, []);
  assert.deepEqual(requestFailures, []);
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
    starnoteShellAndContinuousPages: true,
    multiDocumentTabs: true,
    edgelessCanvasUi: true,
    batchSnbxExport: true,
    anchoredZoomAndBoundedPan: true,
    responsiveScreenshots: true,
    consoleAndNetworkHealth: true,
    paperTemplatesAndCustomToolbar: true,
  }, null, 2));
} finally {
  await browser.close();
}

function statefulText(value) {
  return String(value || '').replace(/[.*+?^${}()|[\]\\]/g, '\\$&');
}
