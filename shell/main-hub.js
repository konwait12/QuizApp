/* QuizApp main information architecture. GPL-3.0-or-later. */
(function attachQuizAppShell(global) {
  'use strict';

  const tabs = [
    { key: 'home', label: '首页', icon: '⌂' },
    { key: 'library', label: '题库', icon: '▦' },
    { key: 'study', label: '学习', icon: '◴' },
    { key: 'profile', label: '我的', icon: '●' },
  ];

  function validTab(value) {
    return tabs.some(tab => tab.key === value) ? value : 'home';
  }

  function renderNavigation(active) {
    return `<nav class="main-nav" aria-label="主导航">${tabs.map(tab => `
      <button class="main-nav-button ${active === tab.key ? 'active' : ''}" onclick="setMainTab('${tab.key}')" aria-label="${tab.label}">
        <span class="icon">${tab.icon}</span><span>${tab.label}</span>
      </button>
    `).join('')}</nav>`;
  }

  function renderTopbar(context) {
    const titles = { home: '学习首页', library: '题库', study: '学习', profile: '我的' };
    let actions = '';
    if (context.tab === 'home') {
      actions = `<button class="btn-text" onclick="toggleEditMode()">${context.editMode ? '完成' : '编辑'}</button><button class="btn-icon mail-button ${context.unreadAnnouncements ? 'unread' : ''}" onclick="openAnnouncementBoard()" aria-label="公告栏" title="公告栏">✉</button>`;
    } else if (context.tab === 'library') {
      actions = `<button class="btn-text" onclick="toggleEditMode()">${context.editMode ? '完成' : '编辑'}</button>`;
    } else if (context.tab === 'study') {
      actions = '<button class="btn-text" onclick="openStudyStats()">统计</button>';
    } else {
      actions = `<span class="topbar-meta">${context.version}</span>`;
    }
    return `<div class="topbar">
      <h1 class="brand-title"><span class="brand-mark">题</span><span>${titles[context.tab]}</span></h1>
      <div class="topbar-actions">${actions}</div>
    </div>`;
  }

  function renderSavedProgress(context) {
    const saved = context.saved;
    if (!saved || !context.showSavedProgressEntry) return '';
    return `<div class="saved-progress-float ${context.showSavedProgressHint ? 'has-hint' : ''}">
      <div class="icon">续</div>
      <div class="info">
        <strong>${escapeHtml(saved.title || '保存的练习')}</strong>
        <small>${saved.currentIndex + 1}/${context.savedTotal} · ${escapeHtml(saved.reviewMode ? '背题模式' : (saved.subtitle || '练习'))}</small>
      </div>
      <div class="actions"><button onclick="resumeSavedSession()">继续</button><button class="secondary" onclick="clearSavedSession()">清除</button></div>
      ${context.showSavedProgressHint ? '<div class="saved-progress-hint">这里可以直接进入上一次做题进度。<button onclick="dismissSavedProgressHint()">不再提醒</button></div>' : ''}
    </div>`;
  }

  function renderHomePanel(context) {
    return `<div class="hub-section">
      <div class="hub-lead-grid ${context.saved && context.showSavedProgressEntry ? '' : 'single'}">
        ${renderSavedProgress(context)}
        <button class="ai-entry" onclick="openAiChat()">
          <span class="ai-entry-mark">AI</span>
          <span class="ai-entry-copy"><strong>AI 助手</strong><small>${context.aiReady ? escapeHtml(context.aiModel) : '配置 API 后开始对话'}</small></span>
          <span class="ai-entry-arrow">›</span>
        </button>
      </div>
      <div class="home-overview"><div class="home-metrics">${context.metricsHtml}</div></div>
      <div class="hub-section-title"><h2>选择科目开始学习</h2><button onclick="setMainTab('study')">查看全部</button></div>
      <div class="subjects-grid">${(context.editMode ? context.subjectEditCards : context.subjectCards).slice(0, 4).join('')}</div>
      <div class="hub-section-title"><h2>快捷入口</h2></div>
      <div class="hub-quick-grid compact-tools">
        <button class="hub-quick-action" onclick="openReviewQueue()"><span class="icon">习</span><span>复习队列</span></button>
        <button class="hub-quick-action" onclick="openWrongPractice('', '', true)"><span class="icon">错</span><span>错题集</span></button>
        <button class="hub-quick-action" onclick="openFreeNotebookLibrary()"><span class="icon">记</span><span>自由笔记</span></button>
        <button class="hub-quick-action" onclick="openExamSetup()"><span class="icon">考</span><span>模拟考试</span></button>
      </div>
      <div class="home-toggle-row">${renderToolToggle('home-tools')}</div>
      ${context.editMode || context.homeToolsExpanded ? `<div class="tool-group">${context.toolPanelsHtml}</div>` : ''}
    </div>`;
  }

  function renderLibraryPanel(context) {
    if (!context.hasBanks) {
      return `<div class="notebook-empty">${context.loadingDefaults ? '正在加载题库' : '还没有题库'}<br>${escapeHtml(context.defaultLoadError || '进入编辑模式后可以导入 JSON 题库')}</div>`;
    }
    return `<div class="hub-section">
      <div class="hub-summary-band"><div><strong>${context.subjectCount} 个科目</strong><small>共 ${context.totalQuestions} 道题，按科目和章节管理</small></div><span class="value">${context.totalQuestions}</span></div>
      ${context.editMode ? renderLibraryManager([], { canEdit: false }) : ''}
      ${context.editMode ? `<div class="subjects-grid">${context.subjectEditCards.join('')}</div><div class="bank-list">${context.rootBankCards.join('')}</div>` : `<div class="subjects-grid">${context.subjectCards.join('')}${context.rootBankCards.join('')}</div>`}
      ${context.editMode ? '<button class="import-zone" onclick="document.getElementById(\'fileInput\').click()"><span class="big">＋</span><p>导入一个或多个 JSON 题库</p></button>' : ''}
      ${context.editMode && context.importedCount ? '<button class="option" style="justify-content:center;color:var(--danger);font-weight:600" onclick="clearImportedBanks()">清除导入题库</button>' : ''}
    </div>`;
  }

  function renderStudyPanel(context) {
    const review = context.reviewStats || { total: 0, due: 0 };
    const exam = context.examStats || { total: 0, latestScore: null };
    const study = context.studyConsole || { subjects: [], scopes: [], subjectStats: {}, selectedPath: [] };
    const activeScope = study.scopes.find(item => item.active) || study.scopes[0] || { token: '', label: '', count: 0 };
    const scopeOptions = study.scopes.map(item => ({
      value: item.token,
      label: `${item.label} · ${item.count} 题`,
    }));
    return `<div class="hub-section">
      <div class="hub-summary-band" onclick="openStudyStats()" role="button" tabindex="0"><div><strong>今日学习 ${context.todayStudyTime}</strong><small>累计已练 ${context.progressAnswered}/${context.totalQuestions} 题</small></div><span class="value">${context.progressPercent}%</span></div>
      <div class="study-console">
        <div class="hub-section-title"><h2>选择科目</h2><button onclick="setMainTab('library')">管理题库</button></div>
        <div class="study-subject-switcher">${study.subjects.map(item => `
          <button class="study-subject-chip ${item.active ? 'active' : ''}" onclick="selectStudySubject('${item.token}')">
            <span><strong>${escapeHtml(item.name)}</strong><small>${item.count} 题</small></span>
          </button>`).join('') || '<div class="notebook-empty">暂无可学习科目</div>'}</div>
        ${study.subject ? `
          <div class="hub-section-title"><h2>学习范围</h2><span class="settings-subtitle">选择范围不会离开本页</span></div>
          <div class="study-scope-dropdown">${renderChoiceSelect('studyScopeSelect', activeScope.token, scopeOptions, 'studyScope')}</div>
          <div class="study-scope-switcher">${study.scopes.map(item => `<button class="study-scope-chip ${item.active ? 'active' : ''}" onclick="selectStudyScope('${item.token}')">${escapeHtml(item.label)} · ${item.count}</button>`).join('')}</div>
          <div class="study-scope-summary"><div><strong>${escapeHtml(study.selectedLabel)}</strong><small>已练 ${study.scopeAnswered}/${study.scopeQuestionCount} · 错题集 ${study.scopeWrong} · 今日本科 ${escapeHtml(formatStudyDuration(study.subjectStats.todaySeconds || 0))}</small></div><span>${study.scopePercent}%</span></div>
          <div class="hub-section-title"><h2>开始学习</h2><span class="settings-subtitle">当前范围 ${study.scopeQuestionCount} 题</span></div>
          <div class="study-mode-grid">
            <button class="study-mode-entry" onclick="startSelectedStudyPractice(false)"><span class="icon">1</span><span>顺序练习</span></button>
            <button class="study-mode-entry" onclick="startSelectedStudyPractice(true)"><span class="icon">↝</span><span>随机练习</span></button>
            <button class="study-mode-entry" onclick="startSelectedStudyReview()"><span class="icon">答</span><span>背题模式</span></button>
            <button class="study-mode-entry" onclick="openSelectedStudyAnswers()"><span class="icon">表</span><span>答案表</span></button>
            <button class="study-mode-entry danger" onclick="openSelectedStudyWrongBook()"><span class="icon">错</span><span>错题集</span></button>
            <button class="study-mode-entry" onclick="openSelectedStudyNotebook()"><span class="icon">写</span><span>题目笔记</span></button>
            <button class="study-mode-entry" onclick="openReviewQueue()"><span class="icon">习</span><span>复习队列 ${review.due}</span></button>
            <button class="study-mode-entry" onclick="openExamSetup()"><span class="icon">考</span><span>模拟考试</span></button>
          </div>` : ''}
      </div>
      <div class="hub-study-status">
        <button onclick="openReviewQueue()"><strong>${review.due}</strong><span>待复习</span><small>${review.total} 张卡片</small></button>
        <button onclick="openFreeNotebookLibrary()"><strong>${context.notebookCount || 0}</strong><span>自由笔记</span><small>独立笔记工作台</small></button>
        <button onclick="openExamSetup()"><strong>${exam.latestScore == null ? '--' : exam.latestScore}</strong><span>最近考试</span><small>${exam.total} 次记录</small></button>
      </div>
      <details class="hub-global-practice">
        <summary>全库学习</summary>
        <div class="hub-quick-grid">
          <button class="hub-quick-action" onclick="startAllPractice(false)"><span class="icon">1</span><span>全部顺序</span></button>
          <button class="hub-quick-action" onclick="startAllPractice(true)"><span class="icon">↝</span><span>全部随机</span></button>
          <button class="hub-quick-action" onclick="startAllReview()"><span class="icon">答</span><span>全部背题</span></button>
          <button class="hub-quick-action" onclick="openAnswerLookup('', '')"><span class="icon">表</span><span>全部答案表</span></button>
        </div>
      </details>
    </div>`;
  }

  function renderProfilePanel(context) {
    return `<div class="hub-profile">
      <div class="hub-profile-header"><div class="hub-profile-avatar">题</div><div><strong>QuizApp</strong><small>开源免费学习工具 · ${context.version}</small></div></div>
      <div class="hub-menu">
        <button class="hub-menu-button" onclick="openSettings()"><span class="menu-icon">设</span><span><strong>应用设置</strong><small>外观、练习、保存、存储和说明</small></span><span class="arrow">›</span></button>
        <button class="hub-menu-button" onclick="openAiSettings()"><span class="menu-icon">AI</span><span><strong>AI 与模型</strong><small>API Key、模型和本机 AI 数据</small></span><span class="arrow">›</span></button>
        <button class="hub-menu-button" onclick="openAnnouncementBoard()"><span class="menu-icon">✉</span><span><strong>公告</strong><small>版本说明、项目公告和免责声明</small></span><span class="arrow">›</span></button>
        <button class="hub-menu-button" onclick="checkForUpdates({manual:true})"><span class="menu-icon">↻</span><span><strong>检查更新</strong><small>通过 GitHub Release 获取 APK</small></span><span class="arrow">›</span></button>
        <button class="hub-menu-button" onclick="openExternal('https://github.com/konwait12/QuizApp')"><span class="menu-icon">GH</span><span><strong>项目仓库</strong><small>源码、问题反馈和 Release</small></span><span class="arrow">›</span></button>
      </div>
    </div>`;
  }

  function render(context) {
    const tab = validTab(context.tab);
    context.tab = tab;
    const content = tab === 'library'
      ? renderLibraryPanel(context)
      : tab === 'study'
        ? renderStudyPanel(context)
        : tab === 'profile'
          ? renderProfilePanel(context)
          : renderHomePanel(context);
    return `${renderTopbar(context)}<div class="viewport view-scroll main-hub-viewport"><div class="home main-hub">${content}<input type="file" id="fileInput" accept=".json,application/json,text/json,text/plain,*/*" multiple hidden onchange="handleFile(this)"></div></div>${renderNavigation(tab)}`;
  }

  global.QuizAppShell = { render, validTab };
})(window);
