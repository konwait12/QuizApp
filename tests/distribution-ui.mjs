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
const pageErrors = [];
page.on('pageerror', error => pageErrors.push(error.message));

let releaseTag = 'v1.0.20';
let bankManifestDelay = 0;
let bankManifestRevision = 0;
const release = () => ({
  tag_name: releaseTag,
  target_commitish: 'distribution-test-commit',
  html_url: 'https://example.test/release',
  assets: [
    { id: 1, name: 'QuizApp-latest.apk', size: 1000, browser_download_url: 'https://example.test/QuizApp-latest.apk' },
    { id: 2, name: 'quizapp-announcements.json', browser_download_url: 'https://example.test/quizapp-announcements.json' },
    { id: 3 + bankManifestRevision, name: 'quizapp-bank-manifest.json', size: 500 + bankManifestRevision, updated_at: `2026-07-18T00:00:0${bankManifestRevision}Z`, browser_download_url: 'https://example.test/quizapp-bank-manifest.json' },
    { id: 4, name: 'quizapp-bank-001.json', browser_download_url: 'https://example.test/quizapp-bank-001.json' },
    { id: 5, name: 'quizapp-bank-002.json', browser_download_url: 'https://example.test/quizapp-bank-002.json' },
    { id: 6, name: 'quizapp-bank-003.json', browser_download_url: 'https://example.test/quizapp-bank-003.json' },
  ],
});
const announcements = {
  announcements: [{
    id: 'remote-distribution-test',
    title: '远程测试公告',
    date: '2026-07-18',
    latest: true,
    body: '<p>公告分发链路测试。</p>',
  }],
};
const manifest = {
  schemaVersion: 2,
  banks: [
    { file: 'quizapp-bank-001.json', name: '测试科目-第一章', subject: '测试科目', chapter: '第一章', path: ['测试科目', '第一章'], questionCount: 1 },
    { file: 'quizapp-bank-002.json', name: '测试科目-第二章', subject: '测试科目', chapter: '第二章', path: ['测试科目', '第二章'], questionCount: 1 },
    { file: 'quizapp-bank-003.json', name: '另一科目-题库包-导论-选择题', subject: '另一科目', chapter: '选择题', path: ['另一科目', '题库包', '导论', '选择题'], questionCount: 1 },
  ],
};
const bankPayload = (subject, chapter, answer = 'A') => ({
  id: `distribution:${subject}:${chapter}`,
  name: `${subject}-${chapter}`,
  subject,
  chapter,
  path: [subject, chapter],
  questions: [{ q: `${subject}${chapter}测试题`, opts: ['选项 A', '选项 B'], ans: answer, type: 'single', explanation: '内置解析' }],
});

