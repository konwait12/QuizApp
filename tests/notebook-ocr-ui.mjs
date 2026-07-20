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
    localStorage.setItem('quizapp_ocr_config', JSON.stringify({ language: 'chi_sim+eng', pageSegMode: 'AUTO', modelSource: 'auto', confirmedModelDownload: true }));
  });
  await page.goto(appUrl, { waitUntil: 'domcontentloaded' });
  await page.waitForFunction(() => Boolean(window.Tesseract && window.QuizNotebookOcr && window.showNotebookOcrSourceDialog));
  const documentId = await page.evaluate(async () => {
    document.querySelectorAll('.app-dialog-mask').forEach(element => element.remove());
    await openHandwritingPractice([], { freeMode: true });
    state.notebookOcrService = {
      async recognize(source, config, options) {
        options?.onProgress?.({ percent: 25, message: '正在读取语言模型' });
        options?.onProgress?.({ percent: 88, message: '正在识别文字' });
        return { text: '牛顿第二定律\nF = ma', confidence: 93, language: config.language, pageSegMode: config.pageSegMode, blocks: [], createdAt: Date.now() };
      },
      async cancel() {},
      async terminate() {},
    };
    return state.notebookSession.document.id;
  });

  await page.getByRole('button', { name: '插入与纸张' }).click();
  await page.getByRole('button', { name: 'OCR 识别', exact: true }).click();
  await page.getByRole('heading', { name: 'OCR 识别' }).waitFor({ state: 'visible' });
  await page.screenshot({ path: path.join(outputDirectory, 'notebook-ocr-source-tablet.png'), fullPage: true });
  await page.getByRole('button', { name: /当前笔记页/ }).click();
  await page.getByRole('heading', { name: 'OCR 识别结果' }).waitFor({ state: 'visible' });
  assert.match(await page.locator('#notebookOcrResultText').inputValue(), /牛顿第二定律/);
  await page.locator('#notebookOcrResultText').fill('修正后的 OCR 文本\nF = ma');
  await page.getByRole('button', { name: '仅保存文本' }).click();
  await page.waitForFunction(() => state.notebookSession?.page.ocr?.text?.includes('修正后'));
  assert.equal(await page.evaluate(() => state.notebookSession.page.objects.length), 0);
  assert.match(await page.evaluate(() => QuizPdfNotebook.documentSearchText(state.notebookSession.document)), /修正后的 ocr 文本/);

  await page.evaluate(() => {
    state.notebookMobilePane = 'right';
    renderHandwritingPractice();
    setNotebookRightTab('info');
  });
  assert.match(await page.locator('#notebookRightPanel').innerText(), /本页 OCR/);
  assert.match(await page.locator('#notebookRightPanel').innerText(), /修正后的 OCR 文本/);

  await page.evaluate(() => showNotebookOcrSourceDialog());
  await page.locator('#notebookOcrInput').setInputFiles({
    name: 'ocr-test.png',
    mimeType: 'image/png',
    buffer: Buffer.from('iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mNk+A8AAQUBAScY42YAAAAASUVORK5CYII=', 'base64'),
  });
  await page.getByRole('heading', { name: 'OCR 识别结果' }).waitFor({ state: 'visible' });
  await page.locator('#notebookOcrResultText').fill('图片 OCR 文本');
  await page.getByRole('button', { name: '保存并插入' }).click();
  await page.waitForFunction(() => state.notebookSession?.page.objects.some(object => object.data?.ocr && object.data?.text === '图片 OCR 文本'));

  await page.evaluate(() => {
    let rejectRecognition;
    state.notebookOcrService = {
      recognize() { return new Promise((resolve, reject) => { rejectRecognition = reject; }); },
      async cancel() { rejectRecognition?.(new Error('OCR 已取消')); },
      async terminate() {},
    };
    showNotebookOcrSourceDialog();
  });
  await page.getByRole('button', { name: /当前笔记页/ }).click();
  await page.getByRole('button', { name: '取消识别' }).waitFor({ state: 'visible' });
  await page.getByRole('button', { name: '取消识别' }).click();
  await page.waitForFunction(() => state.pendingNotebookOcr === null && !document.querySelector('[aria-label="OCR 识别进度"]'));

  await page.evaluate(() => renderSettings());
  await page.getByText('笔记与 OCR 识别', { exact: true }).waitFor({ state: 'visible' });
  await page.locator('[data-choice-id="ocrLanguage"] .choice-trigger').click();
  await page.locator('[data-choice-id="ocrLanguage"] .choice-option[data-choice-value="eng"]').click();
  await page.locator('[data-choice-id="ocrModelSource"] .choice-trigger').click();
  await page.locator('[data-choice-id="ocrModelSource"] .choice-option[data-choice-value="custom"]').click();
  await page.locator('#ocrCustomLangPath').fill('https://example.com/tessdata');
  await page.getByRole('button', { name: '保存设置' }).click();
  await page.waitForFunction(() => JSON.parse(localStorage.getItem('quizapp_ocr_config')).language === 'eng');
  assert.equal(await page.evaluate(() => JSON.parse(localStorage.getItem('quizapp_ocr_config')).customLangPath), 'https://example.com/tessdata');
  await page.setViewportSize({ width: 390, height: 844 });
  await page.evaluate(() => renderSettings());
  const ocrPanel = page.getByText('笔记与 OCR 识别', { exact: true }).locator('..');
  await ocrPanel.scrollIntoViewIfNeeded();
  await page.screenshot({ path: path.join(outputDirectory, 'notebook-ocr-settings-mobile.png'), fullPage: true });

  const persistence = await page.evaluate(async id => {
    const saved = await getNotebookRepository().get(id);
    const backup = await QuizBackup.create({ appVersion: APP_VERSION, databaseName: INK_DB_NAME });
    const notebookRecords = backup.data.indexedDB.stores.notebooks.records.map(record => record.value);
    const backedUp = notebookRecords.find(document => document.id === id);
    return {
      savedText: saved.pages[0].ocr?.text,
      objectCount: saved.pages[0].objects.length,
      backedUpText: backedUp?.pages?.[0]?.ocr?.text,
      configBackedUp: Boolean(backup.data.localStorage.quizapp_ocr_config),
    };
  }, documentId);
  assert.equal(persistence.savedText, '图片 OCR 文本');
  assert.equal(persistence.objectCount, 1);
  assert.equal(persistence.backedUpText, '图片 OCR 文本');
  assert.equal(persistence.configBackedUp, true);
  assert.deepEqual(errors, []);

  await page.evaluate(async id => {
    await getNotebookAssetRepository().deleteByDocument(id);
    await getNotebookRepository().delete(id);
  }, documentId);
  console.log(JSON.stringify({
    sourceSelection: true,
    editableResult: true,
    searchablePageMetadata: true,
    optionalTextObjectInsertion: true,
    cancellation: true,
    settingsPersistence: true,
    backupIncludesOcr: true,
    responsiveScreenshots: true,
  }, null, 2));
} finally {
  await browser.close();
}
