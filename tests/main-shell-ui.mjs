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
const page = await browser.newPage({ viewport: { width: 1440, height: 900 } });
const pageErrors = [];
page.on('pageerror', error => pageErrors.push(error.message));

async function assertNoHorizontalOverflow(label) {
  const metrics = await page.evaluate(() => ({
    documentWidth: document.documentElement.scrollWidth,
    viewportWidth: document.documentElement.clientWidth,
    bodyWidth: document.body.scrollWidth,
  }));
  assert.ok(metrics.documentWidth <= metrics.viewportWidth + 1, `${label} document should not overflow horizontally`);
  assert.ok(metrics.bodyWidth <= metrics.viewportWidth + 1, `${label} body should not overflow horizontally`);
}

try {
  await page.goto(appUrl, { waitUntil: 'domcontentloaded' });
  await page.waitForFunction(() => Boolean(window.QuizAppShell && getVisibleBanks().length));
  await page.evaluate(() => {
    document.querySelectorAll('.app-dialog-mask').forEach(element => element.remove());
    state.uiConfig.showSavedProgressHint = false;
    setMainTab('home');
  });
  assert.equal(await page.locator('.main-nav-button').count(), 4);
  await assertNoHorizontalOverflow('desktop home');
  await page.screenshot({ path: 'output/playwright/main-shell-desktop.png', fullPage: true });

  for (const tab of ['library', 'study', 'profile']) {
    await page.evaluate(value => setMainTab(value), tab);
    assert.equal(await page.locator('.main-nav-button.active').count(), 1);
    assert.equal(await page.locator('.main-nav-button.active').getAttribute('aria-label'), tab === 'library' ? '题库' : tab === 'study' ? '学习' : '我的');
    await assertNoHorizontalOverflow(`desktop ${tab}`);
  }

  await page.evaluate(() => openSettings());
  await page.locator('.settings-page').waitFor({ state: 'visible' });
  await assertNoHorizontalOverflow('desktop settings');
  await page.screenshot({ path: 'output/playwright/settings-desktop.png', fullPage: true });

  await page.setViewportSize({ width: 1280, height: 800 });
  await page.evaluate(() => { state.mainTab = 'home'; renderHome(); });
  await assertNoHorizontalOverflow('tablet home');
  await page.screenshot({ path: 'output/playwright/main-shell-tablet-current.png', fullPage: true });

  await page.setViewportSize({ width: 390, height: 844 });
  await page.evaluate(() => renderHome());
  await assertNoHorizontalOverflow('mobile home');
  await page.screenshot({ path: 'output/playwright/main-shell-mobile-current.png', fullPage: true });

  await page.evaluate(() => startAllPractice(false, true));
  await page.locator('.quiz-topbar').waitFor({ state: 'visible' });
  await assertNoHorizontalOverflow('mobile quiz');
  await page.screenshot({ path: 'output/playwright/quiz-mobile-current.png', fullPage: true });
  assert.deepEqual(pageErrors, []);
  console.log(JSON.stringify({
    fourMainTabs: true,
    desktop: true,
    tablet: true,
    mobile: true,
    settings: true,
    quiz: true,
    horizontalOverflow: false,
  }, null, 2));
} finally {
  await browser.close();
}
