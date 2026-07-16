/*
 * QuizApp Notebook Engine
 *
 * Copyright (C) 2026 QuizApp contributors
 * Derived from SpeedyNote's document architecture:
 * https://github.com/alpha-liu-01/SpeedyNote
 * Source revision: 6b2bbc15127b0a8a8c046e7f4168a598c976ee3b
 *
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation, either version 3 of the License, or any later version.
 *
 * This is a JavaScript port for QuizApp, not an official SpeedyNote build.
 * The port follows SpeedyNote's Document -> Page -> VectorLayer ->
 * VectorStroke/InsertedObject model and viewport coordinate system.
 */
(function attachQuizNotebook(global) {
  'use strict';

  const SCHEMA_VERSION = 4;
  const DEFAULT_PAGE_SIZE = { width: 1200, height: 1600 };
  const thumbnailAssetCache = new Map();

  function uid(prefix = 'id') {
    if (global.crypto?.randomUUID) return `${prefix}:${global.crypto.randomUUID()}`;
    return `${prefix}:${Date.now().toString(36)}:${Math.random().toString(36).slice(2)}`;
  }

  function clamp(value, min, max) {
    return Math.max(min, Math.min(max, Number(value)));
  }

  function clone(value) {
    return global.structuredClone
      ? global.structuredClone(value)
      : JSON.parse(JSON.stringify(value));
  }

  function createLayer(name = '图层 1') {
    return {
      id: uid('layer'),
      name,
      visible: true,
      locked: false,
      opacity: 1,
      strokes: [],
    };
  }

  function createPage(options = {}) {
    const layer = createLayer();
    return {
      id: uid('page'),
      name: options.name || '第 1 页',
      width: Number(options.width || DEFAULT_PAGE_SIZE.width),
      height: Number(options.height || DEFAULT_PAGE_SIZE.height),
      background: {
        type: options.backgroundType || 'grid',
        color: options.backgroundColor || '#ffffff',
        patternColor: options.patternColor || '#dfe6e2',
        spacing: Number(options.spacing || 40),
        assetId: String(options.backgroundAssetId || ''),
        sourceName: String(options.sourceName || ''),
        sourcePage: Number(options.sourcePage || 0),
        searchText: String(options.searchText || ''),
      },
      ocr: options.ocr && typeof options.ocr === 'object' ? clone(options.ocr) : null,
      layers: [layer],
      activeLayerId: layer.id,
      objects: [],
      createdAt: Date.now(),
      updatedAt: Date.now(),
    };
  }

  function createDocument(options = {}) {
    const firstPage = createPage({ name: '第 1 页', ...options.page });
    const now = Date.now();
    return {
      schemaVersion: SCHEMA_VERSION,
      id: options.id || uid('notebook'),
      title: options.title || '未命名笔记',
      kind: options.kind === 'question' ? 'question' : 'free',
      binding: options.binding || null,
      tags: Array.isArray(options.tags) ? options.tags.map(item => String(item || '').trim()).filter(Boolean) : [],
      links: Array.isArray(options.links) ? clone(options.links) : [],
      bookmarks: Array.isArray(options.bookmarks) ? clone(options.bookmarks) : [],
      mode: options.mode === 'edgeless' ? 'edgeless' : 'paged',
      pages: [firstPage],
      activePageId: firstPage.id,
      lastViewport: null,
      completed: false,
      createdAt: now,
      updatedAt: now,
    };
  }

  function normalizeLayer(raw, index) {
    return {
      id: raw?.id || uid('layer'),
      name: raw?.name || `图层 ${index + 1}`,
      visible: raw?.visible !== false,
      locked: Boolean(raw?.locked),
      opacity: clamp(raw?.opacity ?? 1, 0, 1),
      strokes: Array.isArray(raw?.strokes) ? raw.strokes.map(normalizeStroke) : [],
    };
  }

  function normalizeStroke(raw) {
    const points = Array.isArray(raw?.points) ? raw.points.map(point => {
      if (Array.isArray(point)) {
        return { x: Number(point[0] || 0), y: Number(point[1] || 0), pressure: Number(point[2] ?? .5), time: Number(point[3] || 0) };
      }
      return {
        x: Number(point?.x || 0),
        y: Number(point?.y || 0),
        pressure: Number(point?.pressure ?? point?.p ?? .5),
        time: Number(point?.time ?? point?.t ?? 0),
      };
    }) : [];
    return {
      id: raw?.id || uid('stroke'),
      tool: raw?.tool === 'highlighter' ? 'highlighter' : 'pen',
      color: raw?.color || '#202522',
      size: Number(raw?.size || raw?.baseThickness || 5),
      pointerType: raw?.pointerType || 'pen',
      points,
      bounds: raw?.bounds || computeBounds(points, Number(raw?.size || 5)),
    };
  }

  function normalizeObject(raw, index) {
    return {
      id: raw?.id || uid('object'),
      type: ['image', 'text', 'link', 'question', 'markdown', 'formula'].includes(raw?.type) ? raw.type : 'text',
      x: Number(raw?.x ?? raw?.position?.x ?? 80),
      y: Number(raw?.y ?? raw?.position?.y ?? 80),
      width: Math.max(40, Number(raw?.width ?? raw?.size?.width ?? 360)),
      height: Math.max(30, Number(raw?.height ?? raw?.size?.height ?? 180)),
      rotation: Number(raw?.rotation || 0),
      zOrder: Number(raw?.zOrder ?? index),
      layerAffinity: Number(raw?.layerAffinity ?? -1),
      visible: raw?.visible !== false,
      locked: Boolean(raw?.locked),
      data: raw?.data && typeof raw.data === 'object' ? raw.data : {},
    };
  }

  function normalizePage(raw, index) {
    const layers = Array.isArray(raw?.layers) && raw.layers.length
      ? raw.layers.map(normalizeLayer)
      : [createLayer()];
    return {
      id: raw?.id || raw?.uuid || uid('page'),
      name: raw?.name || `第 ${index + 1} 页`,
      width: Number(raw?.width || raw?.size?.width || DEFAULT_PAGE_SIZE.width),
      height: Number(raw?.height || raw?.size?.height || DEFAULT_PAGE_SIZE.height),
      background: {
        type: ['plain', 'grid', 'lines', 'question', 'pdf'].includes(raw?.background?.type) ? raw.background.type : 'grid',
        color: raw?.background?.color || '#ffffff',
        patternColor: raw?.background?.patternColor || '#dfe6e2',
        spacing: Number(raw?.background?.spacing || 40),
        assetId: String(raw?.background?.assetId || ''),
        sourceName: String(raw?.background?.sourceName || ''),
        sourcePage: Number(raw?.background?.sourcePage || 0),
        searchText: String(raw?.background?.searchText || ''),
      },
      ocr: raw?.ocr && typeof raw.ocr === 'object' ? {
        text: String(raw.ocr.text || ''),
        language: String(raw.ocr.language || ''),
        confidence: Number(raw.ocr.confidence || 0),
        source: String(raw.ocr.source || ''),
        createdAt: Number(raw.ocr.createdAt || Date.now()),
      } : null,
      layers,
      activeLayerId: layers.some(layer => layer.id === raw?.activeLayerId) ? raw.activeLayerId : layers[0].id,
      objects: Array.isArray(raw?.objects) ? raw.objects.map(normalizeObject) : [],
      createdAt: Number(raw?.createdAt || Date.now()),
      updatedAt: Number(raw?.updatedAt || Date.now()),
    };
  }

  function normalizeDocument(raw) {
    if (!raw || typeof raw !== 'object') return createDocument();
    const pages = Array.isArray(raw.pages) && raw.pages.length
      ? raw.pages.map(normalizePage)
      : [createPage()];
    return {
      schemaVersion: SCHEMA_VERSION,
      id: raw.id || uid('notebook'),
      title: raw.title || raw.name || '未命名笔记',
      kind: raw.kind === 'question' ? 'question' : 'free',
      binding: raw.binding || null,
      tags: Array.isArray(raw.tags) ? raw.tags.map(item => String(item || '').trim()).filter(Boolean) : [],
      links: Array.isArray(raw.links) ? raw.links.filter(item => item && typeof item === 'object').map(item => ({ ...item })) : [],
      bookmarks: Array.isArray(raw.bookmarks) ? raw.bookmarks.filter(item => item && pages.some(page => page.id === item.pageId)).map(item => ({
        id: item.id || uid('bookmark'),
        pageId: String(item.pageId || ''),
        label: String(item.label || '书签'),
        note: String(item.note || ''),
        createdAt: Number(item.createdAt || Date.now()),
      })) : [],
      mode: raw.mode === 'edgeless' ? 'edgeless' : 'paged',
      pages,
      activePageId: pages.some(page => page.id === raw.activePageId) ? raw.activePageId : pages[0].id,
      lastViewport: raw.lastViewport || null,
      completed: Boolean(raw.completed),
      createdAt: Number(raw.createdAt || Date.now()),
      updatedAt: Number(raw.updatedAt || Date.now()),
    };
  }

  function migrateLegacyInk(note, options = {}) {
    const document = createDocument({
      id: options.id,
      title: options.title || '题目笔记',
      kind: 'question',
      binding: options.binding || null,
    });
    document.completed = Boolean(note?.completed);
    const page = document.pages[0];
    const layer = page.layers[0];
    layer.strokes = (note?.strokes || []).map(raw => {
      const normalized = normalizeStroke(raw);
      normalized.points = normalized.points.map(point => ({
        ...point,
        x: point.x >= 0 && point.x <= 1 ? point.x * page.width : point.x,
        y: point.y >= 0 && point.y <= 1 ? point.y * page.height : point.y,
      }));
      normalized.bounds = computeBounds(normalized.points, normalized.size);
      return normalized;
    });
    document.updatedAt = Number(note?.updatedAt || Date.now());
    return document;
  }

  function computeBounds(points, padding = 0) {
    if (!points?.length) return { x: 0, y: 0, width: 0, height: 0 };
    let minX = points[0].x;
    let maxX = minX;
    let minY = points[0].y;
    let maxY = minY;
    points.forEach(point => {
      minX = Math.min(minX, point.x);
      maxX = Math.max(maxX, point.x);
      minY = Math.min(minY, point.y);
      maxY = Math.max(maxY, point.y);
    });
    const inset = Math.max(2, Number(padding || 0));
    return { x: minX - inset, y: minY - inset, width: maxX - minX + inset * 2, height: maxY - minY + inset * 2 };
  }

  function pointToSegmentDistance(point, start, end) {
    const dx = end.x - start.x;
    const dy = end.y - start.y;
    const lengthSq = dx * dx + dy * dy;
    if (lengthSq < .0001) return Math.hypot(point.x - start.x, point.y - start.y);
    const t = clamp(((point.x - start.x) * dx + (point.y - start.y) * dy) / lengthSq, 0, 1);
    return Math.hypot(point.x - (start.x + t * dx), point.y - (start.y + t * dy));
  }

  function strokeContainsPoint(stroke, point, tolerance = 12) {
    const bounds = stroke.bounds || computeBounds(stroke.points, stroke.size);
    if (point.x < bounds.x - tolerance || point.x > bounds.x + bounds.width + tolerance ||
        point.y < bounds.y - tolerance || point.y > bounds.y + bounds.height + tolerance) return false;
    if (stroke.points.length === 1) return Math.hypot(point.x - stroke.points[0].x, point.y - stroke.points[0].y) <= tolerance;
    for (let index = 1; index < stroke.points.length; index += 1) {
      if (pointToSegmentDistance(point, stroke.points[index - 1], stroke.points[index]) <= tolerance + stroke.size / 2) return true;
    }
    return false;
  }

  function rectFromPoints(start, end) {
    const x = Math.min(start.x, end.x);
    const y = Math.min(start.y, end.y);
    return { x, y, width: Math.abs(end.x - start.x), height: Math.abs(end.y - start.y) };
  }

  function unionBounds(items) {
    const bounds = items.filter(Boolean);
    if (!bounds.length) return null;
    const minX = Math.min(...bounds.map(item => item.x));
    const minY = Math.min(...bounds.map(item => item.y));
    const maxX = Math.max(...bounds.map(item => item.x + item.width));
    const maxY = Math.max(...bounds.map(item => item.y + item.height));
    return { x: minX, y: minY, width: maxX - minX, height: maxY - minY };
  }

  function rotatePoint(point, center, angleDegrees) {
    const angle = Number(angleDegrees || 0) * Math.PI / 180;
    const cos = Math.cos(angle);
    const sin = Math.sin(angle);
    const dx = point.x - center.x;
    const dy = point.y - center.y;
    return { x: center.x + dx * cos - dy * sin, y: center.y + dx * sin + dy * cos };
  }

  function objectBounds(object) {
    const center = { x: object.x + object.width / 2, y: object.y + object.height / 2 };
    const corners = [
      { x: object.x, y: object.y },
      { x: object.x + object.width, y: object.y },
      { x: object.x + object.width, y: object.y + object.height },
      { x: object.x, y: object.y + object.height },
    ].map(point => rotatePoint(point, center, object.rotation));
    return unionBounds(corners.map(point => ({ x: point.x, y: point.y, width: 0, height: 0 })));
  }

  function pointInObject(point, object) {
    const center = { x: object.x + object.width / 2, y: object.y + object.height / 2 };
    const local = rotatePoint(point, center, -Number(object.rotation || 0));
    return local.x >= object.x && local.x <= object.x + object.width &&
      local.y >= object.y && local.y <= object.y + object.height;
  }

  function boundsIntersect(a, b) {
    return a.x <= b.x + b.width && a.x + a.width >= b.x &&
      a.y <= b.y + b.height && a.y + a.height >= b.y;
  }

  class NotebookRepository {
    constructor(options = {}) {
      this.dbName = options.dbName || 'quizapp_study_data';
      this.dbVersion = Number(options.dbVersion || 3);
      this.storeName = options.storeName || 'notebooks';
      this.upgradeStores = Array.isArray(options.upgradeStores) ? options.upgradeStores : [];
    }

    open() {
      return new Promise((resolve, reject) => {
        const request = indexedDB.open(this.dbName, this.dbVersion);
        request.onupgradeneeded = () => {
          const database = request.result;
          if (!database.objectStoreNames.contains(this.storeName)) database.createObjectStore(this.storeName, { keyPath: 'id' });
          this.upgradeStores.forEach(store => {
            const objectStore = database.objectStoreNames.contains(store.name)
              ? request.transaction.objectStore(store.name)
              : database.createObjectStore(store.name, store.options || { keyPath: 'id' });
            (store.indexes || []).forEach(index => {
              if (!objectStore.indexNames.contains(index.name)) objectStore.createIndex(index.name, index.keyPath, index.options || {});
            });
          });
        };
        request.onsuccess = () => resolve(request.result);
        request.onerror = () => reject(request.error || new Error('无法打开笔记数据库'));
      });
    }

    async transact(mode, callback) {
      const database = await this.open();
      return new Promise((resolve, reject) => {
        const transaction = database.transaction(this.storeName, mode);
        let result;
        try {
          result = callback(transaction.objectStore(this.storeName));
        } catch (error) {
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
          reject(transaction.error || new Error('笔记数据库操作失败'));
        };
      });
    }

    async get(id) {
      const database = await this.open();
      return new Promise((resolve, reject) => {
        const transaction = database.transaction(this.storeName, 'readonly');
        const request = transaction.objectStore(this.storeName).get(id);
        request.onsuccess = () => resolve(request.result ? normalizeDocument(request.result) : null);
        request.onerror = () => reject(request.error);
        transaction.oncomplete = () => database.close();
      });
    }

    async list() {
      const database = await this.open();
      return new Promise((resolve, reject) => {
        const transaction = database.transaction(this.storeName, 'readonly');
        const request = transaction.objectStore(this.storeName).getAll();
        request.onsuccess = () => resolve((request.result || []).map(normalizeDocument).sort((a, b) => b.updatedAt - a.updatedAt));
        request.onerror = () => reject(request.error);
        transaction.oncomplete = () => database.close();
      });
    }

    async put(document) {
      const normalized = normalizeDocument(document);
      normalized.updatedAt = Date.now();
      await this.transact('readwrite', store => store.put(normalized));
      return normalized;
    }

    async delete(id) {
      return this.transact('readwrite', store => store.delete(id));
    }
  }

  class NotebookSession extends EventTarget {
    constructor(document) {
      super();
      this.document = normalizeDocument(document);
      this.undoStack = [];
      this.redoStack = [];
      this.selected = null;
      this.clipboard = [];
      this.historyStart = null;
    }

    get page() {
      return this.document.pages.find(page => page.id === this.document.activePageId) || this.document.pages[0];
    }

    get layer() {
      return this.page.layers.find(layer => layer.id === this.page.activeLayerId) || this.page.layers[0];
    }

    emit(type = 'change', detail = {}) {
      this.document.updatedAt = Date.now();
      this.page.updatedAt = Date.now();
      this.dispatchEvent(new CustomEvent(type, { detail }));
    }

    beginHistory() {
      if (!this.historyStart) this.historyStart = clone(this.document);
    }

    commitHistory(label = 'change') {
      if (!this.historyStart) return;
      this.undoStack.push({ label, document: this.historyStart });
      if (this.undoStack.length > 80) this.undoStack.shift();
      this.historyStart = null;
      this.redoStack = [];
      this.emit('change', { label });
    }

    undo() {
      const entry = this.undoStack.pop();
      if (!entry) return false;
      this.redoStack.push({ label: entry.label, document: clone(this.document) });
      this.document = normalizeDocument(entry.document);
      this.selected = null;
      this.emit('documentchange', { action: 'undo' });
      return true;
    }

    redo() {
      const entry = this.redoStack.pop();
      if (!entry) return false;
      this.undoStack.push({ label: entry.label, document: clone(this.document) });
      this.document = normalizeDocument(entry.document);
      this.selected = null;
      this.emit('documentchange', { action: 'redo' });
      return true;
    }

    setPage(id) {
      if (!this.document.pages.some(page => page.id === id)) return false;
      this.document.activePageId = id;
      this.selected = null;
      this.emit('pagechange', { id });
      return true;
    }

    addPage(options = {}) {
      this.beginHistory();
      const page = createPage({ name: `第 ${this.document.pages.length + 1} 页`, ...options });
      this.document.pages.push(page);
      this.document.activePageId = page.id;
      this.commitHistory('add-page');
      this.emit('pagechange', { id: page.id });
      return page;
    }

    appendPages(pages = [], options = {}) {
      const normalized = (Array.isArray(pages) ? pages : []).map((page, index) => normalizePage(page, this.document.pages.length + index));
      if (!normalized.length) return [];
      this.beginHistory();
      const activeIndex = this.document.pages.findIndex(page => page.id === this.document.activePageId);
      if (options.replaceCurrent && activeIndex >= 0) this.document.pages.splice(activeIndex, 1, ...normalized);
      else this.document.pages.push(...normalized);
      this.document.activePageId = normalized[0].id;
      this.selected = null;
      this.commitHistory('append-pages');
      this.emit('pagechange', { id: normalized[0].id, count: normalized.length });
      return normalized;
    }

    duplicatePage() {
      this.beginHistory();
      const page = clone(this.page);
      page.id = uid('page');
      page.name = `${this.page.name} 副本`;
      page.layers.forEach(layer => {
        layer.id = uid('layer');
        layer.strokes.forEach(stroke => { stroke.id = uid('stroke'); });
      });
      page.activeLayerId = page.layers[0].id;
      page.objects.forEach(object => { object.id = uid('object'); });
      const index = this.document.pages.findIndex(item => item.id === this.page.id);
      this.document.pages.splice(index + 1, 0, page);
      this.document.activePageId = page.id;
      this.commitHistory('duplicate-page');
      this.emit('pagechange', { id: page.id });
      return page;
    }

    movePage(id, direction) {
      const from = this.document.pages.findIndex(page => page.id === id);
      const to = clamp(from + Number(direction || 0), 0, this.document.pages.length - 1);
      if (from < 0 || from === to) return false;
      return this.movePageTo(id, to);
    }

    movePageTo(id, targetIndex) {
      const from = this.document.pages.findIndex(page => page.id === id);
      const to = clamp(Number(targetIndex || 0), 0, this.document.pages.length - 1);
      if (from < 0 || from === to) return false;
      this.beginHistory();
      const [page] = this.document.pages.splice(from, 1);
      this.document.pages.splice(to, 0, page);
      this.commitHistory('move-page');
      this.emit('pagechange', { id });
      return true;
    }

    removePage() {
      if (this.document.pages.length <= 1) return false;
      this.beginHistory();
      const index = this.document.pages.findIndex(page => page.id === this.page.id);
      const removedPageId = this.page.id;
      this.document.pages.splice(index, 1);
      this.document.bookmarks = (this.document.bookmarks || []).filter(bookmark => bookmark.pageId !== removedPageId);
      this.document.activePageId = this.document.pages[Math.max(0, index - 1)].id;
      this.commitHistory('remove-page');
      this.emit('pagechange', { id: this.document.activePageId });
      return true;
    }

    addLayer(name) {
      this.beginHistory();
      const layer = createLayer(name || `图层 ${this.page.layers.length + 1}`);
      this.page.layers.push(layer);
      this.page.activeLayerId = layer.id;
      this.commitHistory('add-layer');
      this.emit('layerchange', { id: layer.id });
      return layer;
    }

    setLayer(id) {
      if (!this.page.layers.some(layer => layer.id === id)) return false;
      this.page.activeLayerId = id;
      this.selected = null;
      this.emit('layerchange', { id });
      return true;
    }

    updateLayer(id, patch) {
      const layer = this.page.layers.find(item => item.id === id);
      if (!layer) return false;
      this.beginHistory();
      Object.assign(layer, patch);
      this.commitHistory('update-layer');
      this.emit('layerchange', { id });
      return true;
    }

    moveLayer(id, direction) {
      const from = this.page.layers.findIndex(layer => layer.id === id);
      const to = clamp(from + direction, 0, this.page.layers.length - 1);
      if (from < 0 || from === to) return false;
      this.beginHistory();
      const [layer] = this.page.layers.splice(from, 1);
      this.page.layers.splice(to, 0, layer);
      this.commitHistory('move-layer');
      this.emit('layerchange', { id });
      return true;
    }

    removeLayer(id) {
      if (this.page.layers.length <= 1) return false;
      const index = this.page.layers.findIndex(layer => layer.id === id);
      if (index < 0) return false;
      this.beginHistory();
      this.page.layers.splice(index, 1);
      this.page.activeLayerId = this.page.layers[Math.max(0, index - 1)].id;
      this.page.objects.forEach(object => {
        if (object.layerAffinity >= index - 1) object.layerAffinity = Math.max(-1, object.layerAffinity - 1);
      });
      this.commitHistory('remove-layer');
      this.emit('layerchange', { id: this.page.activeLayerId });
      return true;
    }

    addObject(type, data = {}, frame = {}) {
      this.beginHistory();
      const object = normalizeObject({
        id: uid('object'),
        type,
        x: frame.x ?? 120,
        y: frame.y ?? 120,
        width: frame.width ?? (type === 'text' ? 420 : 520),
        height: frame.height ?? (type === 'text' ? 160 : 320),
        zOrder: this.page.objects.length,
        layerAffinity: this.page.layers.findIndex(layer => layer.id === this.page.activeLayerId) - 1,
        data,
      }, this.page.objects.length);
      this.page.objects.push(object);
      this.selected = { kind: 'object', id: object.id };
      this.commitHistory('add-object');
      return object;
    }

    getSelectionItems() {
      if (!this.selected) return [];
      return this.selected.kind === 'group'
        ? this.selected.items.map(item => ({ ...item }))
        : [{ ...this.selected }];
    }

    setSelection(items = []) {
      const unique = [];
      const keys = new Set();
      items.filter(Boolean).forEach(item => {
        const key = `${item.kind}:${item.layerId || ''}:${item.id || ''}`;
        if (!item.id || keys.has(key)) return;
        keys.add(key);
        unique.push({ kind: item.kind, id: item.id, ...(item.layerId ? { layerId: item.layerId } : {}) });
      });
      this.selected = unique.length > 1
        ? { kind: 'group', items: unique }
        : (unique[0] || null);
      return this.selected;
    }

    copySelection() {
      this.clipboard = this.getSelectionItems().map(selection => {
        if (selection.kind === 'object') {
          const object = this.page.objects.find(item => item.id === selection.id);
          return object ? { kind: 'object', item: clone(object) } : null;
        }
        const layer = this.page.layers.find(item => item.id === selection.layerId);
        const stroke = layer?.strokes.find(item => item.id === selection.id);
        return stroke ? { kind: 'stroke', layerId: layer.id, item: clone(stroke) } : null;
      }).filter(Boolean);
      return this.clipboard.length;
    }

    pasteSelection(offset = 32) {
      if (!this.clipboard.length) return 0;
      this.beginHistory();
      const selection = [];
      this.clipboard.forEach(record => {
        if (record.kind === 'object') {
          const object = normalizeObject({
            ...clone(record.item),
            id: uid('object'),
            x: Number(record.item.x || 0) + offset,
            y: Number(record.item.y || 0) + offset,
            zOrder: this.page.objects.length,
          }, this.page.objects.length);
          this.page.objects.push(object);
          selection.push({ kind: 'object', id: object.id });
          return;
        }
        let layer = this.page.layers.find(item => item.id === record.layerId && item.visible && !item.locked);
        if (!layer) layer = this.layer.visible && !this.layer.locked ? this.layer : this.page.layers.find(item => item.visible && !item.locked);
        if (!layer) return;
        const stroke = normalizeStroke({ ...clone(record.item), id: uid('stroke') });
        stroke.points.forEach(point => { point.x += offset; point.y += offset; });
        stroke.bounds = computeBounds(stroke.points, stroke.size);
        layer.strokes.push(stroke);
        selection.push({ kind: 'stroke', id: stroke.id, layerId: layer.id });
      });
      if (!selection.length) {
        this.historyStart = null;
        return 0;
      }
      this.setSelection(selection);
      this.commitHistory('paste-selection');
      return selection.length;
    }

    duplicateSelection() {
      if (!this.copySelection()) return 0;
      return this.pasteSelection(32);
    }

    removeSelection() {
      const selections = this.getSelectionItems();
      if (!selections.length) return false;
      this.beginHistory();
      let removed = 0;
      selections.forEach(selection => {
        if (selection.kind === 'object') {
          const index = this.page.objects.findIndex(object => object.id === selection.id && !object.locked);
          if (index >= 0) {
            this.page.objects.splice(index, 1);
            removed += 1;
          }
          return;
        }
        const layer = this.page.layers.find(item => item.id === selection.layerId);
        const index = layer?.strokes.findIndex(stroke => stroke.id === selection.id) ?? -1;
        if (layer && !layer.locked && index >= 0) {
          layer.strokes.splice(index, 1);
          removed += 1;
        }
      });
      if (!removed) {
        this.historyStart = null;
        return false;
      }
      this.selected = null;
      this.commitHistory('delete-selection');
      return true;
    }
  }

  class CanvasViewport {
    constructor(canvas, session, options = {}) {
      this.canvas = canvas;
      this.session = session;
      this.getStroke = options.getStroke || global.PerfectFreehand?.getStroke;
      this.onChange = options.onChange || (() => {});
      this.onSelectionChange = options.onSelectionChange || (() => {});
      this.onViewportChange = options.onViewportChange || (() => {});
      this.tool = 'pen';
      this.color = '#202522';
      this.size = 5;
      this.penOnly = true;
      this.scale = 1;
      this.offsetX = 0;
      this.offsetY = 0;
      this.pointer = null;
      this.touchPoints = new Map();
      this.pinch = null;
      this.imageCache = new Map();
      this.assetImageCache = new Map();
      this.resolveAsset = typeof options.resolveAsset === 'function' ? options.resolveAsset : null;
      this.raf = 0;
      this.destroyed = false;
      this.resizeObserver = global.ResizeObserver ? new ResizeObserver(() => this.resize()) : null;
      this.bind();
      this.resizeObserver?.observe(canvas);
      this.resize();
    }

    bind() {
      this.canvas.tabIndex = this.canvas.tabIndex >= 0 ? this.canvas.tabIndex : 0;
      this.handlers = {
        pointerdown: event => this.pointerDown(event),
        pointermove: event => this.pointerMove(event),
        pointerup: event => this.pointerUp(event),
        pointercancel: event => this.pointerUp(event),
        wheel: event => this.wheel(event),
        keydown: event => this.keyDown(event),
        contextmenu: event => event.preventDefault(),
      };
      Object.entries(this.handlers).forEach(([name, handler]) => this.canvas.addEventListener(name, handler, { passive: false }));
    }

    destroy() {
      this.destroyed = true;
      this.resizeObserver?.disconnect();
      Object.entries(this.handlers || {}).forEach(([name, handler]) => this.canvas.removeEventListener(name, handler));
      if (this.raf) cancelAnimationFrame(this.raf);
    }

    resize() {
      const rect = this.canvas.getBoundingClientRect();
      if (!rect.width || !rect.height) return;
      const dpr = Math.min(2, global.devicePixelRatio || 1);
      const width = Math.round(rect.width * dpr);
      const height = Math.round(rect.height * dpr);
      if (this.canvas.width !== width || this.canvas.height !== height) {
        this.canvas.width = width;
        this.canvas.height = height;
        this.canvas.dataset.dpr = String(dpr);
        if (!this.session.document.lastViewport) this.fit();
      }
      this.requestRender();
    }

    setTool(tool) {
      this.tool = ['pen', 'highlighter', 'eraser', 'select', 'pan'].includes(tool) ? tool : 'pen';
      this.canvas.dataset.tool = this.tool;
      this.requestRender();
    }

    setStyle({ color, size, penOnly } = {}) {
      if (color) this.color = color;
      if (size != null) this.size = clamp(size, 1, 48);
      if (penOnly != null) this.penOnly = Boolean(penOnly);
    }

    get viewportSize() {
      const rect = this.canvas.getBoundingClientRect();
      return { width: rect.width, height: rect.height };
    }

    fit() {
      const page = this.session.page;
      const view = this.viewportSize;
      const margin = 32;
      this.scale = clamp(Math.min((view.width - margin * 2) / page.width, (view.height - margin * 2) / page.height), .15, 4);
      this.offsetX = (view.width - page.width * this.scale) / 2;
      this.offsetY = (view.height - page.height * this.scale) / 2;
      this.saveViewport();
      this.onViewportChange({ scale: this.scale, offsetX: this.offsetX, offsetY: this.offsetY });
      this.requestRender();
    }

    zoomAt(screenX, screenY, factor) {
      const before = this.screenToPage(screenX, screenY);
      this.scale = clamp(this.scale * factor, .15, 6);
      this.offsetX = screenX - before.x * this.scale;
      this.offsetY = screenY - before.y * this.scale;
      this.saveViewport();
      this.onViewportChange({ scale: this.scale, offsetX: this.offsetX, offsetY: this.offsetY });
      this.requestRender();
    }

    saveViewport() {
      const viewport = this.viewportSize;
      this.session.document.lastViewport = {
        scale: this.scale,
        offsetX: this.offsetX,
        offsetY: this.offsetY,
        viewportWidth: viewport.width,
        viewportHeight: viewport.height,
      };
    }

    restoreViewport() {
      const saved = this.session.document.lastViewport;
      if (!saved) return this.fit();
      const viewport = this.viewportSize;
      const savedWidth = Number(saved.viewportWidth || 0);
      const savedHeight = Number(saved.viewportHeight || 0);
      const orientationChanged = savedWidth && savedHeight && (savedWidth >= savedHeight) !== (viewport.width >= viewport.height);
      const widthRatio = savedWidth ? viewport.width / savedWidth : 0;
      const heightRatio = savedHeight ? viewport.height / savedHeight : 0;
      const sizeChanged = !savedWidth || !savedHeight || widthRatio < .72 || widthRatio > 1.38 || heightRatio < .72 || heightRatio > 1.38;
      if (orientationChanged || sizeChanged) return this.fit();
      this.scale = clamp(saved.scale || 1, .15, 6);
      this.offsetX = Number(saved.offsetX || 0);
      this.offsetY = Number(saved.offsetY || 0);
      this.requestRender();
    }

    screenToPage(clientX, clientY) {
      const rect = this.canvas.getBoundingClientRect();
      return { x: (clientX - rect.left - this.offsetX) / this.scale, y: (clientY - rect.top - this.offsetY) / this.scale };
    }

    pageContains(point) {
      const page = this.session.page;
      return point.x >= 0 && point.y >= 0 && point.x <= page.width && point.y <= page.height;
    }

    wheel(event) {
      event.preventDefault();
      if (event.ctrlKey || Math.abs(event.deltaY) > Math.abs(event.deltaX)) {
        this.zoomAt(event.clientX, event.clientY, Math.exp(-event.deltaY * .0015));
      } else {
        this.offsetX -= event.deltaX;
        this.saveViewport();
        this.onViewportChange({ scale: this.scale, offsetX: this.offsetX, offsetY: this.offsetY });
        this.requestRender();
      }
    }

    pointerDown(event) {
      if (event.pointerType === 'touch') {
        this.touchPoints.set(event.pointerId, { x: event.clientX, y: event.clientY });
        if (this.touchPoints.size >= 2) {
          const points = [...this.touchPoints.values()];
          this.pinch = {
            distance: Math.hypot(points[0].x - points[1].x, points[0].y - points[1].y),
            center: { x: (points[0].x + points[1].x) / 2, y: (points[0].y + points[1].y) / 2 },
          };
          this.pointer = null;
          return;
        }
      }
      if (event.pointerType === 'touch' && this.penOnly && this.tool !== 'pan') {
        this.startPan(event);
        return;
      }
      event.preventDefault();
      this.canvas.focus({ preventScroll: true });
      this.canvas.setPointerCapture?.(event.pointerId);
      const point = this.screenToPage(event.clientX, event.clientY);
      if (this.tool === 'pan' || event.button === 1 || event.button === 2) return this.startPan(event);
      if (!this.pageContains(point)) return;
      if (this.tool === 'eraser') {
        this.session.beginHistory();
        this.pointer = { id: event.pointerId, mode: 'erase' };
        this.eraseAt(point);
        return;
      }
      if (this.tool === 'select') {
        const handle = this.selectionHandleAt(point);
        if (handle) return this.startObjectTransform(event, point, handle);
        return this.startSelection(event, point);
      }
      if (this.session.layer.locked || !this.session.layer.visible) return;
      this.session.beginHistory();
      const stroke = normalizeStroke({
        id: uid('stroke'),
        tool: this.tool,
        color: this.color,
        size: this.tool === 'highlighter' ? Math.max(14, this.size * 3) : this.size,
        pointerType: event.pointerType || 'mouse',
        points: [this.eventPoint(event)],
      });
      this.session.layer.strokes.push(stroke);
      this.pointer = { id: event.pointerId, mode: 'draw', stroke };
      this.requestRender();
    }

    startPan(event) {
      this.pointer = { id: event.pointerId, mode: 'pan', x: event.clientX, y: event.clientY };
    }

    startSelection(event, point) {
      const hit = this.hitTest(point);
      if (!hit) {
        if (!event.shiftKey) this.session.setSelection([]);
        this.pointer = { id: event.pointerId, mode: 'lasso', start: point, end: point };
        this.onSelectionChange(this.session.selected);
        return this.requestRender();
      }
      if (event.shiftKey) {
        const selections = this.session.getSelectionItems();
        const key = `${hit.kind}:${hit.layerId || ''}:${hit.id}`;
        const exists = selections.findIndex(item => `${item.kind}:${item.layerId || ''}:${item.id}` === key);
        if (exists >= 0) selections.splice(exists, 1);
        else selections.push(hit);
        this.session.setSelection(selections);
      } else {
        const alreadySelected = this.session.getSelectionItems().some(item => item.kind === hit.kind && item.id === hit.id && item.layerId === hit.layerId);
        if (!alreadySelected) this.session.setSelection([hit]);
      }
      this.onSelectionChange(this.session.selected);
      if (!this.session.selected) return this.requestRender();
      const target = this.getSelectionTarget(hit);
      if (!target || target.locked) return this.requestRender();
      this.session.beginHistory();
      this.pointer = { id: event.pointerId, mode: 'move', x: point.x, y: point.y, hit: this.session.selected };
      this.requestRender();
    }

    selectionHandleAt(point) {
      const selected = this.session.selected;
      if (selected?.kind !== 'object') return '';
      const object = this.getSelectionTarget(selected);
      if (!object || object.locked) return '';
      const tolerance = 18 / this.scale;
      const center = { x: object.x + object.width / 2, y: object.y + object.height / 2 };
      const resizePoint = rotatePoint({ x: object.x + object.width, y: object.y + object.height }, center, object.rotation);
      const rotateHandle = rotatePoint({ x: object.x + object.width / 2, y: object.y - 34 / this.scale }, center, object.rotation);
      if (Math.hypot(point.x - resizePoint.x, point.y - resizePoint.y) <= tolerance) return 'resize';
      if (Math.hypot(point.x - rotateHandle.x, point.y - rotateHandle.y) <= tolerance) return 'rotate';
      return '';
    }

    startObjectTransform(event, point, mode) {
      const object = this.getSelectionTarget(this.session.selected);
      if (!object || object.locked) return;
      this.session.beginHistory();
      this.pointer = {
        id: event.pointerId,
        mode,
        object,
        x: point.x,
        y: point.y,
        startWidth: object.width,
        startHeight: object.height,
        startRotation: object.rotation,
        startAngle: Math.atan2(point.y - (object.y + object.height / 2), point.x - (object.x + object.width / 2)),
      };
    }

    pointerMove(event) {
      if (event.pointerType === 'touch' && this.touchPoints.has(event.pointerId)) {
        this.touchPoints.set(event.pointerId, { x: event.clientX, y: event.clientY });
        if (this.touchPoints.size >= 2 && this.pinch) {
          event.preventDefault();
          const points = [...this.touchPoints.values()];
          const distance = Math.hypot(points[0].x - points[1].x, points[0].y - points[1].y);
          const center = { x: (points[0].x + points[1].x) / 2, y: (points[0].y + points[1].y) / 2 };
          this.offsetX += center.x - this.pinch.center.x;
          this.offsetY += center.y - this.pinch.center.y;
          this.zoomAt(center.x, center.y, distance / Math.max(1, this.pinch.distance));
          this.pinch = { distance, center };
          return;
        }
      }
      if (!this.pointer || this.pointer.id !== event.pointerId) return;
      event.preventDefault();
      if (this.pointer.mode === 'pan') {
        this.offsetX += event.clientX - this.pointer.x;
        this.offsetY += event.clientY - this.pointer.y;
        this.pointer.x = event.clientX;
        this.pointer.y = event.clientY;
        this.saveViewport();
        this.onViewportChange({ scale: this.scale, offsetX: this.offsetX, offsetY: this.offsetY });
      } else if (this.pointer.mode === 'draw') {
        const coalesced = typeof event.getCoalescedEvents === 'function' ? event.getCoalescedEvents() : [event];
        coalesced.forEach(item => this.pointer.stroke.points.push(this.eventPoint(item)));
      } else if (this.pointer.mode === 'erase') {
        this.eraseAt(this.screenToPage(event.clientX, event.clientY));
      } else if (this.pointer.mode === 'move') {
        const point = this.screenToPage(event.clientX, event.clientY);
        this.moveSelection(this.pointer.hit, point.x - this.pointer.x, point.y - this.pointer.y);
        this.pointer.x = point.x;
        this.pointer.y = point.y;
      } else if (this.pointer.mode === 'lasso') {
        this.pointer.end = this.screenToPage(event.clientX, event.clientY);
      } else if (this.pointer.mode === 'resize') {
        const point = this.screenToPage(event.clientX, event.clientY);
        this.pointer.object.width = Math.max(80, this.pointer.startWidth + point.x - this.pointer.x);
        this.pointer.object.height = Math.max(60, this.pointer.startHeight + point.y - this.pointer.y);
      } else if (this.pointer.mode === 'rotate') {
        const point = this.screenToPage(event.clientX, event.clientY);
        const object = this.pointer.object;
        const angle = Math.atan2(point.y - (object.y + object.height / 2), point.x - (object.x + object.width / 2));
        object.rotation = this.pointer.startRotation + (angle - this.pointer.startAngle) * 180 / Math.PI;
      }
      this.requestRender();
    }

    pointerUp(event) {
      if (event.pointerType === 'touch') {
        this.touchPoints.delete(event.pointerId);
        if (this.touchPoints.size < 2) this.pinch = null;
      }
      if (!this.pointer || this.pointer.id !== event.pointerId) return;
      event.preventDefault();
      if (this.pointer.mode === 'draw') {
        this.pointer.stroke.points.push(this.eventPoint(event));
        this.pointer.stroke.bounds = computeBounds(this.pointer.stroke.points, this.pointer.stroke.size);
        this.session.commitHistory('stroke');
        this.onChange('stroke');
      } else if (this.pointer.mode === 'erase') {
        this.session.commitHistory('erase');
        this.onChange('erase');
      } else if (this.pointer.mode === 'move') {
        this.session.commitHistory('move-selection');
        this.onChange('move-selection');
      } else if (this.pointer.mode === 'lasso') {
        const rect = rectFromPoints(this.pointer.start, this.pointer.end);
        const selected = this.selectInRect(rect);
        this.session.setSelection(selected);
        this.onSelectionChange(this.session.selected);
      } else if (this.pointer.mode === 'resize' || this.pointer.mode === 'rotate') {
        this.session.commitHistory(`transform-${this.pointer.mode}`);
        this.onChange(`transform-${this.pointer.mode}`);
      }
      this.pointer = null;
      this.requestRender();
    }

    keyDown(event) {
      const modifier = event.ctrlKey || event.metaKey;
      const key = String(event.key || '').toLowerCase();
      if (modifier && key === 'c') {
        if (this.session.copySelection()) event.preventDefault();
        return;
      }
      if (modifier && key === 'v') {
        if (this.session.pasteSelection()) {
          event.preventDefault();
          this.onSelectionChange(this.session.selected);
          this.onChange('paste-selection');
          this.requestRender();
        }
        return;
      }
      if (modifier && key === 'd') {
        if (this.session.duplicateSelection()) {
          event.preventDefault();
          this.onSelectionChange(this.session.selected);
          this.onChange('duplicate-selection');
          this.requestRender();
        }
        return;
      }
      if (key === 'delete' || key === 'backspace') {
        if (this.session.removeSelection()) {
          event.preventDefault();
          this.onSelectionChange(null);
          this.onChange('delete-selection');
          this.requestRender();
        }
      }
    }

    eventPoint(event) {
      const point = this.screenToPage(event.clientX, event.clientY);
      return {
        x: clamp(point.x, 0, this.session.page.width),
        y: clamp(point.y, 0, this.session.page.height),
        pressure: event.pointerType === 'pen' ? clamp(event.pressure || .5, .05, 1) : .5,
        time: Number(event.timeStamp || 0),
      };
    }

    hitTest(point) {
      const objects = [...this.session.page.objects].sort((a, b) => b.zOrder - a.zOrder);
      const object = objects.find(item => item.visible && pointInObject(point, item));
      if (object) return { kind: 'object', id: object.id };
      const layers = [...this.session.page.layers].reverse();
      for (const layer of layers) {
        if (!layer.visible) continue;
        for (let index = layer.strokes.length - 1; index >= 0; index -= 1) {
          if (strokeContainsPoint(layer.strokes[index], point, 12 / this.scale)) return { kind: 'stroke', id: layer.strokes[index].id, layerId: layer.id };
        }
      }
      return null;
    }

    getSelectionTarget(hit) {
      if (!hit) return null;
      if (hit.kind === 'group') return null;
      if (hit.kind === 'object') return this.session.page.objects.find(object => object.id === hit.id);
      return this.session.page.layers.find(layer => layer.id === hit.layerId)?.strokes.find(stroke => stroke.id === hit.id);
    }

    moveSelection(hit, dx, dy) {
      if (hit?.kind === 'group') {
        hit.items.forEach(item => this.moveSelection(item, dx, dy));
        return;
      }
      const target = this.getSelectionTarget(hit);
      if (!target) return;
      if (hit.kind === 'object') {
        if (target.locked) return;
        target.x += dx;
        target.y += dy;
      } else {
        const layer = this.session.page.layers.find(item => item.id === hit.layerId);
        if (layer?.locked) return;
        target.points.forEach(point => { point.x += dx; point.y += dy; });
        target.bounds = computeBounds(target.points, target.size);
      }
    }

    selectInRect(rect) {
      if (rect.width < 4 / this.scale && rect.height < 4 / this.scale) return [];
      const selected = [];
      this.session.page.objects.forEach(object => {
        if (object.visible && boundsIntersect(rect, objectBounds(object))) selected.push({ kind: 'object', id: object.id });
      });
      this.session.page.layers.forEach(layer => {
        if (!layer.visible) return;
        layer.strokes.forEach(stroke => {
          const bounds = stroke.bounds || computeBounds(stroke.points, stroke.size);
          if (boundsIntersect(rect, bounds)) selected.push({ kind: 'stroke', id: stroke.id, layerId: layer.id });
        });
      });
      return selected;
    }

    eraseAt(point) {
      const layers = [...this.session.page.layers].reverse();
      for (const layer of layers) {
        if (!layer.visible || layer.locked) continue;
        const index = layer.strokes.findIndex(stroke => strokeContainsPoint(stroke, point, Math.max(12, this.size * 2) / this.scale));
        if (index >= 0) {
          layer.strokes.splice(index, 1);
          return true;
        }
      }
      return false;
    }

    requestRender() {
      if (this.raf || this.destroyed) return;
      this.raf = requestAnimationFrame(() => {
        this.raf = 0;
        this.render();
      });
    }

    render() {
      const canvas = this.canvas;
      const rect = canvas.getBoundingClientRect();
      if (!rect.width || !rect.height) return;
      const dpr = Number(canvas.dataset.dpr || 1);
      const context = canvas.getContext('2d');
      context.setTransform(dpr, 0, 0, dpr, 0, 0);
      context.clearRect(0, 0, rect.width, rect.height);
      context.fillStyle = '#e9eeeb';
      context.fillRect(0, 0, rect.width, rect.height);
      const page = this.session.page;
      context.save();
      context.translate(this.offsetX, this.offsetY);
      context.scale(this.scale, this.scale);
      context.shadowColor = 'rgba(20, 34, 27, .14)';
      context.shadowBlur = 24 / this.scale;
      context.shadowOffsetY = 8 / this.scale;
      context.fillStyle = page.background.color;
      context.fillRect(0, 0, page.width, page.height);
      context.shadowColor = 'transparent';
      this.renderBackground(context, page);
      this.renderObjects(context, page, -1);
      page.layers.forEach((layer, index) => {
        if (layer.visible) this.renderLayer(context, layer);
        this.renderObjects(context, page, index);
      });
      this.renderSelection(context);
      this.renderLasso(context);
      context.strokeStyle = '#cbd5cf';
      context.lineWidth = 1 / this.scale;
      context.strokeRect(0, 0, page.width, page.height);
      context.restore();
    }

    renderBackground(context, page) {
      const background = page.background;
      if (background.type === 'pdf' && background.assetId) {
        this.renderAssetBackground(context, page, background);
        return;
      }
      context.save();
      context.strokeStyle = background.patternColor;
      context.lineWidth = 1 / this.scale;
      const spacing = Math.max(16, background.spacing);
      if (background.type === 'grid') {
        for (let x = spacing; x < page.width; x += spacing) {
          context.beginPath(); context.moveTo(x, 0); context.lineTo(x, page.height); context.stroke();
        }
      }
      if (background.type === 'grid' || background.type === 'lines') {
        for (let y = spacing; y < page.height; y += spacing) {
          context.beginPath(); context.moveTo(0, y); context.lineTo(page.width, y); context.stroke();
        }
      }
      context.restore();
    }

    renderAssetBackground(context, page, background) {
      const key = String(background.assetId || '');
      if (!key || !this.resolveAsset) return;
      const cached = this.assetImageCache.get(key);
      if (cached?.image?.complete) {
        context.drawImage(cached.image, 0, 0, page.width, page.height);
        return;
      }
      if (cached?.pending) return;
      const record = { pending: true, image: null };
      this.assetImageCache.set(key, record);
      Promise.resolve(this.resolveAsset(key)).then(source => {
        if (!source || this.destroyed) {
          this.assetImageCache.delete(key);
          return;
        }
        const image = new Image();
        image.onload = () => {
          record.pending = false;
          record.image = image;
          this.requestRender();
        };
        image.onerror = () => this.assetImageCache.delete(key);
        image.src = source;
      }).catch(() => this.assetImageCache.delete(key));
    }

    renderLayer(context, layer) {
      context.save();
      context.globalAlpha = layer.opacity;
      layer.strokes.forEach(stroke => this.renderStroke(context, stroke));
      context.restore();
    }

    renderStroke(context, stroke) {
      if (!stroke.points.length) return;
      const points = stroke.points.map(point => [point.x, point.y, point.pressure]);
      const outline = this.getStroke ? this.getStroke(points, {
        size: stroke.size,
        thinning: stroke.tool === 'highlighter' ? .1 : .55,
        smoothing: .68,
        streamline: .42,
        simulatePressure: stroke.pointerType !== 'pen',
        last: this.pointer?.stroke !== stroke,
      }) : points;
      if (!outline.length) return;
      context.save();
      context.fillStyle = stroke.color;
      context.globalAlpha *= stroke.tool === 'highlighter' ? .28 : 1;
      context.beginPath();
      context.moveTo(outline[0][0], outline[0][1]);
      for (let index = 1; index < outline.length - 1; index += 1) {
        const current = outline[index];
        const next = outline[index + 1];
        context.quadraticCurveTo(current[0], current[1], (current[0] + next[0]) / 2, (current[1] + next[1]) / 2);
      }
      context.closePath();
      context.fill();
      context.restore();
    }

    renderObjects(context, page, affinity) {
      page.objects
        .filter(object => object.visible && object.layerAffinity === affinity)
        .sort((a, b) => a.zOrder - b.zOrder)
        .forEach(object => this.renderObject(context, object));
    }

    renderObject(context, object) {
      context.save();
      context.translate(object.x + object.width / 2, object.y + object.height / 2);
      context.rotate(object.rotation * Math.PI / 180);
      context.translate(-object.width / 2, -object.height / 2);
      if (object.type === 'image' || object.type === 'question' || object.type === 'markdown' || object.type === 'formula') {
        const image = this.imageCache.get(object.id);
        if (image?.complete) context.drawImage(image, 0, 0, object.width, object.height);
        else if (object.data?.src && !this.imageCache.has(object.id)) {
          const loaded = new Image();
          loaded.onload = () => this.requestRender();
          loaded.src = object.data.src;
          this.imageCache.set(object.id, loaded);
        }
      } else {
        context.fillStyle = object.data?.fill || '#fffbe8';
        context.strokeStyle = object.data?.border || '#d7c87b';
        context.lineWidth = 1 / this.scale;
        context.fillRect(0, 0, object.width, object.height);
        context.strokeRect(0, 0, object.width, object.height);
        context.fillStyle = object.data?.color || '#202522';
        context.font = `${Math.max(14, Number(object.data?.fontSize || 24))}px sans-serif`;
        this.wrapText(context, object.data?.text || object.data?.url || '文本', 18, 34, object.width - 36, Number(object.data?.lineHeight || 34));
      }
      context.restore();
    }

    wrapText(context, text, x, y, maxWidth, lineHeight) {
      const chars = String(text || '').split('');
      let line = '';
      let cursorY = y;
      chars.forEach(char => {
        const test = line + char;
        if (context.measureText(test).width > maxWidth && line) {
          context.fillText(line, x, cursorY);
          line = char;
          cursorY += lineHeight;
        } else line = test;
      });
      if (line) context.fillText(line, x, cursorY);
    }

    renderSelection(context) {
      const selections = this.session.getSelectionItems();
      if (!selections.length) return;
      const targets = selections.map(selection => {
        const target = this.getSelectionTarget(selection);
        if (!target) return null;
        return selection.kind === 'object'
          ? objectBounds(target)
          : (target.bounds || computeBounds(target.points, target.size));
      }).filter(Boolean);
      const bounds = unionBounds(targets);
      if (!bounds) return;
      context.save();
      context.strokeStyle = '#1f8f62';
      context.setLineDash([8 / this.scale, 5 / this.scale]);
      context.lineWidth = 2 / this.scale;
      const singleObject = selections.length === 1 && selections[0].kind === 'object'
        ? this.getSelectionTarget(selections[0])
        : null;
      if (singleObject) {
        context.translate(singleObject.x + singleObject.width / 2, singleObject.y + singleObject.height / 2);
        context.rotate(singleObject.rotation * Math.PI / 180);
        context.strokeRect(-singleObject.width / 2, -singleObject.height / 2, singleObject.width, singleObject.height);
        const handleSize = 12 / this.scale;
        context.setLineDash([]);
        context.fillStyle = '#ffffff';
        context.strokeStyle = '#1f8f62';
        context.lineWidth = 2 / this.scale;
        const resizeX = singleObject.width / 2 - handleSize / 2;
        const resizeY = singleObject.height / 2 - handleSize / 2;
        context.fillRect(resizeX, resizeY, handleSize, handleSize);
        context.strokeRect(resizeX, resizeY, handleSize, handleSize);
        const rotateY = -singleObject.height / 2 - 34 / this.scale;
        context.beginPath();
        context.moveTo(0, -singleObject.height / 2);
        context.lineTo(0, rotateY);
        context.stroke();
        context.beginPath();
        context.arc(0, rotateY, 7 / this.scale, 0, Math.PI * 2);
        context.fill();
        context.stroke();
      } else context.strokeRect(bounds.x, bounds.y, bounds.width, bounds.height);
      context.restore();
    }

    renderLasso(context) {
      if (this.pointer?.mode !== 'lasso') return;
      const rect = rectFromPoints(this.pointer.start, this.pointer.end);
      context.save();
      context.fillStyle = 'rgba(31, 143, 98, .08)';
      context.strokeStyle = '#1f8f62';
      context.lineWidth = 1.5 / this.scale;
      context.setLineDash([6 / this.scale, 4 / this.scale]);
      context.fillRect(rect.x, rect.y, rect.width, rect.height);
      context.strokeRect(rect.x, rect.y, rect.width, rect.height);
      context.restore();
    }
  }

  function renderPageThumbnail(canvas, page, getStroke = global.PerfectFreehand?.getStroke, assetSource = '') {
    if (!canvas || !page) return;
    const width = Math.max(1, Number(canvas.width || 72));
    const height = Math.max(1, Number(canvas.height || 96));
    const context = canvas.getContext('2d');
    const scale = Math.min(width / page.width, height / page.height);
    const offsetX = (width - page.width * scale) / 2;
    const offsetY = (height - page.height * scale) / 2;
    context.clearRect(0, 0, width, height);
    context.fillStyle = '#e7ece9';
    context.fillRect(0, 0, width, height);
    context.save();
    context.translate(offsetX, offsetY);
    context.scale(scale, scale);
    context.fillStyle = page.background?.color || '#ffffff';
    context.fillRect(0, 0, page.width, page.height);
    if (page.background?.type === 'pdf' && page.background?.assetId && assetSource) {
      const key = String(page.background.assetId);
      let cached = thumbnailAssetCache.get(key);
      if (!cached || cached.source !== assetSource) {
        const image = new Image();
        cached = { source: assetSource, image, loaded: false };
        thumbnailAssetCache.set(key, cached);
        image.onload = () => {
          cached.loaded = true;
          renderPageThumbnail(canvas, page, getStroke, assetSource);
        };
        image.src = assetSource;
      }
      if (cached.loaded) context.drawImage(cached.image, 0, 0, page.width, page.height);
    }
    context.strokeStyle = page.background?.patternColor || '#dfe6e2';
    context.lineWidth = Math.max(1, 1 / scale);
    const spacing = Math.max(20, Number(page.background?.spacing || 40));
    if (page.background?.type === 'grid') {
      for (let x = spacing; x < page.width; x += spacing) {
        context.beginPath(); context.moveTo(x, 0); context.lineTo(x, page.height); context.stroke();
      }
    }
    if (page.background?.type === 'grid' || page.background?.type === 'lines') {
      for (let y = spacing; y < page.height; y += spacing) {
        context.beginPath(); context.moveTo(0, y); context.lineTo(page.width, y); context.stroke();
      }
    }
    page.layers.filter(layer => layer.visible).forEach(layer => {
      context.save();
      context.globalAlpha = Number(layer.opacity ?? 1);
      layer.strokes.forEach(stroke => {
        if (!stroke.points?.length) return;
        const points = stroke.points.map(point => [point.x, point.y, point.pressure]);
        const outline = getStroke ? getStroke(points, {
          size: stroke.size,
          thinning: stroke.tool === 'highlighter' ? .1 : .55,
          smoothing: .68,
          streamline: .42,
          simulatePressure: stroke.pointerType !== 'pen',
          last: true,
        }) : points;
        if (!outline.length) return;
        context.save();
        context.fillStyle = stroke.color;
        context.globalAlpha *= stroke.tool === 'highlighter' ? .28 : 1;
        context.beginPath();
        context.moveTo(outline[0][0], outline[0][1]);
        outline.slice(1).forEach(point => context.lineTo(point[0], point[1]));
        context.closePath();
        context.fill();
        context.restore();
      });
      context.restore();
    });
    page.objects.filter(object => object.visible).forEach(object => {
      context.save();
      context.translate(object.x + object.width / 2, object.y + object.height / 2);
      context.rotate(Number(object.rotation || 0) * Math.PI / 180);
      context.fillStyle = object.type === 'text' || object.type === 'link' ? (object.data?.fill || '#fffbe8') : '#edf2ef';
      context.strokeStyle = object.data?.border || '#aeb9b3';
      context.fillRect(-object.width / 2, -object.height / 2, object.width, object.height);
      context.strokeRect(-object.width / 2, -object.height / 2, object.width, object.height);
      context.restore();
    });
    context.strokeStyle = '#c4cec8';
    context.lineWidth = Math.max(1, 1 / scale);
    context.strokeRect(0, 0, page.width, page.height);
    context.restore();
  }

  global.QuizNotebook = {
    SCHEMA_VERSION,
    NotebookRepository,
    NotebookSession,
    CanvasViewport,
    createDocument,
    createPage,
    createLayer,
    normalizePage,
    normalizeDocument,
    migrateLegacyInk,
    computeBounds,
    renderPageThumbnail,
  };
})(window);
