/* Markdown and KaTeX notebook object rendering. GPL-3.0-or-later. */
(function attachQuizNotebookRichObjects(global) {
  'use strict';

  function nextFrame() {
    return new Promise(resolve => global.requestAnimationFrame(() => resolve()));
  }

  function escapeRawHtml(value) {
    return String(value || '').replace(/</g, '&lt;').replace(/>/g, '&gt;');
  }

  function secureRenderedLinks(root) {
    root.querySelectorAll('a').forEach(anchor => {
      const href = String(anchor.getAttribute('href') || '').trim();
      if (!/^https:\/\//i.test(href)) anchor.removeAttribute('href');
      else {
        anchor.target = '_blank';
        anchor.rel = 'noopener noreferrer';
      }
    });
    root.querySelectorAll('img,iframe,video,audio,script,style').forEach(element => element.remove());
  }

  function renderMarkdown(container, source) {
    if (!container) return;
    if (!global.marked?.parse) {
      container.textContent = String(source || '');
      return;
    }
    const safeSource = escapeRawHtml(source);
    container.innerHTML = global.marked.parse(safeSource, { gfm: true, breaks: true, async: false });
    secureRenderedLinks(container);
  }

  function renderFormula(container, source) {
    if (!container) return;
    container.textContent = '';
    if (!global.katex?.render) {
      container.textContent = String(source || '');
      return;
    }
    global.katex.render(String(source || ''), container, {
      displayMode: true,
      throwOnError: false,
      strict: 'ignore',
      trust: false,
      output: 'htmlAndMathml',
    });
  }

  function render(container, type, source) {
    if (type === 'formula') renderFormula(container, source);
    else renderMarkdown(container, source);
  }

  async function capture(type, source, options = {}) {
    if (!global.html2canvas) throw new Error('富文本图像模块未加载');
    const value = String(source || '').trim();
    if (!value) throw new Error(type === 'formula' ? '请输入 LaTeX 公式' : '请输入 Markdown 内容');
    const host = global.document.createElement('div');
    host.className = `notebook-rich-capture notebook-rich-capture-${type === 'formula' ? 'formula' : 'markdown'}`;
    host.style.width = `${Math.max(280, Math.min(760, Number(options.width || (type === 'formula' ? 620 : 680))))}px`;
    host.setAttribute('aria-hidden', 'true');
    global.document.body.appendChild(host);
    try {
      render(host, type, value);
      if (global.document.fonts?.ready) await global.document.fonts.ready;
      await nextFrame();
      await nextFrame();
      const width = Math.ceil(host.getBoundingClientRect().width);
      const height = Math.max(80, Math.ceil(host.scrollHeight));
      if (height > 1500) throw new Error('内容过长，请分成多个对象插入');
      const canvas = await global.html2canvas(host, {
        backgroundColor: null,
        scale: Math.max(1, Math.min(2.5, Number(options.scale || 2))),
        useCORS: true,
        logging: false,
        width,
        height,
        windowWidth: Math.max(global.innerWidth || 0, width + 40),
      });
      const dataUrl = canvas.toDataURL('image/png');
      canvas.width = 1;
      canvas.height = 1;
      return { dataUrl, width, height };
    } finally {
      host.remove();
    }
  }

  function sourceForObject(object) {
    if (object?.type === 'formula') return String(object.data?.latex || object.data?.text || '');
    if (object?.type === 'markdown') return String(object.data?.markdown || object.data?.text || '');
    return '';
  }

  global.QuizNotebookRichObjects = {
    render,
    renderMarkdown,
    renderFormula,
    capture,
    sourceForObject,
  };
})(window);
