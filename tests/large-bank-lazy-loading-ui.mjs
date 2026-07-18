import assert from 'node:assert/strict';
import path from 'node:path';
import { createRequire } from 'node:module';
import { pathToFileURL } from 'node:url';

const nodeModules = process.env.PLAYWRIGHT_NODE_MODULES;
if (!nodeModules) throw new Error('Set PLAYWRIGHT_NODE_MODULES to the bundled node_modules directory');
const require = createRequire(pathToFileURL(path.join(nodeModules, 'quizapp-playwright-resolver.cjs')));
const { chromium } = require('playwright');
const appUrl = process.env.QUIZAPP_URL || 'http://127.0.0.1:8141/';
const browser = await chromium.launch({
  headless: true,
  ...(process.env.PLAYWRIGHT_CHROMIUM_EXECUTABLE ? { executablePath: process.env.PLAYWRIGHT_CHROMIUM_EXECUTABLE } : {}),
});
const page = await browser.newPage({ viewport: { width: 1280, height: 800 } });
const errors = [];
page.on('pageerror', error => errors.push(error.message));

const bankId = 'lazy-bank-test';
const bank = {
  id: bankId,
  name: '懒加载测试题库',
  subject: '懒加载测试科目',
  chapter: '第一章',
  path: ['懒加载测试科目', '第一章'],
  questions: [
    { id: 'q1', type: 'single', q: '测试题一', options: ['甲', '乙', '丙'], ans: 'A' },
    { id: 'q2', type: 'multi', q: '测试题二', options: ['甲', '乙', '丙'], ans: 'AB' },
    { id: 'q3', type: 'bool', q: '测试题三', options: ['对', '错'], ans: 'A' },
  ],
};

