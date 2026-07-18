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

async function auditVisibleContrast(label) {
  const failures = await page.evaluate(() => {
    const parseColor = value => {
      const match = value.match(/rgba?\(([^)]+)\)/);
      if (!match) return null;
      const parts = match[1].split(/[ ,/]+/).map(Number);
      return { r: parts[0], g: parts[1], b: parts[2], a: Number.isFinite(parts[3]) ? parts[3] : 1 };
    };
    const composite = (front, back) => ({
      r: front.r * front.a + back.r * (1 - front.a),
      g: front.g * front.a + back.g * (1 - front.a),
      b: front.b * front.a + back.b * (1 - front.a),
      a: 1,
    });
    const backgroundFor = element => {
      const ancestors = [];
      for (let node = element; node; node = node.parentElement) ancestors.push(node);
      return ancestors.reverse().reduce((background, node) => {
        const color = parseColor(getComputedStyle(node).backgroundColor);
        return color?.a ? composite(color, background) : background;
      }, { r: 255, g: 255, b: 255, a: 1 });
    };
    const luminance = color => {
      const channel = value => {
        const normalized = value / 255;
        return normalized <= 0.04045 ? normalized / 12.92 : ((normalized + 0.055) / 1.055) ** 2.4;
      };
      return 0.2126 * channel(color.r) + 0.7152 * channel(color.g) + 0.0722 * channel(color.b);
    };
    const contrast = (front, back) => {
      const first = luminance(front);
      const second = luminance(back);
      return (Math.max(first, second) + 0.05) / (Math.min(first, second) + 0.05);
    };

    return [...document.querySelectorAll('body *')]
      .filter(element => {
        if (!element.getClientRects().length || getComputedStyle(element).visibility === 'hidden') return false;
        const hasOwnText = [...element.childNodes].some(node => node.nodeType === Node.TEXT_NODE && node.textContent.trim());
        const textControl = element.matches('input:not([type="checkbox"]):not([type="radio"]):not([type="range"]):not([type="color"]),select,textarea');
        return hasOwnText || textControl || element.matches('.icon,.menu-icon');
      })
      .map(element => {
        const style = getComputedStyle(element);
        const foreground = parseColor(style.color);
        const background = backgroundFor(element);
        if (!foreground) return null;
        return {
          text: (element.innerText || element.getAttribute('aria-label') || '').trim().slice(0, 40),
          selector: `${element.tagName.toLowerCase()}.${String(element.className || '').trim().replace(/\s+/g, '.')}`,
          ratio: contrast(composite(foreground, background), background),
        };
      })
      .filter(item => item && item.ratio < 4.5);
  });
  assert.deepEqual(failures, [], `${label} has low-contrast visible controls: ${JSON.stringify(failures.slice(0, 8))}`);
}

async function auditControlLegibility(label) {
  const failures = await page.evaluate(() => [...document.querySelectorAll(
    '.icon,.menu-icon,.btn-back,.btn-icon,.notebook-tool,.notebook-icon-action,.notebook-page-actions button,.choice-trigger .chevron'
  )].filter(element => element.getClientRects().length && getComputedStyle(element).visibility !== 'hidden').map(element => {
    const style = getComputedStyle(element);
    return {
      selector: `${element.tagName.toLowerCase()}.${String(element.className || '').trim().replace(/\s+/g, '.')}`,
      text: (element.textContent || element.getAttribute('aria-label') || '').trim().slice(0, 20),
      parent: String(element.parentElement?.className || '').trim().replace(/\s+/g, '.'),
      fontSize: Number.parseFloat(style.fontSize),
      opacity: Number.parseFloat(style.opacity),
    };
  }).filter(item => item.fontSize < 12 || item.opacity < 0.6));
  assert.deepEqual(failures, [], `${label} has visually weak icons: ${JSON.stringify(failures.slice(0, 8))}`);
}

async function auditPage(label, screenshot) {
  await auditVisibleContrast(label);
  await auditControlLegibility(label);
  assert.ok(await page.evaluate(() => document.documentElement.scrollWidth - document.documentElement.clientWidth <= 1), `${label} overflows horizontally`);
  await page.screenshot({ path: screenshot });
}

