import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import { createRequire } from 'node:module';
import { pathToFileURL } from 'node:url';

const nodeModules = process.env.PLAYWRIGHT_NODE_MODULES;
if (!nodeModules) throw new Error('Set PLAYWRIGHT_NODE_MODULES to the bundled node_modules directory');
const require = createRequire(pathToFileURL(path.join(nodeModules, 'quizapp-playwright-resolver.cjs')));
const { chromium } = require('playwright');
const appUrl = process.env.QUIZAPP_URL || 'http://127.0.0.1:8160/';
const outputDirectory = path.resolve('output/playwright');
fs.mkdirSync(outputDirectory, { recursive: true });

const browser = await chromium.launch({
  headless: true,
  ...(process.env.PLAYWRIGHT_CHROMIUM_EXECUTABLE ? { executablePath: process.env.PLAYWRIGHT_CHROMIUM_EXECUTABLE } : {}),
});
const page = await browser.newPage({ viewport: { width: 390, height: 844 } });
const errors = [];
page.on('pageerror', error => errors.push(error.message));

async function assertCustomChoices(label) {
  assert.equal(await page.locator('select').count(), 0, `${label} should not expose native select controls`);
  assert.ok(await page.locator('.choice-select').count() > 0, `${label} should render custom choices`);
}

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
  await page.waitForFunction(() => getVisibleBanks().length > 0 && typeof renderChoiceSelect === 'function');
  await page.evaluate(() => document.querySelectorAll('.app-dialog-mask').forEach(element => element.remove()));

  await page.evaluate(() => renderSettings());
  await assertCustomChoices('settings and OCR');
  assert.ok(await page.locator('[data-choice-id="ocrLanguage"]').count() === 1);

  await page.evaluate(() => renderAiSettings());
  await assertCustomChoices('AI settings');
  assert.ok(await page.locator('[data-choice-id="aiProvider"]').count() === 1);

  await page.evaluate(() => openExamSetup());
  await assertCustomChoices('exam setup');
  assert.equal(await page.locator('.exam-field .choice-select').count(), 3);

  await page.evaluate(() => {
    const bank = getVisibleBanks()[0];
    const item = {
      name: bank.name,
      path: getBankPath(bank),
      questionCount: bank.questions.length,
    };
    const app = document.getElementById('app');
    app.innerHTML = `<div class="app-dialog" style="margin:20px auto">${renderReleaseBankRows([item])}</div>`;
    wireReleaseBankTree(app);
  });
  await assertCustomChoices('bank release conflict choice');
  const bankChoice = page.locator('[data-choice-id="releaseBankAction0"]');
  await bankChoice.locator('.choice-trigger').click();
  await bankChoice.locator('.choice-menu').waitFor({ state: 'visible' });
  assert.equal(await bankChoice.locator('.choice-option').count(), 2);
  assert.equal(await bankChoice.locator('.choice-trigger').getAttribute('aria-expanded'), 'true');
  assert.equal(await bankChoice.locator('.choice-option.active').getAttribute('aria-selected'), 'true');
  const arrow = await bankChoice.locator('.chevron').evaluate(element => {
    const before = getComputedStyle(element, '::before');
    const after = getComputedStyle(element, '::after');
    return {
      beforeWidth: before.width,
      beforeHeight: before.height,
      afterWidth: after.width,
      afterHeight: after.height,
    };
  });
  assert.deepEqual(arrow, {
    beforeWidth: '8px',
    beforeHeight: '2px',
    afterWidth: '8px',
    afterHeight: '2px',
  });
  await page.screenshot({ path: path.join(outputDirectory, 'bank-choice-menu-mobile.png'), fullPage: true });
  await bankChoice.locator('.choice-option[data-choice-value="copy"]').click();
  assert.equal(await page.locator('#releaseBankAction0').inputValue(), 'copy');

  await page.setViewportSize({ width: 1280, height: 800 });
  await bankChoice.locator('.choice-trigger').click();
  await page.screenshot({ path: path.join(outputDirectory, 'bank-choice-menu-tablet.png'), fullPage: true });
  assert.deepEqual(errors, []);
  console.log(JSON.stringify({
    nativeSelectsRemaining: 0,
    bankReleaseChoice: true,
    examChoices: true,
    aiProviderChoice: true,
    ocrChoices: true,
    responsiveScreenshots: true,
  }, null, 2));
} finally {
  await browser.close();
}
