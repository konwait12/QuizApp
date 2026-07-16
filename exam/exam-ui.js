/* QuizApp objective exam mode. GPL-3.0-or-later. */
(function attachExamUi(global) {
  'use strict';

  const ACTIVE_KEY = 'quizapp_active_exam';
  const HISTORY_KEY = 'quizapp_exam_history';
  let timer = 0;

  function loadHistory() {
    try {
      const parsed = JSON.parse(localStorage.getItem(HISTORY_KEY) || '[]');
      return Array.isArray(parsed) ? parsed : [];
    } catch(e) {
      return [];
    }
  }

  function saveHistory(history) {
    localStorage.setItem(HISTORY_KEY, JSON.stringify(history.slice(0, 30)));
  }

  function snapshotQuestion(question) {
    const builtin = question.explanations?.builtin;
    return {
      id: question.id || '',
      type: getType(question),
      q: String(question.q || ''),
      options: getOptions(question).slice(0, 12),
      ans: getCorrectAnswer(question),
      exp: String(builtin?.text || question.exp || '').slice(0, 6000),
      sourceSubject: question.sourceSubject || '',
      sourceChapter: question.sourceChapter || '',
      sourcePath: normalizePath(question.sourcePath || [question.sourceSubject, question.sourceChapter]),
    };
  }

  function resolveHistoryQuestion(item, liveMap) {
    const live = liveMap.get(item.key);
    if (live) return live;
    const fallback = normalizeQuestion(item.question || {}, Number(item.index || 0));
    fallback.sourceSubject = item.question?.sourceSubject || '';
    fallback.sourceChapter = item.question?.sourceChapter || '';
    fallback.sourcePath = normalizePath(item.question?.sourcePath || [fallback.sourceSubject, fallback.sourceChapter]);
    return fallback;
  }

  function historyItems(result) {
    if (Array.isArray(result.items) && result.items.length) return result.items;
    return (result.wrongItems || []).map(item => ({
      ...item,
      correct: false,
      unanswered: !item.answer,
    }));
  }

  function objectiveQuestions(subject = '') {
    const banks = subject ? getBanksBySubject(subject) : getVisibleBanks();
    return buildQuestionSet(banks).filter(question => ['single', 'multi', 'bool'].includes(getType(question)));
  }

  function sampleQuestions(questions, count) {
    const copy = questions.slice();
    for (let index = copy.length - 1; index > 0; index -= 1) {
      const target = Math.floor(Math.random() * (index + 1));
      [copy[index], copy[target]] = [copy[target], copy[index]];
    }
    return copy.slice(0, Math.min(count, copy.length));
  }

  function formatClock(seconds) {
    const safe = Math.max(0, Number(seconds || 0));
    const minutes = Math.floor(safe / 60);
    const rest = safe % 60;
    return `${String(minutes).padStart(2, '0')}:${String(rest).padStart(2, '0')}`;
  }

  function saveActiveExam() {
    const session = state.examSession;
    if (!session || session.submitted) return localStorage.removeItem(ACTIVE_KEY);
    localStorage.setItem(ACTIVE_KEY, JSON.stringify({
      id: session.id,
      title: session.title,
      subject: session.subject,
      questionKeys: session.questions.map(questionKey),
      answers: session.answers,
      currentIndex: session.currentIndex,
      durationSeconds: session.durationSeconds,
      remainingSeconds: session.remainingSeconds,
      paused: session.paused,
      startedAt: session.startedAt,
      updatedAt: Date.now(),
    }));
  }

  function clearExamTimer() {
    window.clearInterval(timer);
    timer = 0;
  }

  function startExamTimer() {
    clearExamTimer();
    timer = window.setInterval(() => {
      const session = state.examSession;
      if (!session || session.submitted || session.paused || state.view !== 'exam') return;
      session.remainingSeconds = Math.max(0, session.remainingSeconds - 1);
      const label = document.getElementById('examTimer');
      if (label) label.textContent = formatClock(session.remainingSeconds);
      if (session.remainingSeconds % 10 === 0) saveActiveExam();
      if (session.remainingSeconds <= 0) submitExam(true);
    }, 1000);
  }

  function renderSetup() {
    state.view = 'examSetup';
    const subjects = getSubjectGroups();
    const active = localStorage.getItem(ACTIVE_KEY);
    const app = document.getElementById('app');
    app.innerHTML = `
      <div class="topbar"><button class="btn-back" onclick="goBackRoute()" aria-label="返回">←</button><h1>模拟考试</h1></div>
      <div class="viewport view-scroll"><div class="home exam-setup-page">
        ${active ? '<button class="exam-resume" onclick="resumeActiveExam()"><strong>继续未完成考试</strong><small>恢复剩余时间、题号和已选答案</small><span>›</span></button>' : ''}
        <div class="analysis-panel">
          <div class="section-title">考试范围</div>
          <label class="exam-field"><span>科目</span><select id="examSubject"><option value="">全部科目</option>${subjects.map(group => `<option value="${escapeAttr(group.subject)}">${escapeHtml(group.subject)} · ${group.questionCount} 题</option>`).join('')}</select></label>
          <label class="exam-field"><span>题量</span><select id="examCount"><option value="10">10 题</option><option value="20" selected>20 题</option><option value="50">50 题</option><option value="100">100 题</option></select></label>
          <label class="exam-field"><span>时长</span><select id="examDuration"><option value="10">10 分钟</option><option value="30" selected>30 分钟</option><option value="60">60 分钟</option><option value="120">120 分钟</option></select></label>
          <div class="settings-notice">模拟考试只抽取可自动判分的单选、多选和判断题。作答过程中不显示正确答案，交卷后统一评分。</div>
          <button class="btn-submit exam-start" onclick="startConfiguredExam()">开始考试</button>
        </div>
        ${renderExamHistory()}
      </div></div>`;
  }

  function renderExamHistory() {
    const history = loadHistory();
    if (!history.length) return '';
    return `<div class="analysis-panel"><div class="section-title">最近考试</div><div class="exam-history-list">${history.slice(0, 10).map(item => `
      <button onclick="openExamHistoryDetail('${escapeAttr(item.id)}')"><span><strong>${escapeHtml(item.title)}</strong><small>${new Date(item.submittedAt).toLocaleString()} · ${item.correct}/${item.total} 题</small></span><b>${item.score}</b><i>›</i></button>
    `).join('')}</div></div>`;
  }

  function renderHistoryDetail(result) {
    state.view = 'examHistory';
    const liveMap = new Map(buildQuestionSet(getVisibleBanks()).map(question => [questionKey(question), question]));
    const items = historyItems(result).map(item => ({ ...item, question: resolveHistoryQuestion(item, liveMap) }));
    const filter = state.examHistoryFilter === 'wrong' ? 'wrong' : 'all';
    const visible = filter === 'wrong' ? items.filter(item => !item.correct) : items;
    const legacy = !Array.isArray(result.items);
    const app = document.getElementById('app');
    app.innerHTML = `
      <div class="topbar"><button class="btn-back" onclick="goBackRoute()" aria-label="返回">←</button><h1>考试详情</h1></div>
      <div class="viewport view-scroll"><div class="home exam-result-page exam-history-page">
        <div class="exam-score"><strong>${result.score}</strong><span>分</span><small>${escapeHtml(result.title)} · ${new Date(result.submittedAt).toLocaleString()}</small></div>
        <div class="review-summary-grid"><div><strong>${result.correct}</strong><small>正确</small></div><div><strong>${result.wrong}</strong><small>错误</small></div><div><strong>${result.unanswered}</strong><small>未答</small></div></div>
        <div class="exam-history-meta"><span>用时 ${formatClock(result.durationUsed || 0)}</span><span>${result.timedOut ? '时间到自动交卷' : '主动交卷'}</span></div>
        <div class="exam-history-tabs"><button class="${filter === 'all' ? 'active' : ''}" onclick="setExamHistoryFilter('all')">全部题目</button><button class="${filter === 'wrong' ? 'active' : ''}" onclick="setExamHistoryFilter('wrong')">错题与未答</button></div>
        ${legacy ? '<div class="settings-notice">这是旧版考试记录，当时只保存了错题；新考试会保留全部题目和作答详情。</div>' : ''}
        <div class="exam-result-list">${visible.length ? visible.map(item => {
          const status = item.correct ? 'correct' : item.unanswered ? 'unanswered' : 'wrong';
          const statusLabel = item.correct ? '正确' : item.unanswered ? '未答' : '错误';
          return `<details class="${status}"><summary><span>${Number(item.index || 0) + 1}. ${escapeHtml(item.question.q || '图片题')}</span><b>${statusLabel}</b></summary><div><p>你的答案：<strong>${escapeHtml(item.answer || '未答')}</strong> · 正确答案：<strong>${escapeHtml(getCorrectAnswer(item.question))}</strong></p>${renderBuiltinExplanation(item.question)}</div></details>`;
        }).join('') : '<div class="review-empty"><strong>没有符合条件的题目</strong></div>'}</div>
      </div></div>`;
  }

  function renderExam() {
    const session = state.examSession;
    if (!session) return renderSetup();
    state.view = 'exam';
    const question = session.questions[session.currentIndex];
    const selected = String(session.answers[session.currentIndex] || '');
    const options = getOptions(question);
    const type = getType(question);
    const letters = 'ABCDEFGH';
    const answeredCount = Object.values(session.answers).filter(Boolean).length;
    const app = document.getElementById('app');
    app.innerHTML = `
      <div class="topbar exam-topbar">
        <button class="btn-back" onclick="requestExitExam()" aria-label="返回">←</button>
        <h1>${escapeHtml(session.title)}</h1>
        <div class="topbar-actions"><button class="btn-text" onclick="toggleExamPause()">${session.paused ? '继续' : '暂停'}</button><span class="exam-timer" id="examTimer">${formatClock(session.remainingSeconds)}</span><button class="btn-text" onclick="requestSubmitExam()">交卷</button></div>
      </div>
      <div class="exam-progress"><span style="width:${Math.round((answeredCount / session.questions.length) * 100)}%"></span></div>
      <div class="viewport view-scroll"><div class="home exam-page">
        <article class="exam-question-card ${session.paused ? 'paused' : ''}">
          <div class="exam-question-meta"><span>第 ${session.currentIndex + 1}/${session.questions.length} 题</span><span>${escapeHtml(getTypeLabel(type))} · 已答 ${answeredCount}</span></div>
          <h2>${escapeHtml(question.q)}</h2>
          ${renderQuestionMedia(question)}
          <div class="exam-options">${options.map((option, index) => {
            const letter = letters[index];
            return `<button class="${selected.includes(letter) ? 'selected' : ''}" onclick="answerExamQuestion('${letter}')"><span>${letter}</span><b>${escapeHtml(option)}</b></button>`;
          }).join('')}</div>
          ${session.paused ? '<div class="exam-pause-cover"><strong>考试已暂停</strong><button onclick="toggleExamPause()">继续考试</button></div>' : ''}
        </article>
      </div></div>
      <div class="bottombar exam-bottombar"><button onclick="changeExamQuestion(-1)" ${session.currentIndex <= 0 ? 'disabled' : ''}>上一题</button><button onclick="showExamOverview()">题号总览</button><button onclick="changeExamQuestion(1)" ${session.currentIndex >= session.questions.length - 1 ? 'disabled' : ''}>下一题</button></div>`;
    startExamTimer();
  }

  function renderResult(result) {
    state.view = 'examResult';
    const app = document.getElementById('app');
    app.innerHTML = `
      <div class="topbar"><button class="btn-back" onclick="finishExamResult()" aria-label="返回">←</button><h1>考试结果</h1></div>
      <div class="viewport view-scroll"><div class="home exam-result-page">
        <div class="exam-score"><strong>${result.score}</strong><span>分</span><small>${escapeHtml(result.title)}</small></div>
        <div class="review-summary-grid"><div><strong>${result.correct}</strong><small>正确</small></div><div><strong>${result.wrong}</strong><small>错误</small></div><div><strong>${result.unanswered}</strong><small>未答</small></div></div>
        ${result.wrongItems.length ? `<div class="analysis-panel"><div class="section-title">错题回顾</div><div class="exam-result-list">${result.wrongItems.map(item => `<details><summary><span>${item.index + 1}. ${escapeHtml(item.question.q)}</span><b>${item.answer || '未答'} / ${getCorrectAnswer(item.question)}</b></summary><div>${renderBuiltinExplanation(item.question)}</div></details>`).join('')}</div></div>` : '<div class="review-empty"><strong>全部答对</strong><p>本次考试没有错题。</p></div>'}
        <button class="btn-submit" onclick="finishExamResult()">返回学习页</button>
      </div></div>`;
  }

  global.openExamSetup = function openExamSetup(options = {}) {
    if (!options.restoreRoute) pushRoute();
    clearExamTimer();
    renderSetup();
  };

  global.startConfiguredExam = function startConfiguredExam() {
    const subject = document.getElementById('examSubject')?.value || '';
    const count = Number(document.getElementById('examCount')?.value || 20);
    const durationMinutes = Number(document.getElementById('examDuration')?.value || 30);
    const available = objectiveQuestions(subject);
    if (!available.length) return showNotice('当前范围没有可自动判分的题目');
    const questions = sampleQuestions(available, count);
    state.examSession = {
      id: `exam:${Date.now()}`,
      title: subject ? `${subject} 模拟考试` : '综合模拟考试',
      subject,
      questions,
      answers: {},
      currentIndex: 0,
      durationSeconds: durationMinutes * 60,
      remainingSeconds: durationMinutes * 60,
      paused: false,
      submitted: false,
      startedAt: Date.now(),
    };
    saveActiveExam();
    renderExam();
  };

  global.answerExamQuestion = function answerExamQuestion(letter) {
    const session = state.examSession;
    if (!session || session.paused) return;
    const index = session.currentIndex;
    const type = getType(session.questions[index]);
    if (type === 'multi') {
      const current = new Set(String(session.answers[index] || '').split('').filter(Boolean));
      current.has(letter) ? current.delete(letter) : current.add(letter);
      session.answers[index] = [...current].sort().join('');
    } else {
      session.answers[index] = session.answers[index] === letter ? '' : letter;
    }
    saveActiveExam();
    renderExam();
  };

  global.changeExamQuestion = function changeExamQuestion(delta) {
    const session = state.examSession;
    if (!session || session.paused) return;
    session.currentIndex = Math.max(0, Math.min(session.questions.length - 1, session.currentIndex + Number(delta || 0)));
    saveActiveExam();
    renderExam();
  };

  global.toggleExamPause = function toggleExamPause() {
    if (!state.examSession) return;
    state.examSession.paused = !state.examSession.paused;
    saveActiveExam();
    renderExam();
  };

  global.showExamOverview = function showExamOverview() {
    const session = state.examSession;
    if (!session) return;
    closeAppDialog();
    const mask = document.createElement('div');
    mask.className = 'app-dialog-mask';
    mask.innerHTML = `<div class="app-dialog exam-overview-dialog"><h3>题号总览</h3><div class="overview-grid">${session.questions.map((question, index) => `<button class="${session.answers[index] ? 'answered' : ''} ${index === session.currentIndex ? 'current' : ''}" onclick="jumpExamQuestion(${index})">${index + 1}</button>`).join('')}</div><div class="app-dialog-actions"><button onclick="closeAppDialog()">关闭</button></div></div>`;
    mask.onclick = event => { if (event.target === mask) closeAppDialog(); };
    document.body.appendChild(mask);
  };

  global.jumpExamQuestion = function jumpExamQuestion(index) {
    closeAppDialog();
    state.examSession.currentIndex = Math.max(0, Math.min(state.examSession.questions.length - 1, Number(index || 0)));
    saveActiveExam();
    renderExam();
  };

  global.requestSubmitExam = function requestSubmitExam() {
    const session = state.examSession;
    if (!session) return;
    const answered = Object.values(session.answers).filter(Boolean).length;
    askConfirm(`已答 ${answered}/${session.questions.length} 题，确认交卷？`, () => submitExam(false));
  };

  global.submitExam = function submitExam(timedOut = false) {
    const session = state.examSession;
    if (!session || session.submitted) return;
    clearExamTimer();
    session.submitted = true;
    const wrongItems = [];
    const items = [];
    let correct = 0;
    let unanswered = 0;
    session.questions.forEach((question, index) => {
      const answer = String(session.answers[index] || '');
      const isCorrect = Boolean(answer && checkAnswer(question, answer));
      if (!answer) unanswered += 1;
      if (isCorrect) correct += 1;
      else wrongItems.push({ index, question, answer });
      items.push({
        index,
        key: questionKey(question),
        answer,
        correct: isCorrect,
        unanswered: !answer,
        question: snapshotQuestion(question),
      });
    });
    const total = session.questions.length;
    const result = {
      id: session.id,
      title: session.title,
      total,
      correct,
      wrong: total - correct - unanswered,
      unanswered,
      score: Math.round((correct / total) * 100),
      timedOut,
      submittedAt: Date.now(),
      durationUsed: session.durationSeconds - session.remainingSeconds,
      wrongItems,
      items,
    };
    const historyEntry = {
      ...result,
      wrongItems: result.wrongItems.map(item => ({ index: item.index, key: questionKey(item.question), answer: item.answer })),
    };
    saveHistory([historyEntry, ...loadHistory()]);
    localStorage.removeItem(ACTIVE_KEY);
    renderResult(result);
  };

  global.requestExitExam = function requestExitExam() {
    if (!state.examSession || state.examSession.submitted) return goBackRoute();
    askConfirm('退出后会保留当前考试，可以下次继续。确认退出？', () => {
      saveActiveExam();
      clearExamTimer();
      state.examSession = null;
      goBackRoute();
    });
  };

  global.resumeActiveExam = function resumeActiveExam() {
    let saved;
    try { saved = JSON.parse(localStorage.getItem(ACTIVE_KEY) || 'null'); } catch(e) {}
    if (!saved?.questionKeys?.length) return showNotice('没有可恢复的考试');
    const map = new Map(buildQuestionSet(getVisibleBanks()).map(question => [questionKey(question), question]));
    const questions = saved.questionKeys.map(key => map.get(key)).filter(Boolean);
    if (questions.length !== saved.questionKeys.length) return showNotice('部分考试题目已不在当前题库，无法恢复');
    state.examSession = {
      ...saved,
      questions,
      currentIndex: Math.max(0, Math.min(questions.length - 1, Number(saved.currentIndex || 0))),
      remainingSeconds: Math.max(1, Number(saved.remainingSeconds || 1)),
      paused: Boolean(saved.paused),
      submitted: false,
    };
    renderExam();
  };

  global.openExamHistoryDetail = function openExamHistoryDetail(id, options = {}) {
    const result = loadHistory().find(item => item.id === id);
    if (!result) return showNotice('考试记录不存在或已被清理');
    if (!options.restoreRoute) pushRoute();
    state.examHistoryId = id;
    state.examHistoryFilter = 'all';
    renderHistoryDetail(result);
  };

  global.setExamHistoryFilter = function setExamHistoryFilter(filter) {
    const result = loadHistory().find(item => item.id === state.examHistoryId);
    if (!result) return renderSetup();
    state.examHistoryFilter = filter === 'wrong' ? 'wrong' : 'all';
    renderHistoryDetail(result);
  };

  global.finishExamResult = function finishExamResult() {
    state.examSession = null;
    state.mainTab = 'study';
    state.navStack = state.navStack.filter(route => route.view !== 'exam' && route.view !== 'examSetup');
    renderHome();
  };

  global.restoreExamRoute = function restoreExamRoute(route) {
    if (state.examSession && route.view === 'exam') return renderExam();
    if (route.view === 'examHistory' && route.examHistoryId) return openExamHistoryDetail(route.examHistoryId, { restoreRoute: true });
    return renderSetup();
  };

  global.QuizExam = {
    HISTORY_KEY,
    history: loadHistory,
    stats() {
      const history = loadHistory();
      return {
        total: history.length,
        latestScore: history[0]?.score ?? null,
        averageScore: history.length ? Math.round(history.reduce((sum, item) => sum + Number(item.score || 0), 0) / history.length) : null,
      };
    },
  };
})(window);
