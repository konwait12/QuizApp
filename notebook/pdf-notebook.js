/* PDF.js-backed notebook PDF import and asset storage. GPL-3.0-or-later. */
(function attachQuizPdfNotebook(global) {
  'use strict';

  const ASSET_STORE_NAME = 'notebook_assets';
  const DOCUMENT_INDEX_NAME = 'documentId';

  function requestValue(request) {
    return new Promise((resolve, reject) => {
      request.onsuccess = () => resolve(request.result);
      request.onerror = () => reject(request.error || new Error('笔记资源读取失败'));
    });
  }

  class NotebookAssetRepository {
    constructor(options = {}) {
      this.dbName = options.dbName || 'quizapp_study_data';
      this.dbVersion = Number(options.dbVersion || 4);
      this.storeName = options.storeName || ASSET_STORE_NAME;
      this.upgradeStores = Array.isArray(options.upgradeStores) ? options.upgradeStores : [];
    }

    open() {
      return new Promise((resolve, reject) => {
        const request = indexedDB.open(this.dbName, this.dbVersion);
        request.onupgradeneeded = () => {
          const database = request.result;
          let assetStore;
          if (!database.objectStoreNames.contains(this.storeName)) {
            assetStore = database.createObjectStore(this.storeName, { keyPath: 'id' });
          } else {
            assetStore = request.transaction.objectStore(this.storeName);
          }
          if (!assetStore.indexNames.contains(DOCUMENT_INDEX_NAME)) {
            assetStore.createIndex(DOCUMENT_INDEX_NAME, 'documentId', { unique: false });
          }
          this.upgradeStores.forEach(store => {
            if (!database.objectStoreNames.contains(store.name)) database.createObjectStore(store.name, store.options || { keyPath: 'id' });
          });
        };
        request.onsuccess = () => resolve(request.result);
        request.onerror = () => reject(request.error || new Error('无法打开笔记资源库'));
      });
    }

    async transact(mode, callback) {
      const database = await this.open();
      return new Promise((resolve, reject) => {
        const transaction = database.transaction(this.storeName, mode);
        let result;
        try {
          result = callback(transaction.objectStore(this.storeName));
        } catch(error) {
          database.close();
          reject(error);
          return;
        }
        transaction.oncomplete = () => {
          database.close();
          resolve(result);
        };
        transaction.onerror = () => {
          database.close();
          reject(transaction.error || new Error('笔记资源事务失败'));
        };
        transaction.onabort = transaction.onerror;
      });
    }

    async get(id) {
      const database = await this.open();
      try {
        const transaction = database.transaction(this.storeName, 'readonly');
        const value = await requestValue(transaction.objectStore(this.storeName).get(id));
        return value || null;
      } finally {
        database.close();
      }
    }

    putMany(records = []) {
      return this.transact('readwrite', store => {
        records.forEach(record => store.put(record));
        return records.length;
      });
    }

    deleteMany(ids = []) {
      return this.transact('readwrite', store => {
        ids.forEach(id => store.delete(id));
        return ids.length;
      });
    }

    async listByDocument(documentId) {
      const database = await this.open();
      try {
        const transaction = database.transaction(this.storeName, 'readonly');
        const store = transaction.objectStore(this.storeName);
        if (store.indexNames.contains(DOCUMENT_INDEX_NAME)) {
          return await requestValue(store.index(DOCUMENT_INDEX_NAME).getAll(documentId));
        }
        const records = await requestValue(store.getAll());
        return (records || []).filter(record => record.documentId === documentId);
      } finally {
        database.close();
      }
    }

    async deleteByDocument(documentId) {
      const records = await this.listByDocument(documentId);
      return this.deleteMany(records.map(record => record.id));
    }
  }

  function uid(prefix) {
    if (global.crypto?.randomUUID) return `${prefix}:${global.crypto.randomUUID()}`;
    return `${prefix}:${Date.now().toString(36)}:${Math.random().toString(36).slice(2)}`;
  }

  function parsePageRange(value, totalPages) {
    const total = Math.max(1, Number(totalPages || 1));
    const text = String(value || '').trim().replace(/，/g, ',').replace(/－|—/g, '-');
    if (!text || text === '全部' || text === '*') return Array.from({ length: total }, (_, index) => index + 1);
    const pages = new Set();
    for (const part of text.split(',').map(item => item.trim()).filter(Boolean)) {
      const range = part.match(/^(\d+)\s*-\s*(\d+)$/);
      if (range) {
        const start = Math.max(1, Math.min(total, Number(range[1])));
        const end = Math.max(1, Math.min(total, Number(range[2])));
        const direction = start <= end ? 1 : -1;
        for (let page = start; page !== end + direction; page += direction) pages.add(page);
        continue;
      }
      if (/^\d+$/.test(part)) {
        const page = Number(part);
        if (page >= 1 && page <= total) pages.add(page);
        continue;
      }
      throw new Error(`无法识别页码：${part}`);
    }
    if (!pages.size) throw new Error('没有选择有效 PDF 页码');
    return Array.from(pages);
  }

  async function loadPdf(file) {
    if (!global.pdfjsLib?.getDocument) throw new Error('PDF.js 未加载');
    if (!file) throw new Error('没有选择 PDF 文件');
    const data = new Uint8Array(await file.arrayBuffer());
    const loadingTask = global.pdfjsLib.getDocument({ data });
    const document = await loadingTask.promise;
    const outline = await extractPdfOutline(document);
    return {
      document,
      loadingTask,
      fileName: String(file.name || '未命名 PDF'),
      fileSize: Number(file.size || data.byteLength),
      totalPages: document.numPages,
      outline,
    };
  }

  async function resolveDestinationPage(pdf, destination) {
    try {
      const resolved = typeof destination === 'string' ? await pdf.getDestination(destination) : destination;
      const reference = Array.isArray(resolved) ? resolved[0] : null;
      if (Number.isInteger(reference)) return reference + 1;
      if (reference) return (await pdf.getPageIndex(reference)) + 1;
    } catch(e) {}
    return 0;
  }

  async function extractPdfOutline(pdf) {
    try {
      const root = await pdf.getOutline();
      const entries = [];
      const visit = async (items, depth) => {
        for (const item of items || []) {
          const pageNumber = await resolveDestinationPage(pdf, item.dest);
          if (pageNumber) entries.push({ title: String(item.title || `第 ${pageNumber} 页`), pageNumber, depth });
          await visit(item.items, depth + 1);
        }
      };
      await visit(root, 0);
      return entries;
    } catch(e) {
      return [];
    }
  }

  async function extractPageLinks(pdf, page, viewport, notebookWidth, notebookHeight) {
    try {
      const annotations = await page.getAnnotations({ intent: 'display' });
      const links = [];
      for (const annotation of annotations || []) {
        if (annotation.subtype !== 'Link' || !Array.isArray(annotation.rect)) continue;
        const converted = viewport.convertToViewportRectangle(annotation.rect);
        const x1 = Math.min(converted[0], converted[2]);
        const y1 = Math.min(converted[1], converted[3]);
        const x2 = Math.max(converted[0], converted[2]);
        const y2 = Math.max(converted[1], converted[3]);
        const targetPage = await resolveDestinationPage(pdf, annotation.dest);
        links.push({
          rect: {
            x: x1 / viewport.width * notebookWidth,
            y: y1 / viewport.height * notebookHeight,
            width: (x2 - x1) / viewport.width * notebookWidth,
            height: (y2 - y1) / viewport.height * notebookHeight,
          },
          targetPage,
          url: String(annotation.url || annotation.unsafeUrl || ''),
          label: String(annotation.title || annotation.contents || annotation.url || (targetPage ? `跳转到第 ${targetPage} 页` : 'PDF 链接')),
        });
      }
      return links.filter(link => link.targetPage || link.url);
    } catch(e) {
      return [];
    }
  }

  async function extractPageText(page) {
    try {
      const content = await page.getTextContent();
      return (content.items || []).map(item => String(item.str || '') + (item.hasEOL ? '\n' : ' ')).join('').replace(/[ \t]+\n/g, '\n').replace(/[ \t]{2,}/g, ' ').trim();
    } catch(e) {
      return '';
    }
  }

  async function renderPdfPage(pdf, pageNumber, options = {}) {
    const page = await pdf.getPage(pageNumber);
    const base = page.getViewport({ scale: 1 });
    const maxWidth = Math.max(900, Number(options.renderWidth || 1600));
    const maxPixels = Math.max(1000000, Number(options.maxPixels || 4200000));
    const widthScale = maxWidth / Math.max(1, base.width);
    const pixelScale = Math.sqrt(maxPixels / Math.max(1, base.width * base.height));
    const scale = Math.max(.5, Math.min(2.5, widthScale, pixelScale));
    const viewport = page.getViewport({ scale });
    const canvas = document.createElement('canvas');
    canvas.width = Math.max(1, Math.round(viewport.width));
    canvas.height = Math.max(1, Math.round(viewport.height));
    const renderedWidth = canvas.width;
    const renderedHeight = canvas.height;
    const context = canvas.getContext('2d', { alpha: false });
    context.fillStyle = '#ffffff';
    context.fillRect(0, 0, canvas.width, canvas.height);
    await page.render({ canvasContext: context, viewport, background: '#ffffff' }).promise;
    const searchText = await extractPageText(page);
    const notebookWidth = 1200;
    const notebookHeight = Math.max(200, Math.round(notebookWidth * base.height / Math.max(1, base.width)));
    const links = await extractPageLinks(pdf, page, viewport, notebookWidth, notebookHeight);
    const dataUrl = canvas.toDataURL('image/jpeg', Number(options.quality || .9));
    canvas.width = 1;
    canvas.height = 1;
    page.cleanup();
    const assetId = uid('pdf-page');
    const notebookPage = global.QuizNotebook.createPage({
      name: `${options.sourceName || 'PDF'} · 第 ${pageNumber} 页`,
      width: notebookWidth,
      height: notebookHeight,
      backgroundType: 'pdf',
      backgroundAssetId: assetId,
      sourceName: options.sourceName || '',
      sourcePage: pageNumber,
      searchText,
      backgroundLinks: links,
    });
    return {
      page: notebookPage,
      asset: {
        id: assetId,
        documentId: String(options.documentId || ''),
        pageId: notebookPage.id,
        type: 'pdf-page',
        mimeType: 'image/jpeg',
        dataUrl,
        sourceName: String(options.sourceName || ''),
        sourcePage: pageNumber,
        width: renderedWidth,
        height: renderedHeight,
        createdAt: Date.now(),
      },
    };
  }

  async function renderPdfPages(pdf, pageNumbers, options = {}) {
    const pages = [];
    const assets = [];
    for (let index = 0; index < pageNumbers.length; index++) {
      const pageNumber = pageNumbers[index];
      const result = await renderPdfPage(pdf, pageNumber, options);
      pages.push(result.page);
      assets.push(result.asset);
      if (typeof options.onProgress === 'function') {
        options.onProgress({
          pageNumber,
          completed: index + 1,
          total: pageNumbers.length,
          percent: Math.round((index + 1) / pageNumbers.length * 100),
        });
      }
      await new Promise(resolve => global.setTimeout(resolve, 0));
    }
    return { pages, assets };
  }

  function documentSearchText(document) {
    const chunks = [document?.title || '', ...(document?.tags || [])];
    if (document?.binding?.questionKey) chunks.push(document.binding.questionKey);
    (document?.links || []).forEach(link => chunks.push(link.label || '', link.target || ''));
    (document?.bookmarks || []).forEach(bookmark => chunks.push(bookmark.label || '', bookmark.note || ''));
    (document?.pages || []).forEach(page => {
      chunks.push(page.name || '', page.background?.sourceName || '', page.background?.searchText || '', page.ocr?.text || '');
      (page.objects || []).forEach(object => chunks.push(
        object.data?.text || '',
        object.data?.label || '',
        object.data?.url || '',
        object.data?.markdown || '',
        object.data?.latex || '',
      ));
    });
    return chunks.join('\n').toLowerCase();
  }

  global.QuizPdfNotebook = {
    ASSET_STORE_NAME,
    DOCUMENT_INDEX_NAME,
    NotebookAssetRepository,
    parsePageRange,
    loadPdf,
    renderPdfPage,
    renderPdfPages,
    extractPdfOutline,
    extractPageLinks,
    documentSearchText,
  };
})(window);
