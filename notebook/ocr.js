/* Optional on-device OCR powered by Tesseract.js. GPL-3.0-or-later. */
(function attachQuizNotebookOcr(global) {
  'use strict';

  const CONFIG_VERSION = 1;
  const CACHE_DATABASE = 'keyval-store';
  const CACHE_STORE = 'keyval';
  const DEFAULT_CONFIG = Object.freeze({
    version: CONFIG_VERSION,
    language: 'chi_sim+eng',
    pageSegMode: 'AUTO',
    modelSource: 'auto',
    customLangPath: '',
    confirmedModelDownload: false,
  });
  const LANGUAGES = Object.freeze([
    { value: 'chi_sim+eng', label: '简体中文 + 英文' },
    { value: 'chi_sim', label: '简体中文' },
    { value: 'eng', label: '英文' },
    { value: 'jpn', label: '日文' },
    { value: 'kor', label: '韩文' },
  ]);
  const PAGE_SEG_MODES = Object.freeze([
    { value: 'AUTO', label: '自动版面' },
    { value: 'SINGLE_BLOCK', label: '单个文本块' },
    { value: 'SPARSE_TEXT', label: '稀疏文字' },
  ]);

  function normalizeConfig(value = {}) {
    const source = value && typeof value === 'object' ? value : {};
    const language = LANGUAGES.some(item => item.value === source.language) ? source.language : DEFAULT_CONFIG.language;
    const pageSegMode = PAGE_SEG_MODES.some(item => item.value === source.pageSegMode) ? source.pageSegMode : DEFAULT_CONFIG.pageSegMode;
    const modelSource = source.modelSource === 'custom' ? 'custom' : 'auto';
    const customLangPath = /^https:\/\//i.test(String(source.customLangPath || '').trim())
      ? String(source.customLangPath || '').trim().replace(/\/$/, '')
      : '';
    return {
      version: CONFIG_VERSION,
      language,
      pageSegMode,
      modelSource,
      customLangPath,
      confirmedModelDownload: Boolean(source.confirmedModelDownload),
    };
  }

  function translateStatus(status) {
    const value = String(status || '').toLowerCase();
    if (value.includes('loading tesseract core')) return '正在加载 OCR 引擎';
    if (value.includes('initializing tesseract')) return '正在初始化 OCR 引擎';
    if (value.includes('loading language')) return '正在下载或读取语言模型';
    if (value.includes('initializing api')) return '正在初始化识别语言';
    if (value.includes('recognizing text')) return '正在识别文字';
    return status ? String(status) : '正在处理';
  }

  function progressPercent(event = {}) {
    const status = String(event.status || '').toLowerCase();
    const progress = Math.max(0, Math.min(1, Number(event.progress || 0)));
    if (status.includes('loading tesseract core')) return 4 + progress * 14;
    if (status.includes('initializing tesseract')) return 18 + progress * 8;
    if (status.includes('loading language')) return 26 + progress * 38;
    if (status.includes('initializing api')) return 64 + progress * 10;
    if (status.includes('recognizing text')) return 74 + progress * 25;
    return progress * 100;
  }

  function pageSegModeValue(config, tesseract = global.Tesseract) {
    const mode = normalizeConfig(config).pageSegMode;
    return tesseract?.PSM?.[mode] ?? ({ AUTO: 3, SINGLE_BLOCK: 6, SPARSE_TEXT: 11 }[mode] || 3);
  }

  function workerOptions(config, logger) {
    const normalized = normalizeConfig(config);
    const options = {
      workerPath: './vendor/tesseract/worker.min.js',
      corePath: './vendor/tesseract/tesseract-core-lstm.wasm.js',
      cacheMethod: 'write',
      logger,
    };
    if (normalized.modelSource === 'custom' && normalized.customLangPath) options.langPath = normalized.customLangPath;
    return options;
  }

  class OcrService {
    constructor(options = {}) {
      this.tesseract = options.tesseract || global.Tesseract;
      this.workerFactory = options.workerFactory || this.tesseract?.createWorker;
      this.worker = null;
      this.workerKey = '';
      this.generation = 0;
      this.pendingWorker = null;
    }

    async ensureWorker(config, onProgress) {
      const normalized = normalizeConfig(config);
      const key = JSON.stringify({ language: normalized.language, modelSource: normalized.modelSource, customLangPath: normalized.customLangPath });
      if (this.worker && this.workerKey === key) return this.worker;
      await this.terminate();
      if (typeof this.workerFactory !== 'function') throw new Error('Tesseract.js 未加载');
      const generation = this.generation;
      const logger = event => {
        if (generation !== this.generation || typeof onProgress !== 'function') return;
        onProgress({
          status: String(event?.status || ''),
          message: translateStatus(event?.status),
          progress: Number(event?.progress || 0),
          percent: progressPercent(event),
        });
      };
      const oem = this.tesseract?.OEM?.LSTM_ONLY ?? 1;
      this.pendingWorker = Promise.resolve(this.workerFactory(normalized.language, oem, workerOptions(normalized, logger), {}));
      let worker;
      try {
        worker = await this.pendingWorker;
      } finally {
        this.pendingWorker = null;
      }
      if (generation !== this.generation) {
        await worker?.terminate?.().catch?.(() => {});
        throw new Error('OCR 已取消');
      }
      this.worker = worker;
      this.workerKey = key;
      return worker;
    }

    async recognize(image, config = {}, options = {}) {
      if (!image) throw new Error('没有可识别的图像');
      const normalized = normalizeConfig(config);
      const worker = await this.ensureWorker(normalized, options.onProgress);
      const generation = this.generation;
      if (generation !== this.generation) throw new Error('OCR 已取消');
      await worker.setParameters?.({
        tessedit_pageseg_mode: pageSegModeValue(normalized, this.tesseract),
        preserve_interword_spaces: '1',
      });
      const result = await worker.recognize(image, {}, { text: true, blocks: true });
      if (generation !== this.generation) throw new Error('OCR 已取消');
      const text = String(result?.data?.text || '').replace(/\r/g, '').replace(/[ \t]+\n/g, '\n').trim();
      return {
        text,
        confidence: Number(result?.data?.confidence || 0),
        language: normalized.language,
        pageSegMode: normalized.pageSegMode,
        blocks: Array.isArray(result?.data?.blocks) ? result.data.blocks : [],
        createdAt: Date.now(),
      };
    }

    async terminate() {
      this.generation += 1;
      const worker = this.worker;
      this.worker = null;
      this.workerKey = '';
      if (worker?.terminate) {
        try { await worker.terminate(); } catch(e) {}
      }
    }

    cancel() {
      return this.terminate();
    }
  }

  function openCacheDatabase() {
    return new Promise((resolve, reject) => {
      if (!global.indexedDB) return resolve(null);
      const request = global.indexedDB.open(CACHE_DATABASE);
      request.onsuccess = () => resolve(request.result);
      request.onerror = () => reject(request.error || new Error('无法读取 OCR 模型缓存'));
    });
  }

  async function listCachedModels() {
    const database = await openCacheDatabase();
    if (!database) return [];
    try {
      if (!database.objectStoreNames.contains(CACHE_STORE)) return [];
      const transaction = database.transaction(CACHE_STORE, 'readonly');
      const request = transaction.objectStore(CACHE_STORE).getAllKeys();
      const keys = await new Promise((resolve, reject) => {
        request.onsuccess = () => resolve(request.result || []);
        request.onerror = () => reject(request.error || new Error('无法读取 OCR 缓存索引'));
      });
      return keys.map(key => String(key)).filter(key => key.endsWith('.traineddata'));
    } finally {
      database.close();
    }
  }

  async function clearCachedModels(service) {
    await service?.terminate?.();
    const database = await openCacheDatabase();
    if (!database) return 0;
    try {
      if (!database.objectStoreNames.contains(CACHE_STORE)) return 0;
      const transaction = database.transaction(CACHE_STORE, 'readwrite');
      const store = transaction.objectStore(CACHE_STORE);
      const request = store.getAllKeys();
      const keys = await new Promise((resolve, reject) => {
        request.onsuccess = () => resolve(request.result || []);
        request.onerror = () => reject(request.error || new Error('无法读取 OCR 缓存索引'));
      });
      const modelKeys = keys.filter(key => String(key).endsWith('.traineddata'));
      modelKeys.forEach(key => store.delete(key));
      await new Promise((resolve, reject) => {
        transaction.oncomplete = resolve;
        transaction.onerror = () => reject(transaction.error || new Error('清除 OCR 缓存失败'));
        transaction.onabort = transaction.onerror;
      });
      return modelKeys.length;
    } finally {
      database.close();
    }
  }

  global.QuizNotebookOcr = {
    CONFIG_VERSION,
    DEFAULT_CONFIG,
    LANGUAGES,
    PAGE_SEG_MODES,
    OcrService,
    normalizeConfig,
    translateStatus,
    progressPercent,
    pageSegModeValue,
    workerOptions,
    listCachedModels,
    clearCachedModels,
  };
})(window);