try {
  await page.addInitScript(() => {
    localStorage.setItem('quizapp_ui_config', JSON.stringify({
      systemTheme: 'endfield',
      autoUpdateCheck: false,
      autoAnnouncementCheck: false,
      autoBankUpdateCheck: false,
    }));
    localStorage.setItem('quizapp_announcement_suppressed', '1');
  });
  await page.goto(appUrl, { waitUntil: 'domcontentloaded' });
  await page.waitForFunction(() => typeof applyUiConfig === 'function' && !state.loadingDefaults && getVisibleBanks().length > 0);
  await page.evaluate(() => document.querySelectorAll('.app-dialog-mask').forEach(element => element.remove()));

  const themeValues = await page.evaluate(() => {
    const style = getComputedStyle(document.body);
    return {
      theme: document.body.dataset.systemTheme,
      surfaceSoft: style.getPropertyValue('--surface-soft').trim(),
      primarySoft: style.getPropertyValue('--primary-soft').trim(),
      text: style.getPropertyValue('--text').trim(),
    };
  });
  assert.equal(themeValues.theme, 'endfield');
  assert.equal(themeValues.surfaceSoft, '#232429');
  assert.equal(themeValues.primarySoft, 'rgba(253,252,0,.14)');
  assert.equal(themeValues.text, '#f4f4f2');

  await page.evaluate(() => { state.mainTab = 'library'; state.view = 'home'; renderHome(); });
  await auditPage('library', 'output/playwright/endfield-library-tablet.png');
  const activeNavColors = await page.locator('.main-nav-button.active .icon').evaluate(element => ({
    color: getComputedStyle(element).color,
    background: getComputedStyle(element).backgroundColor,
  }));
  assert.equal(activeNavColors.color, 'rgb(16, 17, 19)');
  assert.equal(activeNavColors.background, 'rgba(0, 0, 0, 0)');

  await page.evaluate(() => setMainTab('home'));
  await auditPage('home', 'output/playwright/endfield-home-tablet.png');

  await page.evaluate(() => setMainTab('profile'));
  await auditPage('profile', 'output/playwright/endfield-profile-tablet.png');

  await page.evaluate(() => setMainTab('study'));
  await auditPage('study', 'output/playwright/endfield-study-tablet.png');
  await page.evaluate(() => renderStudyStatsPage());
  await auditPage('statistics', 'output/playwright/endfield-statistics-tablet.png');
  await page.evaluate(() => renderSettings());
  await auditPage('settings', 'output/playwright/endfield-settings-tablet.png');

  await page.evaluate(() => {
    const group = getSubjectGroups()[0];
    openSubject(group.subject);
  });
  await auditPage('subject', 'output/playwright/endfield-subject-tablet.png');

  await page.evaluate(() => openAnswerLookup(getSubjectGroups()[0].subject, ''));
  await page.locator('.answer-list').waitFor({ state: 'visible' });
  await auditPage('answer lookup', 'output/playwright/endfield-answer-lookup-tablet.png');

  await page.evaluate(() => openAiChat());
  await page.locator('.ai-shell').waitFor({ state: 'visible' });
  await auditPage('AI chat', 'output/playwright/endfield-ai-chat-tablet.png');

  await page.evaluate(() => openAiSettings());
  await page.locator('.ai-settings-page').waitFor({ state: 'visible' });
  await auditPage('AI settings', 'output/playwright/endfield-ai-settings-tablet.png');

  await page.evaluate(() => openExamSetup());
  await page.locator('.exam-setup-page').waitFor({ state: 'visible' });
  await auditPage('exam setup', 'output/playwright/endfield-exam-setup-tablet.png');

  await page.evaluate(() => openAnnouncementBoard());
  await page.locator('.announcement-dialog').waitFor({ state: 'visible' });
  await auditPage('announcements', 'output/playwright/endfield-announcements-tablet.png');
  await page.evaluate(() => closeAppDialog());

  await page.evaluate(() => {
    const group = getSubjectGroups()[0];
    startPractice({
      subject: group.subject,
      banks: group.banks,
      title: group.subject,
      subtitle: 'Endfield visual test',
      shuffled: false,
      path: [group.subject],
      returnPath: null,
    });
  });
  await page.locator('.quiz-topbar').waitFor({ state: 'visible' });
  await auditPage('quiz', 'output/playwright/endfield-quiz-tablet.png');
  await page.evaluate(() => showQuestionOverview());
  await page.locator('.overview-panel').waitFor({ state: 'visible' });
  await auditPage('question overview', 'output/playwright/endfield-overview-tablet.png');
  await page.evaluate(() => closeQuestionOverview());

  await page.evaluate(async () => openFreeNotebookLibrary());
  await page.locator('.notebook-workspace').waitFor({ state: 'visible' });
  await auditPage('notebook', 'output/playwright/endfield-notebook-tablet.png');

  await page.setViewportSize({ width: 390, height: 844 });
  await auditPage('notebook mobile', 'output/playwright/endfield-notebook-mobile.png');

  await page.reload({ waitUntil: 'domcontentloaded' });
  await page.waitForFunction(() => typeof renderHome === 'function' && !state.loadingDefaults && getVisibleBanks().length > 0);
  await page.evaluate(() => {
    document.querySelectorAll('.app-dialog-mask').forEach(element => element.remove());
    state.uiConfig.systemTheme = 'endfield';
    applyUiConfig();
    state.mainTab = 'library';
    state.view = 'home';
    renderHome();
  });
  await auditPage('library mobile', 'output/playwright/endfield-library-mobile.png');

  assert.deepEqual(pageErrors, []);
  console.log(JSON.stringify({ endfieldContrast: true, tablet: true, mobile: true }, null, 2));
} finally {
  await browser.close();
}
