import assert from 'node:assert/strict';
import path from 'node:path';
import { createRequire } from 'node:module';
import { pathToFileURL } from 'node:url';

const nodeModules = process.env.PLAYWRIGHT_NODE_MODULES;
if (!nodeModules) throw new Error('Set PLAYWRIGHT_NODE_MODULES to the bundled node_modules directory');
const require = createRequire(pathToFileURL(path.join(nodeModules, 'quizapp-playwright-resolver.cjs')));
const { chromium } = require('playwright');
const appUrl = process.env.QUIZAPP_URL || 'http://127.0.0.1:8141/';
const browser = await chromium.launch({
  headless: true,
  ...(process.env.PLAYWRIGHT_CHROMIUM_EXECUTABLE ? { executablePath: process.env.PLAYWRIGHT_CHROMIUM_EXECUTABLE } : {}),
});
const page = await browser.newPage({ viewport: { width: 1280, height: 800 } });
const pageErrors = [];
page.on('pageerror', error => pageErrors.push(error.message));

try {
  await page.addInitScript(() => {
    localStorage.setItem('quizapp_announcement_suppressed', '1');
    localStorage.setItem('quizapp_announcement_seen', 'release-v1.0.20');
    localStorage.setItem('quizapp_ui_config', JSON.stringify({ autoUpdateCheck: false, autoAnnouncementCheck: false, autoBankUpdateCheck: false }));
  });
  await page.goto(appUrl, { waitUntil: 'domcontentloaded' });
  await page.waitForFunction(() => typeof openFreeNotebookLibrary === 'function' && !state.loadingDefaults);
  await page.evaluate(async () => {
    document.querySelectorAll('.app-dialog-mask').forEach(item => item.remove());
    const repository = getNotebookRepository();
    for (const item of await repository.list()) await repository.delete(item.id);
    await getNotebookAssetRepository().deleteByDocument('unused').catch(() => {});
    state.notebookFolders = [];
    localStorage.removeItem(NOTEBOOK_FOLDERS_KEY);
    state.banks.push({
      id: 'library-ui-bank', name: '线性代数-第一章', subject: '线性代数', chapter: '第一章', path: ['线性代数', '第一章'], bundled: false,
      questions: [{ id: 'library-q1', q: '矩阵的秩', opts: ['A', 'B'], ans: 'A', sourceSubject: '线性代数', sourceChapter: '第一章', sourcePath: ['线性代数', '第一章'], sourceBankId: 'library-ui-bank', sourceIndex: 0 }],
    });
    const free = QuizNotebook.createDocument({ title: '未分类笔记', kind: 'free' });
    const subject = QuizNotebook.createDocument({ title: '线性代数自由笔记', kind: 'free', folderId: notebookSubjectFolderId('线性代数') });
    const question = QuizNotebook.createDocument({ title: '矩阵题目笔记', kind: 'question', binding: { questionKey: questionKey(state.banks.at(-1).questions[0]), sourcePath: ['线性代数', '第一章'] } });
    await repository.put(free); await repository.put(subject); await repository.put(question);
    await openFreeNotebookLibrary();
    return { freeId: free.id, subjectId: subject.id, questionId: question.id };
  });

  assert.equal(await page.evaluate(() => state.view), 'notebookLibrary');
  assert.equal(await page.locator('.notebook-library-grid').count(), 1, 'desktop should default to cover mode');
  assert.match(await page.locator('.notebook-folder-list').innerText(), /线性代数/);
  assert.equal(await page.locator('.notebook-cover-card').count(), 3, 'all notes should include free and question-bound documents');
  const questionFolder = await page.evaluate(() => {
    const item = state.notebookDocuments.find(documentItem => documentItem.kind === 'question');
    return { id: item.id, folderId: getNotebookDocumentFolderId(item), expected: notebookSubjectFolderId('线性代数') };
  });
  assert.equal(questionFolder.folderId, questionFolder.expected, 'question notes should follow their bound subject');
  await page.evaluate(id => showNotebookMoveDialog(id), questionFolder.id);
  await page.getByRole('heading', { name: '题目笔记会自动跟随所属科目' }).waitFor({ state: 'visible' });
  await page.getByRole('button', { name: '知道了' }).click();

  await page.locator('.notebook-folder-section-label button').click();
  await page.locator('.app-dialog input').fill('考研冲刺');
  await page.getByRole('button', { name: '确定' }).click();
  await page.getByRole('button', { name: /考研冲刺/ }).click();
  const customFolderId = await page.evaluate(() => state.notebookLibraryFolderId);
  assert.match(customFolderId, /^folder:/);
  await page.evaluate(id => renameNotebookFolder(id), customFolderId);
  await page.locator('.app-dialog input').fill('考研冲刺-已重命名');
  await page.getByRole('button', { name: '确定' }).click();
  await page.waitForFunction(id => state.notebookFolders.find(item => item.id === id)?.name === '考研冲刺-已重命名', customFolderId);

  const createdInFolder = await page.evaluate(async () => {
    const originalPrompt = window.prompt;
    window.prompt = () => '当前文件夹新建';
    try {
      await createFreeNotebook('paged');
      return { id: state.notebookSession.document.id, folderId: state.notebookSession.document.folderId };
    } finally {
      window.prompt = originalPrompt;
    }
  });
  assert.equal(createdInFolder.folderId, customFolderId, 'new notes should be created in the selected folder');
  await page.getByRole('button', { name: '返回' }).click();
  await page.waitForFunction(() => state.view === 'notebookLibrary');
  await page.evaluate(async id => {
    await getNotebookRepository().delete(id);
    await getNotebookAssetRepository().deleteByDocument(id).catch(() => {});
    await loadNotebookDocuments();
    renderNotebookLibraryPage();
  }, createdInFolder.id);

  const freeId = await page.evaluate(() => state.notebookDocuments.find(item => item.title === '未分类笔记').id);
  await page.evaluate(id => showNotebookMoveDialog(id), freeId);
  await page.getByRole('button', { name: /考研冲刺-已重命名/ }).last().click();
  await page.waitForFunction(({ id, folderId }) => state.notebookDocuments.find(item => item.id === id)?.folderId === folderId, { id: freeId, folderId: customFolderId });
  assert.equal(await page.evaluate(id => state.notebookDocuments.find(item => item.id === id)?.folderId, freeId), customFolderId);
  assert.equal(await page.locator('.notebook-cover-card').count(), 1);

  await page.evaluate(id => setNotebookPresetCover(id, 'blue'), freeId);
  await page.waitForFunction(id => state.notebookDocuments.find(item => item.id === id)?.cover?.preset === 'blue', freeId);
  assert.equal(await page.locator('.notebook-cover-preset.blue').count(), 1);
  await page.evaluate(id => setNotebookPageCover(id), freeId);
  await page.waitForFunction(id => state.notebookDocuments.find(item => item.id === id)?.cover?.mode === 'page', freeId);
  assert.equal(await page.locator('[data-notebook-cover-page]').count(), 1);

  const png = Buffer.from('iVBORw0KGgoAAAANSUhEUgAAAAIAAAACCAIAAAD91JpzAAAAD0lEQVR42mP8z8AARAwMAA0EAQHn1n1HAAAAAElFTkSuQmCC', 'base64');
  await page.evaluate(id => { state.pendingNotebookCoverDocumentId = id; }, freeId);
  await page.locator('#notebookCoverInput').setInputFiles({ name: 'cover.png', mimeType: 'image/png', buffer: png });
  await page.getByRole('heading', { name: '裁切封面' }).waitFor({ state: 'visible' });
  await page.locator('#notebookCoverFocusX').fill('65');
  await page.getByRole('button', { name: '使用封面' }).click();
  await page.waitForFunction(id => state.notebookDocuments.find(item => item.id === id)?.cover?.mode === 'image', freeId);
  const coverAsset = await page.evaluate(async id => {
    const item = state.notebookDocuments.find(documentItem => documentItem.id === id);
    return getNotebookAssetRepository().get(item.cover.assetId);
  }, freeId);
  assert.equal(coverAsset.role, 'cover');
  assert.equal(coverAsset.pageId, '');

  const coverSnbxRoundTrip = await page.evaluate(async id => {
    const documentItem = await getNotebookRepository().get(id);
    const assets = await getNotebookAssetRepository().listByDocument(id);
    const packed = await QuizNotebookExport.createSnbx(documentItem, assets);
    const unpacked = await QuizNotebookExport.readSnbx(packed.bytes);
    const restoredCover = unpacked.assets.find(asset => asset.role === 'cover');
    return {
      mode: unpacked.document.cover?.mode,
      coverAssetId: unpacked.document.cover?.assetId,
      restoredAssetId: restoredCover?.id,
      restoredDataUrl: restoredCover?.dataUrl || '',
    };
  }, freeId);
  assert.equal(coverSnbxRoundTrip.mode, 'image');
  assert.equal(coverSnbxRoundTrip.coverAssetId, coverSnbxRoundTrip.restoredAssetId, 'SNBX should preserve the cover asset reference');
  assert.match(coverSnbxRoundTrip.restoredDataUrl, /^data:image\/jpeg;base64,/);

  const rekeyed = await page.evaluate(async id => {
    const item = await getNotebookRepository().get(id);
    const assets = await getNotebookAssetRepository().listByDocument(id);
    const result = rekeyImportedNotebook(item, assets);
    return { coverAssetId: result.document.cover.assetId, assetId: result.assets.find(asset => asset.role === 'cover')?.id, pageId: result.assets.find(asset => asset.role === 'cover')?.pageId };
  }, freeId);
  assert.equal(rekeyed.coverAssetId, rekeyed.assetId, 'SNBX import rekey should preserve the cover reference');
  assert.equal(rekeyed.pageId, '');
  await page.evaluate(id => resetNotebookCover(id), freeId);
  await page.waitForFunction(id => state.notebookDocuments.find(item => item.id === id)?.cover?.mode === 'preset', freeId);
  assert.equal(await page.evaluate(id => getNotebookAssetRepository().get(id), coverAsset.id), null, 'restoring the default cover should clean the replaced image asset');

  await page.setViewportSize({ width: 390, height: 844 });
  await page.locator('.notebook-library-list').waitFor({ state: 'visible' });
  assert.equal(await page.locator('.notebook-library-list').count(), 1, 'mobile should default to list mode');
  assert.equal(await page.locator('.notebook-folder-list').evaluate(element => getComputedStyle(element).scrollbarWidth), 'none');
  await page.screenshot({ path: 'output/playwright/notebook-library-mobile-list.png', fullPage: true });
  const beforeMobileOpen = await page.evaluate(id => ({ last: state.notebookDocuments.find(item => item.id === id)?.lastEditorMode, mobile: matchMedia('(max-width:839px), (orientation:portrait)').matches, selected: state.notebookLibraryFolderId }), freeId);
  await page.locator('.notebook-library-row').click();
  await page.waitForFunction(id => state.notebookSession?.document?.id === id, freeId);
  const mobileOpenMode = await page.evaluate(() => ({ mode: state.notebookEditorMode, last: state.notebookSession?.document?.lastEditorMode, title: state.notebookSession?.document?.title }));
  assert.equal(mobileOpenMode.mode, 'typing', `fresh mobile free note should open in typing mode: ${JSON.stringify({ beforeMobileOpen, mobileOpenMode })}`);
  assert.equal(await page.locator('.notebook-document-tabs').isVisible(), false, 'mobile editor should keep a single top bar');
  assert.equal(await page.locator('.notebook-center > .notebook-pages').isVisible(), false, 'mobile editor should not reserve canvas space for the page strip');
  await page.getByRole('button', { name: '页面', exact: true }).click();
  await page.getByRole('dialog', { name: '页面管理' }).waitFor({ state: 'visible' });
  assert.equal(await page.locator('#notebookPagesSheet .notebook-page-thumb').count(), 1);
  await page.screenshot({ path: 'output/playwright/notebook-pages-mobile-sheet.png', fullPage: true });
  await page.getByRole('button', { name: '完成' }).click();
  await page.locator('.notebook-typing-editor').fill('手机正文\n保留换行');
  await page.waitForFunction(async ({ id, text }) => {
    const saved = await getNotebookRepository().get(id);
    return saved?.pages?.find(pageItem => pageItem.id === saved.activePageId)?.bodyText === text;
  }, { id: freeId, text: '手机正文\n保留换行' });
  const typingBounds = await page.evaluate(() => ({
    editorBottom: document.querySelector('.notebook-typing-editor').getBoundingClientRect().bottom,
    workspaceBottom: document.querySelector('.notebook-center').getBoundingClientRect().bottom,
  }));
  assert.ok(typingBounds.editorBottom <= typingBounds.workspaceBottom, `typing surface should stay within the mobile workspace: ${JSON.stringify(typingBounds)}`);
  await page.getByRole('button', { name: '手写', exact: true }).click();
  assert.equal(await page.evaluate(() => state.notebookSession.page.bodyText), '手机正文\n保留换行');
  await page.getByRole('button', { name: '工具设置' }).click();
  await page.getByRole('dialog', { name: '工具设置' }).waitFor({ state: 'visible' });
  await page.getByRole('button', { name: '手指写字' }).click();
  assert.equal(await page.evaluate(() => state.inkPenOnly), false);
  await page.getByRole('button', { name: '笔写字' }).click();
  assert.equal(await page.evaluate(() => state.inkPenOnly), true);
  await page.screenshot({ path: 'output/playwright/notebook-tools-mobile-sheet.png', fullPage: true });
  await page.getByRole('button', { name: '完成' }).click();

  const canvas = page.locator('#notebookCanvas');
  await canvas.evaluate(element => { element.setPointerCapture = () => {}; });
  const canvasBox = await canvas.boundingBox();
  const centerX = canvasBox.x + canvasBox.width / 2;
  const centerY = canvasBox.y + canvasBox.height / 2;
  const dispatchTouch = (type, pointerId, x, y) => canvas.dispatchEvent(type, {
    pointerId,
    pointerType: 'touch',
    clientX: x,
    clientY: y,
    button: 0,
    buttons: type === 'pointerup' ? 0 : 1,
    pressure: type === 'pointerup' ? 0 : .5,
    bubbles: true,
  });
  const penTouchBefore = await page.evaluate(() => ({ strokes: state.notebookSession.layer.strokes.length, x: state.notebookViewport.offsetX, y: state.notebookViewport.offsetY }));
  await dispatchTouch('pointerdown', 71, centerX, centerY);
  await dispatchTouch('pointermove', 71, centerX + 24, centerY + 18);
  await dispatchTouch('pointerup', 71, centerX + 24, centerY + 18);
  const penTouchAfter = await page.evaluate(() => ({ strokes: state.notebookSession.layer.strokes.length, x: state.notebookViewport.offsetX, y: state.notebookViewport.offsetY }));
  assert.equal(penTouchAfter.strokes, penTouchBefore.strokes, 'pen-writing mode should not draw from one-finger touch');
  assert.ok(penTouchAfter.x !== penTouchBefore.x || penTouchAfter.y !== penTouchBefore.y, 'pen-writing mode should pan with one-finger touch');

  await page.getByRole('button', { name: '工具设置' }).click();
  await page.getByRole('button', { name: '手指写字' }).click();
  await page.getByRole('button', { name: '完成' }).click();
  const fingerStrokeBefore = await page.evaluate(() => state.notebookSession.layer.strokes.length);
  await dispatchTouch('pointerdown', 72, centerX, centerY);
  await dispatchTouch('pointermove', 72, centerX + 28, centerY + 20);
  await dispatchTouch('pointerup', 72, centerX + 36, centerY + 26);
  assert.equal(await page.evaluate(() => state.notebookSession.layer.strokes.length), fingerStrokeBefore + 1, 'finger-writing mode should draw from a touch pointer');
  const pinchScaleBefore = await page.evaluate(() => state.notebookViewport.scale);
  await dispatchTouch('pointerdown', 73, centerX - 35, centerY);
  await dispatchTouch('pointerdown', 74, centerX + 35, centerY);
  await dispatchTouch('pointermove', 74, centerX + 95, centerY);
  await dispatchTouch('pointerup', 74, centerX + 95, centerY);
  await dispatchTouch('pointerup', 73, centerX - 35, centerY);
  assert.ok(await page.evaluate(scale => state.notebookViewport.scale > scale, pinchScaleBefore), 'two-finger touch should zoom the canvas in finger-writing mode');
  await page.setViewportSize({ width: 320, height: 568 });
  await page.waitForTimeout(180);
  const mobileToolbarBounds = await page.locator('.notebook-toolbar').evaluate(element => {
    const buttons = [...element.querySelectorAll('button')];
    const firstLeft = buttons[0].getBoundingClientRect().left;
    element.scrollLeft = element.scrollWidth;
    return {
      firstLeft,
      lastRight: buttons.at(-1).getBoundingClientRect().right,
      viewportWidth: innerWidth,
      scrollable: element.scrollWidth > element.clientWidth,
    };
  });
  assert.ok(mobileToolbarBounds.firstLeft >= 0, `mobile toolbar should not clip its first tool: ${JSON.stringify(mobileToolbarBounds)}`);
  assert.equal(mobileToolbarBounds.scrollable, true, 'narrow mobile toolbar should remain horizontally scrollable');
  assert.ok(mobileToolbarBounds.lastRight <= mobileToolbarBounds.viewportWidth, `mobile toolbar should keep its last tool reachable: ${JSON.stringify(mobileToolbarBounds)}`);
  await page.getByRole('button', { name: '更多操作' }).click();
  const mobileMenuBounds = await page.getByRole('dialog', { name: '笔记操作' }).boundingBox();
  assert.ok(mobileMenuBounds.y + mobileMenuBounds.height >= 567, `mobile more menu should attach to the bottom edge: ${JSON.stringify(mobileMenuBounds)}`);
  await page.screenshot({ path: 'output/playwright/notebook-more-mobile-sheet.png', fullPage: true });
  await page.getByRole('button', { name: '关闭' }).click();
  await page.screenshot({ path: 'output/playwright/notebook-library-mobile.png', fullPage: true });

  await page.getByRole('button', { name: '返回' }).click();
  await page.waitForFunction(() => state.view === 'notebookLibrary');
  await page.locator('.notebook-library-row').click();
  await page.waitForFunction(id => state.notebookSession?.document?.id === id, freeId);
  assert.equal(await page.evaluate(() => state.notebookEditorMode), 'handwriting', 'free note should remember its last editor mode');
  await page.getByRole('button', { name: '返回' }).click();
  await page.waitForFunction(() => state.view === 'notebookLibrary');
  await page.evaluate(() => selectNotebookFolder('all'));
  await page.locator('.notebook-library-row').filter({ hasText: '矩阵题目笔记' }).click();
  await page.waitForFunction(() => state.notebookSession?.document?.kind === 'question');
  assert.equal(await page.evaluate(() => state.notebookEditorMode), 'handwriting', 'question-bound notes should default to handwriting on mobile');
  await page.getByRole('button', { name: '打字', exact: true }).click();
  assert.equal(await page.evaluate(() => state.notebookEditorMode), 'typing', 'question-bound notes should allow typing mode');
  await page.getByRole('button', { name: '返回' }).click();
  await page.waitForFunction(() => state.view === 'notebookLibrary');
  await page.setViewportSize({ width: 1280, height: 800 });
  await page.locator('.notebook-library-grid').waitFor({ state: 'visible' });
  await page.evaluate(id => selectNotebookFolder(id), customFolderId);
  assert.equal(await page.evaluate(id => state.notebookFolders.some(item => item.id === id), customFolderId), true);
  await page.evaluate(id => deleteNotebookFolder(id), customFolderId);
  await page.getByRole('button', { name: '确认' }).click();
  await page.waitForFunction(id => !state.notebookFolders.some(item => item.id === id), customFolderId);
  assert.equal(await page.evaluate(id => state.notebookDocuments.find(item => item.id === id)?.folderId, freeId), '');
  assert.equal(await page.evaluate(() => state.notebookLibraryFolderId), 'unfiled');

  const deletionProbe = await page.evaluate(async () => {
    const id = 'notebook-delete-asset-probe';
    const assetId = 'notebook-delete-cover-probe';
    const documentItem = QuizNotebook.createDocument({ id, title: '删除资源测试', kind: 'free', cover: { mode: 'image', assetId } });
    await getNotebookRepository().put(documentItem);
    await getNotebookAssetRepository().putMany([{ id: assetId, documentId: id, pageId: '', role: 'cover', mimeType: 'image/png', dataUrl: 'data:image/png;base64,iVBORw0KGgo=', createdAt: 1 }]);
    await loadNotebookDocuments();
    renderNotebookLibraryPage();
    return { id, assetId };
  });
  await page.evaluate(id => deleteFreeNotebook(id), deletionProbe.id);
  await page.getByRole('button', { name: '确认' }).click();
  await page.waitForFunction(id => !state.notebookDocuments.some(item => item.id === id), deletionProbe.id);
  assert.equal(await page.evaluate(id => getNotebookAssetRepository().get(id), deletionProbe.assetId), null, 'deleting a notebook should remove its cover assets');

  assert.deepEqual(pageErrors, []);
  console.log(JSON.stringify({
    subjectFolderMapping: true,
    customFolderLifecycle: true,
    questionSubjectBinding: true,
    notebookMove: true,
    createInCurrentFolder: true,
    desktopCoverMode: true,
    mobileListMode: true,
    presetPageAndImageCovers: true,
    coverSnbxRoundTrip: true,
    coverAssetRekey: true,
    coverAssetCleanup: true,
    mobileTypingAndHandwriting: true,
    perDocumentModeMemory: true,
    questionMobileModes: true,
    inputModeSwitch: true,
    browserTouchEvents: true,
    mobileLayeredControls: true,
  }, null, 2));
} finally {
  await browser.close();
}
