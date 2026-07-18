import assert from 'node:assert/strict';
import path from 'node:path';
import { createRequire } from 'node:module';
import { pathToFileURL } from 'node:url';

const nodeModules = process.env.PLAYWRIGHT_NODE_MODULES;
if (!nodeModules) throw new Error('Set PLAYWRIGHT_NODE_MODULES to the bundled node_modules directory');
const require = createRequire(pathToFileURL(path.join(nodeModules, 'quizapp-playwright-resolver.cjs')));
const { chromium } = require('playwright');
const appUrl = process.env.QUIZAPP_URL || 'http://127.0.0.1:8139/';
const testBank = {
  id: 'settings-behavior-bank',
  name: '设置行为回归',
  subject: '设置回归',
  chapter: '第一章',
  path: ['设置回归', '第一章'],
  questions: [
    { id: 'q1', type: '单选', q: '自动保存题', options: ['正确', '错误'], ans: 'A' },
    { id: 'q2', type: '多选', q: '多选题', options: ['一', '二', '三'], ans: 'AB' },
    { id: 'q3', type: '判断', q: '判断题', options: ['对', '错'], ans: 'A' },
  ],
};

const browser = await chromium.launch({
  headless: true,
  ...(process.env.PLAYWRIGHT_CHROMIUM_EXECUTABLE ? { executablePath: process.env.PLAYWRIGHT_CHROMIUM_EXECUTABLE } : {}),
});
const page = await browser.newPage({ viewport: { width: 1280, height: 800 } });
const pageErrors = [];
page.on('pageerror', error => pageErrors.push(error.message));

