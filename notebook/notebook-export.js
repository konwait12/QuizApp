/* Notebook page rasterization and annotated PDF export. GPL-3.0-or-later. */
(function attachQuizNotebookExport(global) {
  'use strict';

  const PDF_POINT_SCALE = 72 / 96;
  const DEFAULT_MAX_PIXELS = 4200000;
  const imageCache = new Map();

  function clamp(value, min, max) {
    return Math.max(min, Math.min(max, Number(value)));
  }

  function nextFrame() {
    return new Promise(resolve => global.setTimeout(resolve, 0));
  }

  function progress(callback, stage, percent, message, details = {}) {
    if (typeof callback === 'function') callback({ stage, percent, message, ...details });
  }

  function safeFilename(value, fallback = 'QuizApp-notebook') {
    const cleaned = String(value || '')
      .replace(/[<>:"/\\|?*\u0000-\u001f]/g, ' ')
      .replace(/\s+/g, ' ')
      .replace(/[. ]+$/g, '')
      .trim();
    return (cleaned || fallback).slice(0, 96);
  }

  function loadImage(source) {
    const src = String(source || '');
    if (!src) return Promise.resolve(null);
    if (imageCache.has(src)) return imageCache.get(src);
    const pending = new Promise((resolve, reject) => {
      const image = new global.Image();
      if (/^https:\/\//i.test(src)) image.crossOrigin = 'anonymous';
      image.onload = () => resolve(image);
      image.onerror = () => reject(new Error('图片资源无法读取'));
      image.src = src;
    }).catch(error => {
      imageCache.delete(src);
      throw error;
    });
    imageCache.set(src, pending);
    return pending;
  }

  function strokeOutline(stroke, getStroke) {
    const points = (stroke.points || []).map(point => [point.x, point.y, point.pressure]);
    if (!points.length) return [];
    return getStroke ? getStroke(points, {
      size: stroke.size,
      thinning: stroke.tool === 'highlighter' ? .1 : .55,
      smoothing: .68,
      streamline: .42,
      simulatePressure: stroke.pointerType !== 'pen',
      last: true,
    }) : points;
  }

  function drawStroke(context, stroke, getStroke) {
    const outline = strokeOutline(stroke, getStroke);
    if (!outline.length) return;
    context.save();
    context.fillStyle = stroke.color || '#202522';
    context.globalAlpha *= stroke.tool === 'highlighter' ? .28 : 1;
    context.beginPath();
    context.moveTo(outline[0][0], outline[0][1]);
    for (let index = 1; index < outline.length - 1; index += 1) {
      const current = outline[index];
      const next = outline[index + 1];
      context.quadraticCurveTo(current[0], current[1], (current[0] + next[0]) / 2, (current[1] + next[1]) / 2);
    }
    if (outline.length > 1) context.lineTo(outline[outline.length - 1][0], outline[outline.length - 1][1]);
    context.closePath();
    context.fill();
    context.restore();
  }

  function wrapText(context, value, x, y, maxWidth, lineHeight, maxHeight) {
    const paragraphs = String(value || '').replace(/\r/g, '').split('\n');
    let cursorY = y;
    for (const paragraph of paragraphs) {
      if (!paragraph) {
        cursorY += lineHeight;
        if (cursorY > maxHeight) break;
        continue;
      }
      let line = '';
      for (const character of Array.from(paragraph)) {
        const next = line + character;
        if (line && context.measureText(next).width > maxWidth) {
          context.fillText(line, x, cursorY);
          cursorY += lineHeight;
          if (cursorY > maxHeight) return;
          line = character;
        } else line = next;
      }
      if (line && cursorY <= maxHeight) context.fillText(line, x, cursorY);
      cursorY += lineHeight;
      if (cursorY > maxHeight) break;
    }
  }

  async function resolvePageImages(page, resolveAsset) {
    const sources = new Map();
    if (page.background?.type === 'pdf' && page.background.assetId && typeof resolveAsset === 'function') {
      try {
        const source = await resolveAsset(page.background.assetId);
        if (source) sources.set(`asset:${page.background.assetId}`, await loadImage(source));
      } catch(e) {}
    }
    const objects = (page.objects || []).filter(object => object.visible && ['image', 'question', 'markdown', 'formula'].includes(object.type) && object.data?.src);
    await Promise.all(objects.map(async object => {
      try {
        sources.set(`object:${object.id}`, await loadImage(object.data.src));
      } catch(e) {}
    }));
    return sources;
  }

  function drawBackground(context, page, images) {
    const background = page.background || {};
    context.fillStyle = background.color || '#ffffff';
    context.fillRect(0, 0, page.width, page.height);
    if (background.type === 'pdf' && background.assetId) {
      const image = images.get(`asset:${background.assetId}`);
      if (image) context.drawImage(image, 0, 0, page.width, page.height);
      return;
    }
    const spacing = Math.max(16, Number(background.spacing || 40));
    context.save();
    context.strokeStyle = background.patternColor || '#dfe6e2';
    context.lineWidth = 1;
    if (background.type === 'grid') {
      for (let x = spacing; x < page.width; x += spacing) {
        context.beginPath();
        context.moveTo(x, 0);
        context.lineTo(x, page.height);
        context.stroke();
      }
    }
    if (background.type === 'grid' || background.type === 'lines') {
      for (let y = spacing; y < page.height; y += spacing) {
        context.beginPath();
        context.moveTo(0, y);
        context.lineTo(page.width, y);
        context.stroke();
      }
    }
    context.restore();
  }

  function drawObject(context, object, images) {
    context.save();
    context.translate(object.x + object.width / 2, object.y + object.height / 2);
    context.rotate(Number(object.rotation || 0) * Math.PI / 180);
    context.translate(-object.width / 2, -object.height / 2);
    if (['image', 'question', 'markdown', 'formula'].includes(object.type)) {
      const image = images.get(`object:${object.id}`);
      if (image) context.drawImage(image, 0, 0, object.width, object.height);
      else {
        context.fillStyle = '#edf2ef';
        context.fillRect(0, 0, object.width, object.height);
        context.strokeStyle = '#aeb9b3';
        context.strokeRect(0, 0, object.width, object.height);
      }
    } else {
      context.fillStyle = object.data?.fill || '#fffbe8';
      context.strokeStyle = object.data?.border || '#d7c87b';
      context.lineWidth = 1;
      context.fillRect(0, 0, object.width, object.height);
      context.strokeRect(0, 0, object.width, object.height);
      context.save();
      context.beginPath();
      context.rect(1, 1, Math.max(0, object.width - 2), Math.max(0, object.height - 2));
      context.clip();
      context.fillStyle = object.data?.color || '#202522';
      const fontSize = Math.max(14, Number(object.data?.fontSize || 24));
      const lineHeight = Math.max(fontSize * 1.2, Number(object.data?.lineHeight || 34));
      context.font = `${fontSize}px sans-serif`;
      context.textBaseline = 'alphabetic';
      const text = object.data?.text || object.data?.label || object.data?.url || '文本';
      wrapText(context, text, 18, Math.max(24, lineHeight), Math.max(1, object.width - 36), lineHeight, object.height - 12);
      context.restore();
    }
    context.restore();
  }

  function drawObjects(context, page, images, affinity) {
    (page.objects || [])
      .filter(object => object.visible && Number(object.layerAffinity ?? -1) === affinity)
      .sort((left, right) => Number(left.zOrder || 0) - Number(right.zOrder || 0))
      .forEach(object => drawObject(context, object, images));
  }

  async function renderPage(page, options = {}) {
    if (!page) throw new Error('笔记页不存在');
    const width = Math.max(1, Number(page.width || 1200));
    const height = Math.max(1, Number(page.height || 1600));
    const maxPixels = Math.max(1000000, Number(options.maxPixels || DEFAULT_MAX_PIXELS));
    const requestedScale = clamp(options.scale || 1, .5, 2);
    const scale = Math.min(requestedScale, Math.sqrt(maxPixels / Math.max(1, width * height)));
    const canvas = global.document.createElement('canvas');
    canvas.width = Math.max(1, Math.round(width * scale));
    canvas.height = Math.max(1, Math.round(height * scale));
    const context = canvas.getContext('2d', { alpha: false });
    const images = await resolvePageImages(page, options.resolveAsset);
    context.save();
    context.scale(scale, scale);
    drawBackground(context, page, images);
    drawObjects(context, page, images, -1);
    (page.layers || []).forEach((layer, index) => {
      if (layer.visible) {
        context.save();
        context.globalAlpha = clamp(layer.opacity ?? 1, 0, 1);
        (layer.strokes || []).forEach(stroke => drawStroke(context, stroke, options.getStroke || global.PerfectFreehand?.getStroke));
        context.restore();
      }
      drawObjects(context, page, images, index);
    });
    context.restore();
    return canvas;
  }

  function canvasBlob(canvas, type, quality) {
    return new Promise((resolve, reject) => {
      canvas.toBlob(blob => blob ? resolve(blob) : reject(new Error('无法生成导出图像')), type, quality);
    });
  }

  async function createPdf(document, options = {}) {
    if (!global.PDFLib?.PDFDocument) throw new Error('PDF 导出模块未加载');
    const pages = Array.isArray(document?.pages) ? document.pages : [];
    if (!pages.length) throw new Error('当前笔记没有可导出的页面');
    const pdf = await global.PDFLib.PDFDocument.create();
    pdf.setTitle(String(document.title || 'QuizApp 笔记'));
    pdf.setAuthor('QuizApp');
    pdf.setSubject('带手写批注的笔记导出');
    pdf.setCreator('QuizApp Notebook');
    pdf.setProducer('pdf-lib 1.17.1');
    pdf.setCreationDate(new Date());
    pdf.setModificationDate(new Date());
    progress(options.onProgress, 'prepare', 2, '正在准备笔记页面', { completed: 0, total: pages.length });
    for (let index = 0; index < pages.length; index += 1) {
      const page = pages[index];
      const percentStart = 4 + index / pages.length * 86;
      progress(options.onProgress, 'render', percentStart, `正在合成第 ${index + 1} 页`, { completed: index, total: pages.length });
      const canvas = await renderPage(page, options);
      const blob = await canvasBlob(canvas, 'image/jpeg', clamp(options.quality || .94, .72, 1));
      const image = await pdf.embedJpg(await blob.arrayBuffer());
      const width = Math.max(1, Number(page.width || 1200)) * PDF_POINT_SCALE;
      const height = Math.max(1, Number(page.height || 1600)) * PDF_POINT_SCALE;
      const pdfPage = pdf.addPage([width, height]);
      pdfPage.drawImage(image, { x: 0, y: 0, width, height });
      canvas.width = 1;
      canvas.height = 1;
      progress(options.onProgress, 'render', 4 + (index + 1) / pages.length * 86, `已合成 ${index + 1}/${pages.length} 页`, { completed: index + 1, total: pages.length });
      await nextFrame();
    }
    progress(options.onProgress, 'encode', 94, '正在生成 PDF 文件', { completed: pages.length, total: pages.length });
    const bytes = await pdf.save({ useObjectStreams: true, addDefaultPage: false });
    progress(options.onProgress, 'complete', 100, 'PDF 已生成', { completed: pages.length, total: pages.length });
    return {
      bytes,
      pageCount: pages.length,
      filename: `${safeFilename(document.title)}-手写批注.pdf`,
    };
  }

  function download(bytes, filename, mimeType = 'application/pdf') {
    const blob = bytes instanceof Blob ? bytes : new Blob([bytes], { type: mimeType });
    const url = URL.createObjectURL(blob);
    const link = global.document.createElement('a');
    link.href = url;
    link.download = filename || 'QuizApp-notebook.pdf';
    global.document.body.appendChild(link);
    link.click();
    link.remove();
    global.setTimeout(() => URL.revokeObjectURL(url), 1500);
  }

  global.QuizNotebookExport = {
    PDF_POINT_SCALE,
    safeFilename,
    renderPage,
    canvasBlob,
    createPdf,
    download,
  };
})(window);
