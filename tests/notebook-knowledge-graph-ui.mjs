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
const page = await browser.newPage({ viewport: { width: 1180, height: 820 } });
const errors = [];
page.on('pageerror', error => errors.push(error.message));

try {
  await page.addInitScript(() => {
    localStorage.setItem('quizapp_ui_config', JSON.stringify({ autoUpdateCheck: false, autoAnnouncementCheck: false }));
    localStorage.setItem('quizapp_announcement_suppressed', '1');
  });
  await page.goto(appUrl, { waitUntil: 'domcontentloaded' });
  await page.waitForFunction(() => Boolean(window.cytoscape && window.QuizNotebookKnowledgeGraph));
  await page.waitForFunction(() => typeof getVisibleBanks === 'function' && getVisibleBanks().length > 0);
  const fixture = await page.evaluate(async () => {
    document.querySelectorAll('.app-dialog-mask').forEach(element => element.remove());
    await openHandwritingPractice([], { freeMode: true });
    const first = state.notebookSession.document;
    first.title = '力学总览';
    first.tags = ['力学', '重点'];
    const second = QuizNotebook.createDocument({ title: '动量笔记', kind: 'free', tags: ['力学'] });
    const question = buildQuestionSet(getVisibleBanks())[0];
    first.links = [
      { type: 'notebook', target: second.id, label: second.title },
      { type: 'question', target: questionKey(question), label: String(question.q || '题目').slice(0, 80) },
      { type: 'url', target: 'https://example.com/physics', label: '力学资料' },
    ];
    await getNotebookRepository().put(first);
    await getNotebookRepository().put(second);
    await loadNotebookDocuments();
    setNotebookLeftTab('notebooks');
    return { firstId: first.id, secondId: second.id, questionKey: questionKey(question) };
  });

  await page.getByRole('button', { name: '知识图谱', exact: true }).click();
  await page.waitForFunction(() => Boolean(state.notebookGraph && state.notebookGraph.nodes().length >= 6));
  const graphData = await page.evaluate(() => {
    const graph = state.notebookGraph;
    const typeCounts = {};
    graph.nodes().forEach(node => { typeCounts[node.data('type')] = (typeCounts[node.data('type')] || 0) + 1; });
    let colored = 0;
    document.querySelectorAll('#notebookGraphCanvas canvas').forEach(canvas => {
      const pixels = canvas.getContext('2d')?.getImageData(0, 0, canvas.width, canvas.height).data || [];
      for (let index = 0; index < pixels.length; index += 4) {
        if (pixels[index + 3] > 0 && (pixels[index] < 235 || pixels[index + 1] < 235 || pixels[index + 2] < 235)) colored += 1;
      }
    });
    return { nodes: graph.nodes().length, edges: graph.edges().length, typeCounts, colored };
  });
  assert.ok(graphData.nodes >= 6 && graphData.edges >= 5);
  assert.ok(graphData.typeCounts.document >= 2);
  assert.ok(graphData.typeCounts.tag >= 2);
  assert.ok(graphData.typeCounts.question >= 1);
  assert.ok(graphData.typeCounts.resource >= 1);
  assert.ok(graphData.colored > 100);
  await page.screenshot({ path: path.join(outputDirectory, 'notebook-knowledge-graph-tablet.png'), fullPage: true });

  await page.locator('#notebookGraphSearch').fill('力学');
  await page.waitForFunction(() => /\d+/.test(document.getElementById('notebookGraphSearchStatus')?.textContent || ''));
  assert.match(await page.locator('#notebookGraphSearchStatus').innerText(), /找到/);

  await page.evaluate(secondId => {
    const node = state.notebookGraph.nodes().filter(item => item.data('type') === 'document' && item.data('target') === secondId).first();
    node.emit('tap');
  }, fixture.secondId);
  await page.waitForFunction(secondId => state.notebookGraphSelection?.target === secondId, fixture.secondId);
  assert.match(await page.locator('#notebookGraphSelection').innerText(), /动量笔记/);
  await page.locator('#notebookGraphSelection').getByRole('button', { name: '打开' }).click();
  await page.waitForFunction(secondId => !document.querySelector('.notebook-graph-dialog') && state.notebookSession?.document.id === secondId, fixture.secondId);

  await page.evaluate(() => setNotebookLeftTab('notebooks'));
  await page.getByRole('button', { name: '知识图谱', exact: true }).click();
  await page.waitForFunction(() => Boolean(state.notebookGraph));
  await page.setViewportSize({ width: 390, height: 844 });
  await page.evaluate(() => { state.notebookGraph?.resize().fit(undefined, 24); });
  const dialogBox = await page.locator('.notebook-graph-dialog').boundingBox();
  assert.ok(dialogBox && dialogBox.x >= 0 && dialogBox.x + dialogBox.width <= 391 && dialogBox.height <= 845);
  await page.screenshot({ path: path.join(outputDirectory, 'notebook-knowledge-graph-mobile.png'), fullPage: true });
  await page.evaluate(() => closeAppDialog());
  assert.equal(await page.evaluate(() => state.notebookGraph), null);
  assert.deepEqual(errors, []);

  await page.evaluate(async ids => {
    for (const id of ids) {
      await getNotebookAssetRepository().deleteByDocument(id);
      await getNotebookRepository().delete(id);
    }
  }, [fixture.firstId, fixture.secondId]);
  console.log(JSON.stringify({
    graphNodesAndEdges: true,
    typedNodes: true,
    canvasRendered: true,
    searchAndSelection: true,
    notebookNavigation: true,
    responsiveScreenshots: true,
    destroyOnClose: true,
  }, null, 2));
} finally {
  await browser.close();
}
