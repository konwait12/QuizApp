/* QuizApp notebook OCR UI and persistence bridge. GPL-3.0-or-later. */
(function attachQuizNotebookOcrUi(global) {
  'use strict';

  let consentResolver = null;

  function getService() {
    if (!state.notebookOcrService && global.QuizNotebookOcr) state.notebookOcrService = new QuizNotebookOcr.OcrService();
    return state.notebookOcrService;
  }

  function loadConfig() {
    let stored = null;
    try { stored = JSON.parse(localStorage.getItem(OCR_CONFIG_KEY) || 'null'); } catch(e) {}
    state.ocrConfig = global.QuizNotebookOcr?.normalizeConfig(stored || state.ocrConfig) || state.ocrConfig;
    return state.ocrConfig;
  }

  function saveConfig() {
    state.ocrConfig = global.QuizNotebookOcr?.normalizeConfig(state.ocrConfig) || state.ocrConfig;
    localStorage.setItem(OCR_CONFIG_KEY, JSON.stringify(state.ocrConfig));
    state.notebookOcrService?.terminate?.();
  }

  function readSettingsForm() {
    const language = document.getElementById('ocrLanguage')?.value || state.ocrConfig.language;
    const pageSegMode = document.getElementById('ocrPageSegMode')?.value || state.ocrConfig.pageSegMode;
    const modelSource = document.getElementById('ocrModelSource')?.value === 'custom' ? 'custom' : 'auto';
    const customLangPath = String(document.getElementById('ocrCustomLangPath')?.value || '').trim().replace(/\/$/, '');
    if (modelSource === 'custom' && !/^https:\/\//i.test(customLangPath)) {
      showNotice('自定义 OCR 模型目录必须是 HTTPS 地址');
      return false;
    }
    state.ocrConfig = QuizNotebookOcr.normalizeConfig({
      ...state.ocrConfig,
      language,
      pageSegMode,
      modelSource,
      customLangPath,
    });
    return true;
  }

  function toggleCustomSource(value) {
    const row = document.getElementById('ocrCustomSourceRow');
    if (row) row.hidden = value !== 'custom';
  }

  function languageLabel(value) {
    return QuizNotebookOcr?.LANGUAGES?.find(item => item.value === value)?.label || value || '未知语言';
  }

  function renderSettingsPanel() {
    if (!global.QuizNotebookOcr) return '';
    const config = QuizNotebookOcr.normalizeConfig(state.ocrConfig);
    return `
      <div class="analysis-panel">
        <div class="section-title">笔记与 OCR 识别</div>
        <div class="settings-notice">OCR 在当前设备处理图像，不会改写题库题干、PDF 原文或内置解析。语言模型只在第一次使用时按需下载并缓存。</div>
        <div class="settings-row">
          <label for="ocrLanguage">默认识别语言</label>
          <select id="ocrLanguage">${QuizNotebookOcr.LANGUAGES.map(item => `<option value="${escapeAttr(item.value)}" ${config.language === item.value ? 'selected' : ''}>${escapeHtml(item.label)}</option>`).join('')}</select>
        </div>
        <div class="settings-row">
          <label for="ocrPageSegMode">页面类型</label>
          <select id="ocrPageSegMode">${QuizNotebookOcr.PAGE_SEG_MODES.map(item => `<option value="${escapeAttr(item.value)}" ${config.pageSegMode === item.value ? 'selected' : ''}>${escapeHtml(item.label)}</option>`).join('')}</select>
        </div>
        <div class="settings-row">
          <label for="ocrModelSource">语言模型来源</label>
          <select id="ocrModelSource" onchange="toggleNotebookOcrCustomSource(this.value)">
            <option value="auto" ${config.modelSource === 'auto' ? 'selected' : ''}>Tesseract.js 默认 CDN</option>
            <option value="custom" ${config.modelSource === 'custom' ? 'selected' : ''}>自定义 HTTPS 镜像</option>
          </select>
        </div>
        <div class="settings-row" id="ocrCustomSourceRow" ${config.modelSource === 'custom' ? '' : 'hidden'}>
          <label for="ocrCustomLangPath">模型目录</label>
          <input id="ocrCustomLangPath" type="url" value="${escapeAttr(config.customLangPath)}" placeholder="https://example.com/tessdata">
          <small>目录中需要包含所选语言的 <code>*.traineddata.gz</code> 文件。</small>
        </div>
        <div class="storage-path-card">
          <strong>OCR 模型缓存</strong>
          <small id="ocrCacheStatus">正在读取缓存…</small>
          <div class="settings-actions"><button class="secondary" onclick="clearNotebookOcrCache()">清除已下载模型</button></div>
        </div>
      </div>`;
  }

  async function refreshCacheStatus() {
    const target = document.getElementById('ocrCacheStatus');
    if (!target || !global.QuizNotebookOcr) return;
    try {
      const keys = await QuizNotebookOcr.listCachedModels();
      const languages = keys.map(key => key.split('/').pop().replace(/\.traineddata$/, '')).filter(Boolean);
      target.textContent = languages.length ? `已缓存：${languages.join('、')}` : '尚未下载语言模型';
    } catch(error) {
      target.textContent = `缓存读取失败：${error.message || error}`;
    }
  }

  function clearCache() {
    askConfirm('清除已下载的 OCR 语言模型？下次识别时需要重新下载。', async () => {
      try {
        const count = await QuizNotebookOcr.clearCachedModels(getService());
        await refreshCacheStatus();
        showToast(count ? `已清除 ${count} 个 OCR 模型` : '当前没有 OCR 模型缓存');
      } catch(error) {
        showNotice(`清除 OCR 缓存失败：${error.message || error}`);
      }
    });
  }

  function requestConsent() {
    if (state.ocrConfig.confirmedModelDownload) return Promise.resolve(true);
    closeAppDialog();
    return new Promise(resolve => {
      consentResolver = resolve;
      const mask = document.createElement('div');
      mask.className = 'app-dialog-mask';
      mask.innerHTML = `
        <div class="app-dialog" role="dialog" aria-modal="true" aria-label="首次使用 OCR">
          <h3>首次使用 OCR</h3>
          <p>应用内已包含 OCR 引擎，但不包含语言模型。继续后会按设置下载 ${escapeHtml(languageLabel(state.ocrConfig.language))} 模型，并缓存在当前设备。</p>
          <div class="settings-notice">图像在本机 Tesseract.js 中识别；应用不会把图像发送给 AI 或 QuizApp 服务器。</div>
          <div class="app-dialog-actions"><button class="secondary" onclick="resolveNotebookOcrConsent(false)">取消</button><button onclick="resolveNotebookOcrConsent(true)">继续</button></div>
        </div>`;
      mask.onclick = event => { if (event.target === mask) resolveConsent(false); };
      document.body.appendChild(mask);
    });
  }

  function resolveConsent(accepted) {
    const resolver = consentResolver;
    consentResolver = null;
    if (accepted) {
      state.ocrConfig.confirmedModelDownload = true;
      saveConfig();
    }
    closeAppDialog();
    resolver?.(Boolean(accepted));
  }

  async function showSourceDialog() {
    if (!global.QuizNotebookOcr || !global.Tesseract || !state.notebookSession) return showNotice('OCR 模块未加载，请重新打开应用');
    if (!await requestConsent()) return;
    closeAppDialog();
    const page = state.notebookSession.page;
    const question = getCurrentNotebookQuestion();
    const questionImage = normalizeImageList(question?.questionImages)[0] || '';
    const hasPdfBackground = Boolean(page.background?.type === 'pdf' && page.background?.assetId);
    const mask = document.createElement('div');
    mask.className = 'app-dialog-mask';
    mask.innerHTML = `
      <div class="app-dialog" role="dialog" aria-modal="true" aria-label="OCR 识别来源">
        <h3>OCR 识别</h3>
        <p>${escapeHtml(languageLabel(state.ocrConfig.language))} · ${escapeHtml(QuizNotebookOcr.PAGE_SEG_MODES.find(item => item.value === state.ocrConfig.pageSegMode)?.label || '自动版面')}</p>
        <div class="notebook-ocr-source-grid">
          <button onclick="startNotebookOcr('page')"><strong>当前笔记页</strong><small>包含底图、笔迹和可见对象</small></button>
          <button onclick="startNotebookOcr('background')" ${hasPdfBackground ? '' : 'disabled'}><strong>当前 PDF 底图</strong><small>${hasPdfBackground ? '不包含上层手写笔迹' : '当前页不是 PDF 底图'}</small></button>
          <button onclick="startNotebookOcr('question')" ${questionImage ? '' : 'disabled'}><strong>当前题目图片</strong><small>${questionImage ? '识别题目原图' : '当前题目没有图片'}</small></button>
          <button onclick="chooseNotebookOcrImage()"><strong>选择图片</strong><small>从设备文件中选择</small></button>
        </div>
        <div class="app-dialog-actions single"><button class="secondary" onclick="closeAppDialog()">取消</button></div>
      </div>`;
    mask.onclick = event => { if (event.target === mask) closeAppDialog(); };
    document.body.appendChild(mask);
  }

  function chooseImage() {
    closeAppDialog();
    document.getElementById('notebookOcrInput')?.click();
  }

  async function prepareImage(input) {
    const file = input?.files?.[0];
    if (input) input.value = '';
    if (!file) return;
    if (!await requestConsent()) return;
    await run(file, file.name || '选择的图片', 'image');
  }

  async function start(sourceType) {
    if (!await requestConsent()) return;
    let source;
    let label;
    if (sourceType === 'background') {
      source = await getNotebookAssetDataUrl(state.notebookSession?.page?.background?.assetId || '');
      label = '当前 PDF 底图';
    } else if (sourceType === 'question') {
      source = normalizeImageList(getCurrentNotebookQuestion()?.questionImages)[0] || '';
      label = '当前题目图片';
    } else {
      closeAppDialog();
      showProgress('当前笔记页', 2, '正在合成当前页');
      await saveCurrentInkNote({ silent: true, throwOnError: true });
      source = await QuizNotebookExport.renderPage(state.notebookSession.page, {
        resolveAsset: getNotebookAssetDataUrl,
        getStroke: global.PerfectFreehand?.getStroke,
      });
      label = '当前笔记页';
    }
    if (!source) return showNotice('没有可识别的图像');
    await run(source, label, sourceType);
  }

  function showProgress(label, percent = 0, message = '正在准备') {
    closeAppDialog();
    const mask = document.createElement('div');
    mask.className = 'app-dialog-mask';
    mask.innerHTML = `
      <div class="app-dialog" role="dialog" aria-modal="true" aria-label="OCR 识别进度">
        <h3>OCR 识别</h3><p>${escapeHtml(label)}</p>
        <div class="backup-operation"><div class="backup-progress-track"><div class="backup-progress-fill" id="notebookOcrProgressFill" style="width:${Math.max(0, Math.min(100, percent))}%"></div></div><div class="backup-progress-label" id="notebookOcrProgressLabel">${escapeHtml(message)}</div></div>
        <div class="app-dialog-actions single"><button class="secondary" onclick="cancelNotebookOcr()">取消识别</button></div>
      </div>`;
    document.body.appendChild(mask);
  }

  function updateProgress(event) {
    const fill = document.getElementById('notebookOcrProgressFill');
    const label = document.getElementById('notebookOcrProgressLabel');
    const percent = Math.max(0, Math.min(100, Number(event?.percent || 0)));
    if (fill) fill.style.width = `${percent}%`;
    if (label) label.textContent = event?.message || `正在识别 ${Math.round(percent)}%`;
  }

  async function run(source, label, sourceType) {
    showProgress(label, 3, '正在启动本机 OCR');
    const pending = { source, label, sourceType, cancelled: false };
    state.pendingNotebookOcr = pending;
    try {
      const result = await getService().recognize(source, state.ocrConfig, { onProgress: updateProgress });
      if (pending.cancelled || state.pendingNotebookOcr !== pending) return;
      if (!result.text) throw new Error('未识别到可保存的文字');
      state.pendingNotebookOcr = { ...pending, result };
      showResult();
    } catch(error) {
      if (pending.cancelled || String(error?.message || error).includes('已取消')) return;
      state.pendingNotebookOcr = null;
      closeAppDialog();
      showNotice(`OCR 识别失败：${error.message || error}`);
    } finally {
      if (source instanceof HTMLCanvasElement) {
        source.width = 1;
        source.height = 1;
      }
    }
  }

  async function cancel() {
    if (state.pendingNotebookOcr) state.pendingNotebookOcr.cancelled = true;
    await getService()?.cancel?.();
    state.pendingNotebookOcr = null;
    closeAppDialog();
    showToast('OCR 识别已取消');
  }

  function showResult() {
    const pending = state.pendingNotebookOcr;
    const result = pending?.result;
    if (!result) return;
    closeAppDialog();
    state.pendingNotebookOcr = pending;
    const mask = document.createElement('div');
    mask.className = 'app-dialog-mask';
    mask.innerHTML = `
      <div class="app-dialog notebook-ocr-result-dialog" role="dialog" aria-modal="true" aria-label="OCR 识别结果">
        <h3>OCR 识别结果</h3>
        <div class="notebook-ocr-meta"><span>${escapeHtml(languageLabel(result.language))}</span><span>置信度 ${Math.round(result.confidence || 0)}%</span><span>${escapeHtml(pending.label)}</span></div>
        <textarea id="notebookOcrResultText" spellcheck="false">${escapeHtml(result.text)}</textarea>
        <div class="settings-notice">识别文本可以先修改再保存。保存后会进入当前页全文索引，不替换原图或题库内容。</div>
        <div class="notebook-ocr-result-actions">
          <button class="secondary" onclick="discardNotebookOcrResult()">取消</button>
          <button class="secondary" onclick="saveNotebookOcrResult(false)">仅保存文本</button>
          <button onclick="saveNotebookOcrResult(true)">保存并插入</button>
        </div>
      </div>`;
    mask.onclick = event => { if (event.target === mask) discardResult(); };
    document.body.appendChild(mask);
  }

  function discardResult() {
    state.pendingNotebookOcr = null;
    closeAppDialog();
  }

  async function saveResult(insertObject) {
    const pending = state.pendingNotebookOcr;
    const result = pending?.result;
    const text = String(document.getElementById('notebookOcrResultText')?.value || '').trim();
    if (!result || !text || !state.notebookSession) return showToast('识别文本为空');
    const session = state.notebookSession;
    session.beginHistory();
    session.page.ocr = {
      text,
      language: result.language,
      confidence: Number(result.confidence || 0),
      source: pending.sourceType || 'image',
      createdAt: Date.now(),
    };
    if (insertObject) {
      const logicalLines = text.split('\n').reduce((sum, line) => sum + Math.max(1, Math.ceil(Array.from(line).length / 28)), 0);
      session.addObject('text', {
        text,
        fill: '#f7faf8',
        border: '#8fb9a6',
        color: '#202522',
        fontSize: 20,
        lineHeight: 30,
        ocr: { language: result.language, confidence: Number(result.confidence || 0), source: pending.sourceType || 'image' },
      }, { x: 90, y: 90, width: 720, height: Math.max(150, Math.min(1100, logicalLines * 30 + 40)) });
    } else session.commitHistory('save-ocr-text');
    state.pendingNotebookOcr = null;
    closeAppDialog();
    if (insertObject) setNotebookTool('select');
    state.notebookViewport?.requestRender();
    await saveCurrentInkNote({ silent: true, throwOnError: true });
    renderNotebookRightPanel();
    showToast(insertObject ? 'OCR 文本已保存并插入' : 'OCR 文本已保存');
  }

  function renderInfo() {
    const ocr = state.notebookSession?.page?.ocr;
    return `
      <div class="notebook-meta-section">
        <div class="notebook-meta-head"><strong>本页 OCR</strong>${ocr?.text ? '<button onclick="clearCurrentNotebookOcr()">删除</button>' : '<button onclick="showNotebookOcrSourceDialog()">识别</button>'}</div>
        ${ocr?.text ? `<div class="notebook-ocr-info"><small>${escapeHtml(languageLabel(ocr.language))} · 置信度 ${Math.round(ocr.confidence || 0)}% · ${escapeHtml(new Date(ocr.createdAt || Date.now()).toLocaleString('zh-CN', { hour12: false }))}</small><p>${escapeHtml(ocr.text.slice(0, 260))}${ocr.text.length > 260 ? '…' : ''}</p></div>` : '<span class="settings-subtitle">当前页还没有 OCR 文本</span>'}
      </div>`;
  }

  function clearCurrent() {
    if (!state.notebookSession?.page?.ocr) return;
    askConfirm('删除当前页保存的 OCR 文本？原图、笔迹和已插入的文本对象不会删除。', () => {
      state.notebookSession.beginHistory();
      state.notebookSession.page.ocr = null;
      state.notebookSession.commitHistory('remove-ocr-text');
      scheduleNotebookSave();
      renderNotebookRightPanel();
      showToast('OCR 文本已删除');
    });
  }

  global.loadOcrConfig = loadConfig;
  global.saveOcrConfig = saveConfig;
  global.readOcrSettingsForm = readSettingsForm;
  global.renderOcrSettingsPanel = renderSettingsPanel;
  global.refreshOcrCacheStatus = refreshCacheStatus;
  global.clearNotebookOcrCache = clearCache;
  global.toggleNotebookOcrCustomSource = toggleCustomSource;
  global.resolveNotebookOcrConsent = resolveConsent;
  global.showNotebookOcrSourceDialog = showSourceDialog;
  global.chooseNotebookOcrImage = chooseImage;
  global.prepareNotebookOcrImage = prepareImage;
  global.startNotebookOcr = start;
  global.cancelNotebookOcr = cancel;
  global.discardNotebookOcrResult = discardResult;
  global.saveNotebookOcrResult = saveResult;
  global.renderNotebookOcrInfo = renderInfo;
  global.clearCurrentNotebookOcr = clearCurrent;
})(window);
