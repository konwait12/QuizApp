/* QuizApp FSRS review user interface. GPL-3.0-or-later. */
(function attachReviewUi(global) {
  'use strict';

  function resolveQuestion(record) {
    const live = buildQuestionSet(getVisibleBanks()).find(question => questionKey(question) === record.key);
    if (live) return live;
    const fallback = normalizeQuestion(record.question || {}, 0);
    fallback.sourceSubject = record.question?.sourceSubject || '';
    fallback.sourceChapter = record.question?.sourceChapter || '';
    fallback.sourcePath = normalizePath(record.question?.sourcePath || [fallback.sourceSubject, fallback.sourceChapter]);
    return fallback;
  }

  function currentRecord() {
    return state.reviewQueueSession?.records?.[state.reviewQueueSession.index] || null;
  }

  function formatDue(value) {
    const due = new Date(value);
    const diff = due.getTime() - Date.now();
    if (diff <= 0) return '现在';
    const minutes = Math.max(1, Math.round(diff / 60000));
    if (minutes < 60) return `${minutes} 分钟`;
    const hours = Math.round(minutes / 60);
    if (hours < 24) return `${hours} 小时`;
    const days = Math.round(hours / 24);
    if (days < 31) return `${days} 天`;
    const months = Math.round(days / 30);
    if (months < 12) return `${months} 个月`;
    return `${Math.round(months / 12)} 年`;
  }

  function getRatingPreview(record) {
    const preview = QuizReview.preview(record.key, new Date());
    if (!preview) return [];
    return [
      { value: FSRS.Rating.Again, label: '忘记', className: 'again' },
      { value: FSRS.Rating.Hard, label: '困难', className: 'hard' },
      { value: FSRS.Rating.Good, label: '良好', className: 'good' },
      { value: FSRS.Rating.Easy, label: '简单', className: 'easy' },
    ].map(item => ({ ...item, due: preview[item.value]?.card?.due }));
  }

  function renderEmpty(stats) {
    const all = QuizReview.all();
    return `
      <div class="review-summary-grid">
        <div><strong>${stats.total}</strong><small>复习卡</small></div>
        <div><strong>${stats.due}</strong><small>今日到期</small></div>
        <div><strong>${stats.learned}</strong><small>已学习</small></div>
      </div>
      <div class="review-empty">
        <strong>${stats.total ? '今天的复习已完成' : '还没有复习卡'}</strong>
        <p>${stats.total ? '可以查看全部卡片，或返回刷题继续添加。' : '在任意题目的“加入复习”按钮处创建第一张卡片。'}</p>
      </div>
      ${all.length ? `<div class="review-card-list">${all.map(record => {
        const question = resolveQuestion(record);
        return `<div class="review-list-item"><div><strong>${escapeHtml(question.q || '题目')}</strong><small>${escapeHtml(pathCrumbs(question.sourcePath || [question.sourceSubject, question.sourceChapter]))} · ${formatDue(record.card.due)}</small></div><button onclick="removeReviewCardByKey('${escapeAttr(record.key)}')">删除</button></div>`;
      }).join('')}</div>` : ''}
    `;
  }

  function renderActive(record, stats) {
    const question = resolveQuestion(record);
    const session = state.reviewQueueSession;
    const revealed = Boolean(session.revealed);
    const options = renderInkQuestionOptions(question);
    const ratings = revealed ? getRatingPreview(record) : [];
    return `
      <div class="review-progress-head">
        <span>待复习 ${session.records.length - session.index} · 全部 ${stats.total}</span>
        <span>${session.index + 1}/${session.records.length}</span>
      </div>
      <div class="review-progress-track"><span style="width:${Math.round((session.index / Math.max(1, session.records.length)) * 100)}%"></span></div>
      <article class="review-study-card">
        <div class="review-source">${escapeHtml(pathCrumbs(question.sourcePath || [question.sourceSubject, question.sourceChapter]))} · ${escapeHtml(getTypeLabel(getType(question)))}</div>
        <h2>${escapeHtml(question.q || '图片题')}</h2>
        ${renderQuestionMedia(question)}
        ${options}
        ${revealed ? `<div class="review-answer">${renderBuiltinExplanation(question)}</div>` : '<button class="review-reveal" onclick="revealReviewCard()">显示答案与解析</button>'}
      </article>
      ${revealed ? `<div class="review-rating-grid">${ratings.map(item => `<button class="${item.className}" onclick="rateReviewCard(${item.value})"><strong>${item.label}</strong><small>${formatDue(item.due)}</small></button>`).join('')}</div>` : ''}
    `;
  }

  global.openReviewQueue = function openReviewQueue(options = {}) {
    if (!global.QuizReview) return showNotice('复习调度器未加载');
    if (!options.restoreRoute) pushRoute();
    const records = QuizReview.due(new Date());
    state.reviewQueueSession = { records, index: 0, revealed: false };
    state.view = 'reviewQueue';
    renderReviewQueue();
  };

  global.renderReviewQueue = function renderReviewQueue() {
    state.view = 'reviewQueue';
    const stats = QuizReview.stats();
    const record = currentRecord();
    const app = document.getElementById('app');
    app.innerHTML = `
      <div class="topbar">
        <button class="btn-back" onclick="goBackRoute()" aria-label="返回">←</button>
        <h1>复习队列</h1>
        <div class="topbar-actions"><span class="topbar-meta">FSRS · ${stats.due} 到期</span></div>
      </div>
      <div class="viewport view-scroll"><div class="home review-queue-page">${record ? renderActive(record, stats) : renderEmpty(stats)}</div></div>
    `;
  };

  global.revealReviewCard = function revealReviewCard() {
    if (!state.reviewQueueSession) return;
    state.reviewQueueSession.revealed = true;
    renderReviewQueue();
  };

  global.rateReviewCard = function rateReviewCard(rating) {
    const record = currentRecord();
    if (!record) return;
    QuizReview.rate(record.key, Number(rating), new Date());
    state.reviewQueueSession.index += 1;
    state.reviewQueueSession.revealed = false;
    renderReviewQueue();
  };

  global.toggleQuestionReviewCard = function toggleQuestionReviewCard(index) {
    const question = state.activeSession?.questions?.[index];
    if (!question) return;
    const key = questionKey(question);
    if (QuizReview.has(key)) {
      QuizReview.remove(key);
      showToast('已从复习队列删除');
    } else {
      QuizReview.add(key, question);
      showToast('已加入复习队列');
    }
    renderQuizPreservingCard(index);
  };

  global.removeReviewCardByKey = function removeReviewCardByKey(key) {
    if (QuizReview.remove(key)) {
      showToast('复习卡已删除');
      renderReviewQueue();
    }
  };
})(window);