try {
  await page.route('**/repos/konwait12/QuizApp/releases/latest', route => route.fulfill({
    status: 200,
    contentType: 'application/json',
    body: JSON.stringify(release()),
  }));
  await page.route('**/distribution/quizapp-announcements.json', route => route.fulfill({
    status: 200,
    contentType: 'application/json',
    body: JSON.stringify(announcements),
  }));
  await page.route('https://example.test/quizapp-announcements.json', route => route.fulfill({
    status: 200,
    contentType: 'application/json',
    body: JSON.stringify(announcements),
  }));
  await page.route('https://example.test/quizapp-bank-manifest.json', async route => {
    if (bankManifestDelay) await new Promise(resolve => setTimeout(resolve, bankManifestDelay));
    await route.fulfill({ status: 200, contentType: 'application/json', body: JSON.stringify(manifest) });
  });
  await page.route('https://example.test/quizapp-bank-001.json', route => route.fulfill({ status: 200, contentType: 'application/json', body: JSON.stringify(bankPayload('测试科目', '第一章')) }));
  await page.route('https://example.test/quizapp-bank-002.json', route => route.fulfill({ status: 200, contentType: 'application/json', body: JSON.stringify(bankPayload('测试科目', '第二章', 'B')) }));
  await page.route('https://example.test/quizapp-bank-003.json', route => route.fulfill({ status: 200, contentType: 'application/json', body: JSON.stringify(bankPayload('另一科目', '导论')) }));

  await page.addInitScript(() => {
    localStorage.setItem('quizapp_ui_config', JSON.stringify({ autoUpdateCheck: false, autoAnnouncementCheck: false, autoBankUpdateCheck: false }));
    localStorage.setItem('quizapp_announcement_suppressed', '1');
    localStorage.removeItem('quizapp_remote_announcements');
    localStorage.removeItem('quizapp_announcement_read_ids');
  });
  await page.goto(appUrl, { waitUntil: 'domcontentloaded' });
  await page.waitForFunction(() => typeof checkForUpdates === 'function' && !state.loadingDefaults);
  await page.evaluate(() => {
    document.querySelectorAll('.app-dialog-mask').forEach(element => element.remove());
  });

  releaseTag = await page.evaluate(() => APP_VERSION);
  await page.evaluate(async () => {
    state.uiConfig.autoUpdateCheck = true;
    try { return await checkForUpdates({ manual: false }); }
    finally { state.uiConfig.autoUpdateCheck = false; }
  });
  await page.waitForTimeout(80);
  assert.equal(await page.locator('.app-dialog-mask').count(), 0, 'automatic no-update check must stay silent');

  await page.evaluate(() => checkForUpdates({ manual: true }));
  await page.getByText(/当前已是最新版本/).waitFor({ state: 'visible' });
  await page.getByRole('button', { name: '知道了' }).click();

  releaseTag = 'v9.9.9';
  await page.evaluate(async () => {
    state.uiConfig.autoUpdateCheck = true;
    try { return await checkForUpdates({ manual: false }); }
    finally { state.uiConfig.autoUpdateCheck = false; }
  });
  await page.getByRole('heading', { name: /发现新版本 v9\.9\.9/ }).waitFor({ state: 'visible' });
  assert.match(await page.locator('.announcement-dialog').innerText(), /覆盖安装不会清除本机题库/);
  await page.screenshot({ path: 'output/playwright/distribution-update-tablet.png' });
  await page.getByRole('button', { name: '稍后' }).click();

  await page.evaluate(async () => {
    state.uiConfig.autoAnnouncementCheck = true;
    try { return await checkForRemoteAnnouncements({ manual: false }); }
    finally { state.uiConfig.autoAnnouncementCheck = false; }
  });
  await page.getByRole('heading', { name: 'QuizApp 公告' }).waitFor({ state: 'visible' });
  assert.match(await page.locator('.announcement-dialog').innerText(), /远程测试公告/);
  await page.getByRole('button', { name: '知道了' }).click();
  assert.equal(await page.evaluate(() => hasUnreadAnnouncements()), false, 'closing the announcement should mark it read');

  await page.evaluate(async () => {
    state.uiConfig.autoBankUpdateCheck = true;
    try { return await checkForBankUpdates({ manual: false }); }
    finally { state.uiConfig.autoBankUpdateCheck = false; }
  });
  await page.getByRole('heading', { name: '题库更新检查' }).waitFor({ state: 'visible' });
  assert.match(await page.locator('.announcement-dialog').innerText(), /发现可下载的题库更新包/);
  await page.locator('#bankUpdateDontShowThisBatch').check();
  await page.getByRole('button', { name: '稍后' }).click();
  await page.evaluate(async () => {
    state.uiConfig.autoBankUpdateCheck = true;
    try { return await checkForBankUpdates({ manual: false }); }
    finally { state.uiConfig.autoBankUpdateCheck = false; }
  });
  assert.equal(await page.locator('.app-dialog-mask').count(), 0, 'dismissed bank manifest fingerprint should stay silent during automatic checks');
  bankManifestRevision = 1;
  await page.evaluate(async () => {
    state.uiConfig.autoBankUpdateCheck = true;
    try { return await checkForBankUpdates({ manual: false }); }
    finally { state.uiConfig.autoBankUpdateCheck = false; }
  });
  await page.getByRole('heading', { name: '题库更新检查' }).waitFor({ state: 'visible' });
  await page.getByRole('button', { name: '稍后' }).click();

  await page.evaluate(() => {
    state.banks = state.banks.filter(bank => !String(bank.id).startsWith('release:distribution:'));
    state.banks.push({
      id: 'imported:distribution-conflict',
      name: '测试科目-第一章',
      subject: '测试科目',
      chapter: '第一章',
      path: ['测试科目', '第一章'],
      hierarchy: ['测试科目', '第一章'],
      bundled: false,
      questions: [{ q: '本地旧题', opts: ['A', 'B'], ans: 'A', type: 'single' }],
    });
    localStorage.removeItem(BANK_UPDATE_META_KEY);
  });
  assert.equal(await page.evaluate(() => normalizeReleaseBankPayload({
    id: 'stable-bank-id',
    name: '稳定 ID 测试',
    subject: '测试科目',
    chapter: '稳定章节',
    path: ['测试科目', '稳定章节'],
    questions: [{ q: '稳定题目', opts: ['A', 'B'], ans: 'A' }],
  }, { tag_name: 'v9.9.9' }, 'stable.json', 0).id), 'stable-bank-id', 'release download must preserve stable bank ids for progress compatibility');
  bankManifestDelay = 180;
  const bankCheck = page.evaluate(() => checkForBankUpdates({ manual: true }));
  await page.getByRole('heading', { name: '正在检查题库更新' }).waitFor({ state: 'visible' });
  assert.equal(await page.locator('#bankCheckingFill').count(), 1, 'manual bank check should expose progress');
  await bankCheck;
  await page.getByRole('heading', { name: '题库更新检查' }).waitFor({ state: 'visible' });
  assert.equal(await page.locator('.release-bank-list > .release-bank-group').count(), 2, 'bank picker should group by subject');
  assert.equal(await page.locator('.release-bank-group').count(), 4, 'bank picker should preserve deeper package and chapter levels');
  assert.equal(await page.locator('.release-bank-check').count(), 3, 'bank picker should expose individual chapters');
  assert.match(await page.locator('.release-bank-list > .release-bank-group').last().innerText(), /题库包/);

  await page.locator('[data-bank-tree-action="clear-all"]').click();
  const testSubjectGroup = page.locator('.release-bank-group').filter({ hasText: '测试科目' });
  await testSubjectGroup.locator('.release-bank-subject-check').check();
  assert.equal(await testSubjectGroup.locator('.release-bank-check:checked').count(), 2, 'subject selection should select all chapters');
  await testSubjectGroup.locator('.release-bank-check').nth(1).uncheck();
  assert.equal(await testSubjectGroup.locator('.release-bank-subject-check').evaluate(input => input.indeterminate), true, 'partial chapter selection should mark the subject indeterminate');
  assert.match(await page.locator('#bankSelectionHint').innerText(), /1\/3/);

  const conflictChoice = page.locator('[data-choice-id="releaseBankAction0"]');
  await conflictChoice.locator('.choice-trigger').click();
  await conflictChoice.getByRole('option', { name: '下载为新题库' }).click();
  assert.equal(await page.locator('#releaseBankAction0').inputValue(), 'copy');
  await page.screenshot({ path: 'output/playwright/distribution-banks-tablet.png' });

  bankManifestDelay = 0;
  await page.getByRole('button', { name: '下载题库' }).click();
  await page.locator('#bankUpdateProgressText').getByText(/题库更新完成：1 个题库/).waitFor({ state: 'visible' });
  const downloaded = await page.evaluate(() => state.banks.filter(bank => bank.releaseManaged && bank.subject === '测试科目'));
  assert.equal(downloaded.length, 1, 'only the checked chapter should download');
  assert.match(downloaded[0].id, /^distribution:测试科目:第一章:copy:/, 'copy mode should derive its id from the stable source bank id');
  assert.match(downloaded[0].chapter, /第一章（9\.9\.9）/);

  await page.setViewportSize({ width: 390, height: 844 });
  await page.evaluate(entry => {
    const mobileRelease = { tag_name: 'v9.9.9', html_url: 'https://example.test/release' };
    showBankUpdateDialog({
      release: mobileRelease,
      items: [createReleaseBankItem({ entry, source: 'quizapp-bank-001.json', release: mobileRelease, index: 0 })],
    });
  }, manifest.banks[0]);
  assert.ok(await page.evaluate(() => document.documentElement.scrollWidth - document.documentElement.clientWidth <= 1));
  await page.screenshot({ path: 'output/playwright/distribution-banks-mobile.png' });

  assert.deepEqual(pageErrors, []);
  console.log(JSON.stringify({
    automaticNoUpdateSilent: true,
    manualNoUpdateNotice: true,
    versionDialog: true,
    remoteAnnouncement: true,
    automaticBankDiscovery: true,
    bankManifestFingerprint: true,
    automaticBankDismissal: true,
    bankCheckProgress: true,
    hierarchicalSelection: true,
    conflictCopy: true,
    selectedDownloadOnly: true,
    responsive: true,
  }, null, 2));
} finally {
  await browser.close();
}
