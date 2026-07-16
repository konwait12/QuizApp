import assert from 'node:assert/strict';
import path from 'node:path';
import { pathToFileURL } from 'node:url';

globalThis.window = globalThis;
const moduleUrl = pathToFileURL(path.resolve('notebook/ocr.js'));
await import(`${moduleUrl.href}?test=${Date.now()}`);

const Ocr = globalThis.QuizNotebookOcr;
assert.ok(Ocr);
assert.equal(Ocr.normalizeConfig({ language: 'eng', pageSegMode: 'SINGLE_BLOCK' }).language, 'eng');
assert.equal(Ocr.normalizeConfig({ modelSource: 'custom', customLangPath: 'http://unsafe.example' }).customLangPath, '');
assert.equal(Ocr.normalizeConfig({ language: 'unknown' }).language, 'chi_sim+eng');

const progress = [];
const calls = [];
let terminated = 0;
const mockTesseract = {
  OEM: { LSTM_ONLY: 1 },
  PSM: { AUTO: 3, SINGLE_BLOCK: 6, SPARSE_TEXT: 11 },
};
const service = new Ocr.OcrService({
  tesseract: mockTesseract,
  workerFactory: async (language, oem, options) => {
    calls.push({ language, oem, options });
    options.logger({ status: 'loading tesseract core', progress: 1 });
    options.logger({ status: 'loading language traineddata', progress: 1 });
    return {
      async setParameters(parameters) { calls.at(-1).parameters = parameters; },
      async recognize() {
        options.logger({ status: 'recognizing text', progress: 1 });
        return { data: { text: '  OCR text \n', confidence: 91, blocks: [{ text: 'OCR text' }] } };
      },
      async terminate() { terminated += 1; },
    };
  },
});

const first = await service.recognize('image-one', {
  language: 'eng',
  pageSegMode: 'SINGLE_BLOCK',
}, { onProgress: event => progress.push(event) });
assert.equal(first.text, 'OCR text');
assert.equal(first.confidence, 91);
assert.equal(calls.length, 1);
assert.equal(calls[0].options.workerPath, './vendor/tesseract/worker.min.js');
assert.equal(calls[0].options.corePath, './vendor/tesseract/tesseract-core-lstm.wasm.js');
assert.equal(calls[0].parameters.tessedit_pageseg_mode, 6);
assert.ok(progress.some(event => event.message.includes('语言模型')));
assert.ok(progress.every(event => event.percent >= 0 && event.percent <= 100));

await service.recognize('image-two', { language: 'eng', pageSegMode: 'AUTO' });
assert.equal(calls.length, 1, 'same language and source should reuse the worker');
await service.recognize('image-three', { language: 'chi_sim', pageSegMode: 'AUTO' });
assert.equal(calls.length, 2, 'language change should create a new worker');
assert.equal(terminated, 1, 'language change should terminate the previous worker');

let resolveRecognition;
const cancellable = new Ocr.OcrService({
  tesseract: mockTesseract,
  workerFactory: async () => ({
    async setParameters() {},
    recognize() { return new Promise(resolve => { resolveRecognition = resolve; }); },
    async terminate() { terminated += 1; },
  }),
});
const pending = cancellable.recognize('slow-image', { language: 'eng' });
await new Promise(resolve => setTimeout(resolve, 0));
await cancellable.cancel();
resolveRecognition({ data: { text: 'late result', confidence: 80 } });
await assert.rejects(pending, /OCR 已取消/);

assert.deepEqual(await Ocr.listCachedModels(), []);
console.log(JSON.stringify({
  configNormalization: true,
  localWorkerAndCorePaths: true,
  modelSourceIsOnDemand: true,
  workerReuseAndLanguageSwitch: true,
  progressMapping: true,
  cancellation: true,
}, null, 2));
