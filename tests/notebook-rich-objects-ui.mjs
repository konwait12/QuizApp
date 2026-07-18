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
    localStorage.setItem('quizapp_ui_config', JSON.stringify({ autoUpdateCheck: false, autoAnnouncementCheck: false, autoBankUpdateCheck: false }));
    localStorage.setItem('quizapp_announcement_suppressed', '1');
  });
  await page.goto(appUrl, { waitUntil: 'domcontentloaded' });
  await page.waitForFunction(() => Boolean(window.marked && window.katex && window.html2canvas && window.QuizNotebookRichObjects && window.QuizNotebookExport));
  await page.evaluate(async () => {
    document.querySelectorAll('.app-dialog-mask').forEach(element => element.remove());
    await openHandwritingPractice([], { freeMode: true });
  });
  await page.getByRole('button', { name: '插入与纸张' }).click();
  await page.getByRole('button', { name: 'Markdown', exact: true }).click();
  const markdownSource = '# 牛顿第二定律\n\n- 合力\n- 加速度\n\n`F = ma`\n\n<script>window.__notebookXss = true</script>';
  await page.locator('#notebookRichSource').fill(markdownSource);
  await page.waitForFunction(() => document.querySelector('#notebookRichPreview h1')?.textContent.includes('牛顿'));
  assert.equal(await page.locator('#notebookRichPreview script').count(), 0);
  assert.equal(await page.evaluate(() => Boolean(window.__notebookXss)), false);
  await page.screenshot({ path: path.join(outputDirectory, 'notebook-markdown-editor-tablet.png'), fullPage: true });
  await page.getByRole('button', { name: '生成对象' }).click();
  await page.waitForFunction(() => state.notebookSession?.page.objects.some(object => object.type === 'markdown' && object.data?.src?.startsWith('data:image/png')));

  const markdown = await page.evaluate(() => {
    const object = state.notebookSession.page.objects.find(item => item.type === 'markdown');
    return { id: object.id, source: object.data.markdown, width: object.width, height: object.height, count: state.notebookSession.page.objects.length };
  });
  assert.match(markdown.source, /牛顿第二定律/);
  assert.ok(markdown.width >= 600 && markdown.height >= 100);

  await page.getByRole('button', { name: '编辑所选 Markdown 或公式' }).click();
  await page.locator('#notebookRichSource').fill('# 动量守恒\n\n系统合外力为零时，总动量不变。');
  await page.getByRole('button', { name: '生成对象' }).click();
  await page.waitForFunction(id => {
    const object = state.notebookSession?.page.objects.find(item => item.id === id);
    return object?.data?.markdown?.includes('动量守恒');
  }, markdown.id);
  const edited = await page.evaluate(id => ({
    count: state.notebookSession.page.objects.length,
    id: state.notebookSession.page.objects.find(item => item.id === id)?.id,
  }), markdown.id);
  assert.equal(edited.count, markdown.count);
  assert.equal(edited.id, markdown.id);

  await page.getByRole('button', { name: '插入与纸张' }).click();
  await page.getByRole('button', { name: '数学公式', exact: true }).click();
  const formulaSource = String.raw`\int_0^1 x^2\,dx = \frac{1}{3}`;
  await page.locator('#notebookRichSource').fill(formulaSource);
  await page.waitForFunction(() => Boolean(document.querySelector('#notebookRichPreview .katex')));
  await page.setViewportSize({ width: 390, height: 844 });
  await page.screenshot({ path: path.join(outputDirectory, 'notebook-formula-editor-mobile.png'), fullPage: true });
  const dialogBox = await page.locator('.notebook-rich-dialog').boundingBox();
  assert.ok(dialogBox && dialogBox.x >= 0 && dialogBox.x + dialogBox.width <= 391);
  await page.getByRole('button', { name: '生成对象' }).click();
  await page.waitForFunction(() => state.notebookSession?.page.objects.some(object => object.type === 'formula' && object.data?.src?.startsWith('data:image/png')));

  const result = await page.evaluate(async () => {
    const session = state.notebookSession;
    await saveCurrentInkNote({ silent: true, throwOnError: true });
    const formula = session.page.objects.find(object => object.type === 'formula');
    const searchable = QuizPdfNotebook.documentSearchText(session.document);
    const pdf = await QuizNotebookExport.createPdf(session.document, {
      resolveAsset: getNotebookAssetDataUrl,
      getStroke: window.PerfectFreehand?.getStroke,
    });
    const loadingTask = pdfjsLib.getDocument({ data: pdf.bytes.slice() });
    const exported = await loadingTask.promise;
    const pageOne = await exported.getPage(1);
    const viewport = pageOne.getViewport({ scale: .25 });
    const canvas = document.createElement('canvas');
    canvas.width = Math.ceil(viewport.width);
    canvas.height = Math.ceil(viewport.height);
    const context = canvas.getContext('2d', { alpha: false });
    await pageOne.render({ canvasContext: context, viewport, background: '#ffffff' }).promise;
    const pixels = context.getImageData(0, 0, canvas.width, canvas.height).data;
    let nonWhite = 0;
    for (let index = 0; index < pixels.length; index += 4) {
      if (pixels[index] < 245 || pixels[index + 1] < 245 || pixels[index + 2] < 245) nonWhite += 1;
    }
    const data = {
      documentId: session.document.id,
      formula: formula.data.latex,
      objectCount: session.page.objects.length,
      searchable,
      pdfPages: exported.numPages,
      pdfBytes: pdf.bytes.byteLength,
      nonWhite,
    };
    await loadingTask.destroy();
    return data;
  });
  assert.equal(result.objectCount, 2);
  assert.equal(result.formula, formulaSource);
  assert.match(result.searchable, /动量守恒/);
  assert.match(result.searchable, /\\int_0\^1/);
  assert.equal(result.pdfPages, 1);
  assert.ok(result.pdfBytes > 10000 && result.nonWhite > 100);
  assert.deepEqual(errors, []);

  await page.evaluate(async documentId => {
    await getNotebookAssetRepository().deleteByDocument(documentId);
    await getNotebookRepository().delete(documentId);
  }, result.documentId);
  console.log(JSON.stringify({
    safeMarkdownPreview: true,
    editableRichObject: true,
    katexFormulaObject: true,
    searchableSources: true,
    pdfExportIncludesRichObjects: true,
    tabletAndMobileScreenshots: true,
  }, null, 2));
} finally {
  await browser.close();
}