try {
  await page.addInitScript(bank => {
    window.__QUIZAPP_EMBEDDED_BANKS__ = [bank];
    localStorage.setItem('quizapp_ui_config', JSON.stringify({ autoUpdateCheck: false, autoAnnouncementCheck: false, autoBankUpdateCheck: false }));
    localStorage.setItem('quizapp_announcement_suppressed', '1');
  }, testBank);
  await page.goto(appUrl, { waitUntil: 'domcontentloaded' });
  await page.waitForFunction(() => getVisibleBanks().length > 0 && typeof startBankPractice === 'function');

  const autoSave = await page.evaluate(() => {
    const bank = getVisibleBanks()[0];
    localStorage.removeItem(SAVED_SESSION_KEY);
    localStorage.removeItem(SAVED_PROGRESS_KEY);
    state.answerState = { sequence: {}, random: {} };
    saveAnswerState();
    state.uiConfig.autoSaveOnExit = false;
    state.navStack = [];
    renderHome();
    startBankPractice(bank.id, false, true);
    const key = questionKey(state.activeSession.questions[0]);
    answerSingle(0, 'A');
    backFromQuiz();
    const disabled = {
      savedSession: Boolean(localStorage.getItem(SAVED_SESSION_KEY)),
      answer: state.answerState.sequence[key] || '',
    };

    state.uiConfig.autoSaveOnExit = true;
    startBankPractice(bank.id, false, true);
    answerSingle(0, 'A');
    backFromQuiz();
    const enabled = {
      savedSession: Boolean(localStorage.getItem(SAVED_SESSION_KEY)),
      answer: state.answerState.sequence[key] || '',
    };
    return { disabled, enabled };
  });
  assert.deepEqual(autoSave.disabled, { savedSession: false, answer: '' });
  assert.deepEqual(autoSave.enabled, { savedSession: true, answer: 'A' });

  const scopePolicy = await page.evaluate(() => {
    const bank = getVisibleBanks()[0];
    state.uiConfig.autoSaveOnExit = false;
    state.uiConfig.progressScopePolicy = 'separate';
    startBankPractice(bank.id, false, true);
    const sequence = state.activeSession.scopeKey;
    backFromQuiz();
    startBankPractice(bank.id, true, true);
    const random = state.activeSession.scopeKey;
    backFromQuiz();
    state.uiConfig.progressScopePolicy = 'merged';
    startBankPractice(bank.id, false, true);
    const mergedSequence = state.activeSession.scopeKey;
    backFromQuiz();
    startBankPractice(bank.id, true, true);
    const mergedRandom = state.activeSession.scopeKey;
    backFromQuiz();
    return { sequence, random, mergedSequence, mergedRandom };
  });
  assert.notEqual(scopePolicy.sequence, scopePolicy.random);
  assert.equal(scopePolicy.mergedSequence, scopePolicy.mergedRandom);

  const rememberedViews = await page.evaluate(() => {
    const bank = getVisibleBanks()[0];
    localStorage.removeItem(VIEW_PROGRESS_KEY);
    state.uiConfig.rememberReviewPosition = false;
    startChapterReview(bank.id, true);
    state.currentIndex = 1;
    state.mode = 'scroll';
    saveReviewPosition();
    const reviewDisabled = Object.keys(getViewProgress()).length;
    state.uiConfig.rememberReviewPosition = true;
    saveReviewPosition();
    const reviewEnabled = Object.values(getViewProgress()).some(item => item.type === 'review');
    state.activeSession = null;
    state.reviewMode = false;

    state.uiConfig.rememberAnswerLookupPosition = false;
    state.answerLookupContext = { subject: bank.subject, baseChapter: '', chapter: bank.chapter, query: '自动', scrollTop: 12 };
    saveAnswerLookupPosition();
    const answerDisabled = Object.values(getViewProgress()).some(item => item.type === 'answerLookup');
    state.uiConfig.rememberAnswerLookupPosition = true;
    saveAnswerLookupPosition();
    const answerEnabled = Object.values(getViewProgress()).some(item => item.type === 'answerLookup');
    return { reviewDisabled, reviewEnabled, answerDisabled, answerEnabled };
  });
  assert.equal(rememberedViews.reviewDisabled, 0);
  assert.equal(rememberedViews.reviewEnabled, true);
  assert.equal(rememberedViews.answerDisabled, false);
  assert.equal(rememberedViews.answerEnabled, true);

  const visualSettings = await page.evaluate(() => {
    state.uiConfig.systemTheme = 'standard';
    state.uiConfig.componentRadius = 7;
    state.uiConfig.reduceMotion = true;
    state.uiConfig.savedProgressWidgetWidthPhone = 410;
    state.uiConfig.savedProgressWidgetWidthTablet = 680;
    applyUiConfig();
    saveUiConfig();
    return {
      radius: getComputedStyle(document.documentElement).getPropertyValue('--radius').trim(),
      reduceMotion: document.body.dataset.reduceMotion,
      phone: getComputedStyle(document.documentElement).getPropertyValue('--saved-progress-width-phone').trim(),
      tablet: getComputedStyle(document.documentElement).getPropertyValue('--saved-progress-width-tablet').trim(),
    };
  });
  assert.deepEqual(visualSettings, { radius: '7px', reduceMotion: 'true', phone: '410px', tablet: '680px' });

  const visibilitySettings = await page.evaluate(() => {
    state.uiConfig.toolPanelsDefaultExpanded = false;
    state.panelExpanded = {};
    state.uiConfig.showSavedProgressEntry = false;
    state.uiConfig.showSavedProgressHint = false;
    state.mainTab = 'home';
    renderHome();
    const collapsed = !document.querySelector('.tool-group');
    const savedHidden = !document.querySelector('.saved-progress-float');
    state.uiConfig.toolPanelsDefaultExpanded = true;
    state.panelExpanded = {};
    renderHome();
    const expanded = Boolean(document.querySelector('.tool-group'));
    return { collapsed, expanded, savedHidden };
  });
  assert.deepEqual(visibilitySettings, { collapsed: true, expanded: true, savedHidden: true });

  const studyPersistence = await page.evaluate(() => {
    state.studyStats[getTodayKey()] = { seconds: 42, subjects: {}, modes: {}, updatedAt: Date.now() };
    setPersistStudyStats(true);
    const saved = Boolean(localStorage.getItem(STUDY_STATS_KEY));
    setPersistStudyStats(false);
    const removed = !localStorage.getItem(STUDY_STATS_KEY);
    return { saved, removed };
  });
  assert.deepEqual(studyPersistence, { saved: true, removed: true });

  const bankUpdateSetting = await page.evaluate(() => {
    state.uiConfig.autoBankUpdateCheck = true;
    renderSettings();
    const input = document.getElementById('autoBankUpdateCheck');
    const defaultChecked = Boolean(input?.checked);
    input.checked = false;
    saveAllSettings();
    const persisted = JSON.parse(localStorage.getItem(UI_CONFIG_KEY) || '{}').autoBankUpdateCheck;
    return { defaultChecked, current: state.uiConfig.autoBankUpdateCheck, persisted };
  });
  assert.deepEqual(bankUpdateSetting, { defaultChecked: true, current: false, persisted: false });

  await page.evaluate(() => {
    state.uiConfig = {
      ...state.uiConfig,
      systemTheme: 'standard',
      theme: 'light',
      palette: 'forest',
      primary: '#1f8f62',
      componentRadius: 14,
      reduceMotion: false,
      autoSaveOnExit: true,
      progressScopePolicy: 'separate',
      rememberReviewPosition: true,
      rememberAnswerLookupPosition: true,
      autoBankUpdateCheck: true,
      persistStudyStats: true,
      toolPanelsDefaultExpanded: false,
      showSavedProgressEntry: true,
      showSavedProgressHint: true,
      savedProgressWidgetWidthPhone: 360,
      savedProgressWidgetWidthTablet: 520,
    };
    normalizeUiConfig();
    applyUiConfig();
    saveUiConfig();
  });
  assert.deepEqual(pageErrors, []);
  console.log(JSON.stringify({
    autoSaveOnOff: true,
    separateAndMergedProgress: true,
    reviewPositionOnOff: true,
    answerLookupPositionOnOff: true,
    componentRadius: true,
    reduceMotion: true,
    savedWidgetWidths: true,
    toolDefaultExpansion: true,
    savedProgressVisibility: true,
    studyPersistence: true,
    autoBankUpdateSetting: true,
  }, null, 2));
} finally {
  await browser.close();
}
