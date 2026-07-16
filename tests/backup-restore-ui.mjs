import assert from 'node:assert/strict';
import path from 'node:path';
import { createRequire } from 'node:module';
import { pathToFileURL } from 'node:url';

const nodeModules = process.env.PLAYWRIGHT_NODE_MODULES;
if (!nodeModules) throw new Error('Set PLAYWRIGHT_NODE_MODULES to the bundled node_modules directory');
const require = createRequire(pathToFileURL(path.join(nodeModules, 'quizapp-playwright-resolver.cjs')));
const { chromium } = require('playwright');
const appUrl = process.env.QUIZAPP_URL || 'http://127.0.0.1:8160/';

const browser = await chromium.launch({
  headless: true,
  ...(process.env.PLAYWRIGHT_CHROMIUM_EXECUTABLE ? { executablePath: process.env.PLAYWRIGHT_CHROMIUM_EXECUTABLE } : {}),
});
const page = await browser.newPage({ viewport: { width: 1280, height: 800 } });
const pageErrors = [];
page.on('pageerror', error => pageErrors.push(error.message));

try {
  await page.addInitScript(() => {
    localStorage.setItem('quizapp_ui_config', JSON.stringify({
      autoUpdateCheck: false,
      autoAnnouncementCheck: false,
    }));
    localStorage.setItem('quizapp_announcement_suppressed', '1');
  });
  await page.goto(appUrl, { waitUntil: 'domcontentloaded' });
  await page.waitForFunction(() => Boolean(window.QuizBackup && window.QuizNotebook && typeof openSettings === 'function'));
  await page.evaluate(() => document.querySelectorAll('.app-dialog-mask').forEach(element => element.remove()));

  const result = await page.evaluate(async () => {
    const probeKey = 'quizapp_backup_test_probe';
    const noteId = 'backup-test-note';
    const rollbackNoteId = 'backup-test-rollback-note';
    const bankId = 'backup-test-large-bank';
    const repository = getNotebookRepository();
    await repository.delete(noteId).catch(() => {});
    await repository.delete(rollbackNoteId).catch(() => {});

    localStorage.setItem(probeKey, 'original');
    localStorage.setItem('quizapp_ai_config', JSON.stringify({ provider: 'deepseek', apiKey: 'secret-from-source' }));
    await repository.put(QuizNotebook.createDocument({ id: noteId, title: 'Backup test note', kind: 'free' }));
    await persistLargeBanks([{ id: bankId, name: 'Backup test bank', path: ['Test', 'Large'], questions: [] }]);

    const backup = await QuizBackup.create({
      appVersion: APP_VERSION,
      buildCommit: APP_BUILD_COMMIT,
      databaseName: INK_DB_NAME,
      includeSecrets: false,
    });
    const inspection = await QuizBackup.inspect(backup);
    const backedAiConfig = JSON.parse(backup.data.localStorage.quizapp_ai_config || '{}');

    const corrupted = structuredClone(backup);
    corrupted.data.localStorage[probeKey] = 'tampered';
    const corruptedInspection = await QuizBackup.inspect(corrupted);

    localStorage.setItem(probeKey, 'mutated');
    localStorage.setItem('quizapp_ai_config', JSON.stringify({ provider: 'custom', apiKey: 'current-device-secret' }));
    await repository.delete(noteId);
    await persistLargeBanks([]);
    await QuizBackup.restore(backup, { databaseName: INK_DB_NAME });
    const restoredProbe = localStorage.getItem(probeKey);
    const restoredNote = await repository.get(noteId);
    const restoredBank = (await readLargeBanks([bankId]))[0] || null;
    const restoredAiConfig = JSON.parse(localStorage.getItem('quizapp_ai_config') || '{}');

    localStorage.setItem(probeKey, 'before-failed-restore');
    await repository.put(QuizNotebook.createDocument({ id: rollbackNoteId, title: 'Rollback snapshot note', kind: 'free' }));
    const originalSetItem = Storage.prototype.setItem;
    let injected = false;
    Storage.prototype.setItem = function setItemWithOneFailure(key, value) {
      if (!injected && String(key).startsWith('quizapp_')) {
        injected = true;
        Storage.prototype.setItem = originalSetItem;
        throw new DOMException('Injected storage failure', 'QuotaExceededError');
      }
      return originalSetItem.call(this, key, value);
    };
    let rollbackError = '';
    try {
      await QuizBackup.restore(backup, { databaseName: INK_DB_NAME });
    } catch(error) {
      rollbackError = error.message || String(error);
    } finally {
      Storage.prototype.setItem = originalSetItem;
    }
    const rollbackNote = await repository.get(rollbackNoteId);
    const rollbackProbe = localStorage.getItem(probeKey);

    await repository.delete(noteId).catch(() => {});
    await repository.delete(rollbackNoteId).catch(() => {});
    await persistLargeBanks([]);
    localStorage.removeItem(probeKey);

    return {
      valid: inspection.valid,
      secretExcluded: !Object.hasOwn(backedAiConfig, 'apiKey'),
      corruptedRejected: !corruptedInspection.valid,
      restoredProbe,
      restoredNote: Boolean(restoredNote),
      restoredBank: Boolean(restoredBank),
      currentSecretPreserved: restoredAiConfig.apiKey === 'current-device-secret',
      rollbackError,
      rollbackNote: Boolean(rollbackNote),
      rollbackProbe,
      storeCounts: inspection.counts.recordCounts,
      backup,
      inspection,
    };
  });

  assert.equal(result.valid, true);
  assert.equal(result.secretExcluded, true);
  assert.equal(result.corruptedRejected, true);
  assert.equal(result.restoredProbe, 'original');
  assert.equal(result.restoredNote, true);
  assert.equal(result.restoredBank, true);
  assert.equal(result.currentSecretPreserved, true);
  assert.match(result.rollbackError, /已回滚原数据/);
  assert.equal(result.rollbackNote, true);
  assert.equal(result.rollbackProbe, 'before-failed-restore');

  await page.evaluate(() => openSettings());
  await page.locator('.settings-page').waitFor({ state: 'visible' });
  await page.locator('.backup-card').scrollIntoViewIfNeeded();
  await page.evaluate(() => document.querySelectorAll('.app-dialog-mask').forEach(element => element.remove()));
  await page.screenshot({ path: 'output/playwright/backup-settings-desktop.png', fullPage: true });

  await page.setViewportSize({ width: 1180, height: 820 });
  await page.evaluate(() => renderSettings());
  await page.locator('.backup-card').scrollIntoViewIfNeeded();
  await page.evaluate(() => document.querySelectorAll('.app-dialog-mask').forEach(element => element.remove()));
  await page.getByRole('button', { name: '导出完整备份' }).click();
  await page.locator('.app-dialog').waitFor({ state: 'visible' });
  await page.screenshot({ path: 'output/playwright/backup-export-tablet.png', fullPage: true });
  await page.locator('.app-dialog-actions .secondary').click();

  await page.setViewportSize({ width: 390, height: 844 });
  await page.evaluate(() => renderSettings());
  await page.evaluate(() => document.querySelectorAll('.app-dialog-mask').forEach(element => element.remove()));
  await page.locator('#backupFileInput').setInputFiles({
    name: 'QuizApp-backup-test.quizbackup',
    mimeType: 'application/json',
    buffer: Buffer.from(JSON.stringify(result.backup), 'utf8'),
  });
  await page.locator('.app-dialog').waitFor({ state: 'visible' });
  await page.getByRole('heading', { name: '恢复备份预览' }).waitFor({ state: 'visible' });
  await page.screenshot({ path: 'output/playwright/backup-restore-mobile.png', fullPage: true });

  assert.deepEqual(pageErrors, []);
  console.log(JSON.stringify({
    checksumValidation: true,
    defaultSecretExclusion: true,
    transactionalRestore: true,
    rollbackAfterInjectedFailure: true,
    responsiveScreenshots: true,
  }, null, 2));
} finally {
  await browser.close();
}