try {
  await page.addInitScript(() => {
    localStorage.setItem('quizapp_ui_config', JSON.stringify({
      autoUpdateCheck: false,
      autoAnnouncementCheck: false,
      autoBankUpdateCheck: false,
    }));
    localStorage.setItem('quizapp_announcement_suppressed', '1');
  });
  await page.goto(appUrl, { waitUntil: 'domcontentloaded' });
  await page.waitForFunction(() => typeof persistLargeBanks === 'function' && !state.loadingDefaults);
  await page.evaluate(async source => {
    await persistLargeBanks([source]);
    localStorage.setItem(STORAGE_KEY, JSON.stringify([{
      id: source.id,
      name: source.name,
      subject: source.subject,
      chapter: source.chapter,
      path: source.path,
      storage: 'indexeddb',
      questionCount: source.questions.length,
    }]));
  }, bank);

  await page.addInitScript(() => {
    window.__startupLargeBankReads = 0;
    const originalGet = IDBObjectStore.prototype.get;
    IDBObjectStore.prototype.get = function trackedGet(...args) {
      if (this.name === 'large_banks') window.__startupLargeBankReads += 1;
      return originalGet.apply(this, args);
    };
  });

  await page.reload({ waitUntil: 'domcontentloaded' });
  await page.waitForFunction(id => !state.loadingDefaults && state.banks.some(item => item.id === id), bankId);

  const startup = await page.evaluate(id => {
    const target = state.banks.find(item => item.id === id);
    const group = getSubjectGroups().find(item => item.subject === target.subject);
    state.answerState.sequence[`${id}:1`] = 'AB';
    const progress = getPracticeProgressSummary();
    return {
      hydrated: target.hydrated,
      questions: target.questions.length,
      count: getBankQuestionCount(target),
      groupCount: group?.questionCount,
      progressTotal: progress.total,
      progressCompleted: progress.completed,
      startupReads: window.__startupLargeBankReads,
    };
  }, bankId);
  assert.equal(startup.hydrated, false);
  assert.equal(startup.questions, 0);
  assert.equal(startup.count, 3);
  assert.equal(startup.groupCount, 3);
  assert.ok(startup.progressTotal >= 3);
  assert.ok(startup.progressCompleted >= 1);
  assert.equal(startup.startupReads, 0);

  const raceBankId = 'lazy-persistence-race-test';
  await page.evaluate(data => {
    importJSON(JSON.stringify(data), data.path);
  }, {
    id: raceBankId,
    name: '异步持久化测试',
    subject: '持久化测试科目',
    chapter: '第一章',
    path: ['持久化测试科目', '第一章'],
    questions: [{ id: 'large-q1', type: 'single', q: '大型题库写入竞态测试', options: ['甲', '乙', '丙'], ans: 'A', exp: 'x'.repeat(760000) }],
  });
  await page.waitForFunction(() => state.bankPersistencePending === 0);
  const persistenceRace = await page.evaluate(async id => {
    const reference = state.banks.find(item => item.id === id);
    const stored = (await readLargeBanks([id]))[0];
    return {
      hydrated: reference?.hydrated,
      count: getBankQuestionCount(reference),
      storedQuestions: stored?.questions?.length || 0,
      storedText: stored?.questions?.[0]?.q || '',
    };
  }, raceBankId);
  assert.deepEqual(persistenceRace, {
    hydrated: false,
    count: 1,
    storedQuestions: 1,
    storedText: '大型题库写入竞态测试',
  });

  await page.evaluate(() => {
    const original = readLargeBanks;
    window.__largeBankReadCalls = [];
    readLargeBanks = async ids => {
      window.__largeBankReadCalls.push(ids.slice());
      return original(ids);
    };
  });

  await page.evaluate(async id => startChapterPractice(id, false, true), bankId);
  await page.locator('.quiz-topbar').waitFor({ state: 'visible' });
  const practice = await page.evaluate(id => ({
    sessionQuestions: state.activeSession.questions.length,
    bankHydrated: state.banks.find(item => item.id === id).hydrated,
    calls: window.__largeBankReadCalls.slice(),
  }), bankId);
  assert.equal(practice.sessionQuestions, 3);
  assert.equal(practice.bankHydrated, false);
  assert.deepEqual(practice.calls.at(-1), [bankId]);
  await page.evaluate(() => backFromQuiz());

  await page.evaluate(async () => openAnswerLookup('懒加载测试科目', ''));
  await page.locator('.answer-row').first().waitFor({ state: 'visible' });
  assert.equal(await page.locator('.answer-row').count(), 3);
  assert.equal(await page.evaluate(id => state.banks.find(item => item.id === id).hydrated, bankId), false);
  await page.evaluate(() => goBackRoute());

  await page.evaluate(async () => openHandwritingPractice(['懒加载测试科目', '第一章']));
  await page.locator('.notebook-workspace').waitFor({ state: 'visible' });
  assert.equal(await page.evaluate(() => state.inkSession.questions.length), 3);
  assert.equal(await page.evaluate(id => state.banks.find(item => item.id === id).hydrated, bankId), false);
  await page.evaluate(() => goHome());

  await page.evaluate(async id => {
    const keys = [`${id}:0`, `${id}:1`, `${id}:2`];
    localStorage.setItem(SAVED_SESSION_KEY, JSON.stringify({
      title: '懒加载恢复测试',
      subject: '懒加载测试科目',
      bankIds: [id],
      questionOrderKeys: keys,
      currentQuestionKey: keys[1],
      currentIndex: 1,
      total: 3,
      shuffled: false,
      reviewMode: false,
      savedAt: Date.now(),
    }));
    await resumeSavedSession();
  }, bankId);
  await page.locator('.quiz-topbar').waitFor({ state: 'visible' });
  assert.deepEqual(await page.evaluate(id => ({
    questions: state.activeSession.questions.length,
    currentIndex: state.currentIndex,
    bankHydrated: state.banks.find(item => item.id === id).hydrated,
  }), bankId), { questions: 3, currentIndex: 1, bankHydrated: false });
  await page.evaluate(() => backFromQuiz());

  await page.evaluate(() => openExamSetup());
  await page.locator('.exam-setup-page').waitFor({ state: 'visible' });
  await page.evaluate(async () => {
    document.getElementById('examSubject').value = '懒加载测试科目';
    await startConfiguredExam();
  });
  await page.locator('.exam-question-card').waitFor({ state: 'visible' });
  assert.equal(await page.evaluate(() => state.examSession.questions.length), 3);
  assert.equal(await page.evaluate(id => state.banks.find(item => item.id === id).hydrated, bankId), false);

  assert.deepEqual(errors, []);
  console.log(JSON.stringify({
    startupIndexedDbReads: startup.startupReads,
    indexedReferenceCount: startup.count,
    chapterScopedHydration: true,
    asyncPersistenceRace: true,
    practice: true,
    answerLookup: true,
    handwriting: true,
    savedSession: true,
    exam: true,
  }, null, 2));
} finally {
  await browser.close();
}
