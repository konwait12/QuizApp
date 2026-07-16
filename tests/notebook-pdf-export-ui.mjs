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
const fixture = path.resolve('tests/fixtures/notebook-pdf-test.pdf');
const outputDirectory = path.resolve('output/playwright');
const downloadedPdf = path.join(outputDirectory, 'notebook-annotated-export.pdf');
fs.mkdirSync(outputDirectory, { recursive: true });

const browser = await chromium.launch({
  headless: true,
  ...(process.env.PLAYWRIGHT_CHROMIUM_EXECUTABLE ? { executablePath: process.env.PLAYWRIGHT_CHROMIUM_EXECUTABLE } : {}),
});
const page = await browser.newPage({ viewport: { width: 1440, height: 900 } });
const pageErrors = [];
page.on('pageerror', error => pageErrors.push(error.message));

try {
  await page.addInitScript(() => {
    localStorage.setItem('quizapp_ui_config', JSON.stringify({ autoUpdateCheck: false, autoAnnouncementCheck: false }));
    localStorage.setItem('quizapp_announcement_suppressed', '1');
  });
  await page.goto(appUrl, { waitUntil: 'domcontentloaded' });
  await page.waitForFunction(() => Boolean(window.PDFLib && window.pdfjsLib && window.QuizNotebookExport && window.QuizPdfNotebook));
  await page.evaluate(async () => {
    document.querySelectorAll('.app-dialog-mask').forEach(element => element.remove());
    await openHandwritingPractice([], { freeMode: true });
  });
  await page.locator('#notebookPdfInput').setInputFiles(fixture);
  await page.getByRole('heading', { name: '导入 PDF 页面' }).waitFor({ state: 'visible' });
  await page.getByRole('button', { name: '开始导入' }).click();
  await page.waitForFunction(() => state.notebookSession?.document.pages.length === 2 && !state.pendingPdfImport, null, { timeout: 60000 });

  const exportResult = await page.evaluate(async () => {
    const session = state.notebookSession;
    const visibleLayer = session.page.layers[0];
    visibleLayer.strokes.push({
      id: 'export-visible-stroke',
      tool: 'pen',
      color: '#d00000',
      size: 34,
      pointerType: 'pen',
      points: [
        { x: 160, y: 420, pressure: .7 },
        { x: 360, y: 500, pressure: .8 },
        { x: 560, y: 420, pressure: .7 },
      ],
    });
    const hiddenLayer = QuizNotebook.createLayer('隐藏测试图层');
    hiddenLayer.visible = false;
    hiddenLayer.strokes.push({
      id: 'export-hidden-stroke',
      tool: 'pen',
      color: '#00ff00',
      size: 80,
      pointerType: 'pen',
      points: [{ x: 160, y: 760, pressure: 1 }, { x: 700, y: 760, pressure: 1 }],
    });
    session.page.layers.push(hiddenLayer);
    session.addObject('text', { text: 'Annotated export', fill: '#fff2ad', border: '#7a6420', color: '#202522', fontSize: 28 }, {
      x: 120,
      y: 860,
      width: 420,
      height: 130,
    });
    await saveCurrentInkNote({ silent: true, throwOnError: true });
    const result = await QuizNotebookExport.createPdf(session.document, {
      resolveAsset: getNotebookAssetDataUrl,
      getStroke: window.PerfectFreehand?.getStroke,
    });
    const header = Array.from(result.bytes.slice(0, 5)).map(value => String.fromCharCode(value)).join('');
    const loadingTask = pdfjsLib.getDocument({ data: result.bytes.slice() });
    const exported = await loadingTask.promise;
    const first = await exported.getPage(1);
    const viewport = first.getViewport({ scale: .45 });
    const canvas = document.createElement('canvas');
    canvas.width = Math.ceil(viewport.width);
    canvas.height = Math.ceil(viewport.height);
    const context = canvas.getContext('2d', { alpha: false });
    await first.render({ canvasContext: context, viewport, background: '#ffffff' }).promise;
    const pixels = context.getImageData(0, 0, canvas.width, canvas.height).data;
    let redPixels = 0;
    let greenPixels = 0;
    let darkPixels = 0;
    for (let index = 0; index < pixels.length; index += 4) {
      const red = pixels[index];
      const green = pixels[index + 1];
      const blue = pixels[index + 2];
      if (red > 120 && green < 105 && blue < 105) redPixels += 1;
      if (green > 180 && red < 100 && blue < 100) greenPixels += 1;
      if (red < 120 && green < 120 && blue < 120) darkPixels += 1;
    }
    const resultData = {
      header,
      byteLength: result.bytes.byteLength,
      filename: result.filename,
      pages: exported.numPages,
      redPixels,
      greenPixels,
      darkPixels,
      documentId: session.document.id,
    };
    await loadingTask.destroy();
    return resultData;
  });

  assert.equal(exportResult.header, '%PDF-');
  assert.equal(exportResult.pages, 2);
  assert.ok(exportResult.byteLength > 10000);
  assert.match(exportResult.filename, /手写批注\.pdf$/);
  assert.ok(exportResult.redPixels > 30, 'visible red handwriting should be present in the exported PDF');
  assert.ok(exportResult.greenPixels < 15, 'hidden green layer should not be exported');
  assert.ok(exportResult.darkPixels > 50, 'PDF background/text should remain visible');

  await page.getByRole('button', { name: '导出' }).click();
  await page.getByRole('heading', { name: '导出笔记' }).waitFor({ state: 'visible' });
  await page.screenshot({ path: path.join(outputDirectory, 'notebook-export-desktop.png'), fullPage: true });
  const downloadPromise = page.waitForEvent('download', { timeout: 60000 });
  await page.getByRole('button', { name: '整本 PDF' }).click();
  const download = await downloadPromise;
  await download.saveAs(downloadedPdf);
  const downloaded = fs.readFileSync(downloadedPdf);
  assert.equal(downloaded.subarray(0, 5).toString('ascii'), '%PDF-');
  assert.ok(downloaded.length > 10000);

  await page.setViewportSize({ width: 1180, height: 820 });
  await page.evaluate(() => showNotebookExportMenu());
  await page.screenshot({ path: path.join(outputDirectory, 'notebook-export-tablet.png'), fullPage: true });
  await page.setViewportSize({ width: 390, height: 844 });
  await page.evaluate(() => showNotebookExportMenu());
  const dialogBox = await page.locator('.app-dialog').boundingBox();
  assert.ok(dialogBox && dialogBox.x >= 0 && dialogBox.x + dialogBox.width <= 391);
  await page.screenshot({ path: path.join(outputDirectory, 'notebook-export-mobile.png'), fullPage: true });

  await page.evaluate(async documentId => {
    await getNotebookAssetRepository().deleteByDocument(documentId);
    await getNotebookRepository().delete(documentId);
  }, exportResult.documentId);
  assert.deepEqual(pageErrors, []);
  console.log(JSON.stringify({
    annotatedPdfPages: exportResult.pages,
    annotatedPdfBytes: exportResult.byteLength,
    hiddenLayersExcluded: true,
    browserDownload: true,
    responsiveExportDialog: true,
  }, null, 2));
} finally {
  await browser.close();
}
