import assert from 'node:assert/strict';
import { pathToFileURL } from 'node:url';
import path from 'node:path';

globalThis.window = globalThis;
globalThis.requestAnimationFrame = () => 1;
globalThis.cancelAnimationFrame = () => {};
if (!globalThis.CustomEvent) {
  globalThis.CustomEvent = class CustomEvent extends Event {
    constructor(type, options = {}) {
      super(type);
      this.detail = options.detail;
    }
  };
}

const moduleUrl = pathToFileURL(path.resolve('notebook/speedynote-notebook.js'));
await import(`${moduleUrl.href}?test=${Date.now()}`);

const { createDocument, NotebookSession, CanvasViewport } = globalThis.QuizNotebook;
assert.equal(globalThis.QuizNotebook.SCHEMA_VERSION, 5, 'notebook schema should expose the library/body-text revision');
const legacyDocument = globalThis.QuizNotebook.normalizeDocument({ schemaVersion: 4, title: 'Legacy', pages: [{}] });
assert.equal(legacyDocument.folderId, '', 'schema 4 documents should gain an unfiled default');
assert.equal(legacyDocument.cover.mode, 'preset', 'schema 4 documents should gain a default cover');
assert.equal(legacyDocument.pages[0].bodyText, '', 'schema 4 pages should gain an empty body text field');
assert.equal(legacyDocument.lastEditorMode, '', 'schema 4 documents should not force an editor mode');
const libraryDocument = createDocument({ title: 'Library fields', folderId: 'folder:test', cover: { mode: 'image', assetId: 'cover:test' }, lastEditorMode: 'typing' });
assert.equal(libraryDocument.folderId, 'folder:test');
assert.equal(libraryDocument.cover.assetId, 'cover:test');
assert.equal(libraryDocument.lastEditorMode, 'typing');
const document = createDocument({ title: 'Notebook engine test' });
const session = new NotebookSession(document);
assert.equal(session.setPageBodyText('同页文字正文'), true);
assert.equal(session.page.bodyText, '同页文字正文');

const first = session.addObject('text', { text: 'A' }, { x: 20, y: 30, width: 120, height: 80 });
const second = session.addObject('text', { text: 'B' }, { x: 220, y: 130, width: 140, height: 90 });
session.setSelection([
  { kind: 'object', id: first.id },
  { kind: 'object', id: second.id },
]);

assert.equal(session.getSelectionItems().length, 2, 'multi-selection should retain both objects');
assert.equal(session.copySelection(), 2, 'copy should include all selected objects');
assert.equal(session.pasteSelection(24), 2, 'paste should create all copied objects');
assert.equal(session.page.objects.length, 4, 'paste should append two objects');
assert.equal(session.getSelectionItems().length, 2, 'pasted objects should become the active selection');
assert.equal(session.page.objects[2].x, first.x + 24, 'pasted object should be offset');

assert.equal(session.removeSelection(), true, 'group delete should remove the pasted selection');
assert.equal(session.page.objects.length, 2, 'group delete should leave the original objects');

const markerDocument = globalThis.QuizNotebook.normalizeDocument({
  title: 'Marker compatibility',
  pdfOutline: [{ title: 'Section', pageNumber: 1, depth: 0, sourceName: 'source.pdf' }],
  pages: [{ background: { links: [{ rect: { x: 4, y: 4, width: 30, height: 20 }, targetPage: 1 }] }, layers: [{ strokes: [{ tool: 'marker', points: [[10, 10, .5], [40, 40, .5]] }] }] }],
});
assert.equal(markerDocument.pages[0].layers[0].strokes[0].tool, 'marker', 'marker strokes should survive normalization');
assert.equal(markerDocument.pdfOutline[0].sourceName, 'source.pdf', 'PDF outline metadata should survive normalization');
assert.equal(markerDocument.pages[0].background.links.length, 1, 'PDF page links should survive normalization');

const lowerLayer = session.page.layers[0];
lowerLayer.strokes.push({ id: 'lower-stroke', tool: 'pen', color: '#000', size: 4, pointerType: 'pen', points: [{ x: 20, y: 20, pressure: .5 }], bounds: { x: 16, y: 16, width: 8, height: 8 } });
const upperLayer = session.addLayer('Upper layer');
upperLayer.strokes.push({ id: 'upper-stroke', tool: 'marker', color: '#f00', size: 8, pointerType: 'pen', points: [{ x: 60, y: 60, pressure: .5 }], bounds: { x: 52, y: 52, width: 16, height: 16 } });
assert.equal(session.mergeLayerDown(upperLayer.id), true, 'an unlocked layer should merge into the layer below');
assert.equal(session.page.layers.length, 1, 'merge should remove the source layer');
assert.deepEqual(session.page.layers[0].strokes.map(stroke => stroke.id), ['lower-stroke', 'upper-stroke'], 'merge should preserve stroke order and ids');
assert.equal(session.undo(), true, 'layer merge should be undoable');
assert.equal(session.page.layers.length, 2, 'undo should restore both layers');

session.addPage();
session.addPage();
const thirdPageId = session.document.activePageId;
assert.equal(session.movePageTo(thirdPageId, 0), true, 'page should move to an explicit index');
assert.equal(session.document.pages[0].id, thirdPageId, 'moved page should occupy the target index');

session.setPage(session.document.pages[0].id);
const rotated = session.addObject('text', { text: 'rotated' }, { x: 0, y: 0, width: 100, height: 40 });
rotated.rotation = 90;
const canvas = {
  dataset: {},
  tabIndex: -1,
  addEventListener() {},
  removeEventListener() {},
  getBoundingClientRect() { return { left: 220, top: 70, width: 800, height: 600 }; },
  getContext() { return null; },
  focus() {},
};
const viewport = new CanvasViewport(canvas, session, { getStroke: points => points });
assert.equal(viewport.hitTest({ x: 95, y: 20 }), null, 'rotated hit testing should reject points outside the rotated object');
assert.equal(viewport.hitTest({ x: 50, y: 60 })?.id, rotated.id, 'rotated hit testing should accept points inside the rotated object');
const zoomAnchor = { x: 620, y: 370 };
const pagePointBeforeZoom = viewport.screenToPage(zoomAnchor.x, zoomAnchor.y);
viewport.zoomAt(zoomAnchor.x, zoomAnchor.y, 1.8);
const pagePointAfterZoom = viewport.screenToPage(zoomAnchor.x, zoomAnchor.y);
assert.ok(Math.abs(pagePointAfterZoom.x - pagePointBeforeZoom.x) < .001, 'zoom should keep the page point under the screen anchor on X');
assert.ok(Math.abs(pagePointAfterZoom.y - pagePointBeforeZoom.y) < .001, 'zoom should keep the page point under the screen anchor on Y');
viewport.offsetX = 10000;
viewport.offsetY = -10000;
viewport.constrainViewport();
const scaledWidth = session.page.width * viewport.scale;
const scaledHeight = session.page.height * viewport.scale;
assert.ok(viewport.offsetX <= 800 - Math.min(800, scaledWidth) + viewport.panMargin, 'horizontal pan should remain within the configured boundary');
const minimumY = scaledHeight <= 600 ? -viewport.panMargin : 600 - scaledHeight - viewport.panMargin;
assert.ok(viewport.offsetY >= minimumY, 'vertical pan should remain within the configured boundary');
const polygonSelection = viewport.selectInPolygon([
  { x: -20, y: -20 },
  { x: 120, y: -20 },
  { x: 120, y: 120 },
  { x: -20, y: 120 },
]);
assert.ok(polygonSelection.some(item => item.kind === 'object' && item.id === rotated.id), 'free lasso should select objects inside its polygon');
viewport.setStraightLine(true);
viewport.setTool('marker');
assert.equal(viewport.straightLine, true, 'straight-line mode should be configurable');
assert.equal(viewport.tool, 'marker', 'marker should be a first-class drawing tool');
viewport.destroy();

const inputSession = new NotebookSession(createDocument({ title: 'Input modes' }));
const inputViewport = new CanvasViewport(canvas, inputSession, { getStroke: points => points });
const touchEvent = (pointerId, x, y) => ({ pointerType: 'touch', pointerId, clientX: x, clientY: y, button: 0, pressure: .5, timeStamp: Date.now(), preventDefault() {} });
inputViewport.setStyle({ penOnly: true });
inputViewport.pointerDown(touchEvent(1, 620, 370));
assert.equal(inputViewport.pointer?.mode, 'pan', 'pen-writing mode should use one-finger touch for panning');
assert.equal(inputSession.layer.strokes.length, 0);
inputViewport.pointerUp(touchEvent(1, 620, 370));
inputViewport.setStyle({ penOnly: false });
inputViewport.pointerDown(touchEvent(2, 620, 370));
assert.equal(inputViewport.pointer?.mode, 'draw', 'finger-writing mode should draw with one-finger touch');
inputViewport.pointerMove(touchEvent(2, 640, 390));
inputViewport.pointerUp(touchEvent(2, 650, 400));
assert.equal(inputSession.layer.strokes.length, 1);
for (const [index, penOnly] of [true, false].entries()) {
  inputViewport.setStyle({ penOnly });
  inputViewport.scale = 1;
  const firstId = 10 + index * 2;
  const secondId = firstId + 1;
  inputViewport.pointerDown(touchEvent(firstId, 480, 300));
  inputViewport.pointerDown(touchEvent(secondId, 580, 300));
  inputViewport.pointerMove(touchEvent(secondId, 680, 300));
  assert.ok(inputViewport.scale > 1, `${penOnly ? 'pen-writing' : 'finger-writing'} mode should keep two-finger pinch zoom`);
  inputViewport.pointerUp(touchEvent(secondId, 680, 300));
  inputViewport.pointerUp(touchEvent(firstId, 480, 300));
}
inputViewport.destroy();

const edgelessDocument = createDocument({ title: 'Edgeless test', mode: 'edgeless' });
assert.equal(edgelessDocument.mode, 'edgeless');
assert.ok(edgelessDocument.pages[0].width >= 3600 && edgelessDocument.pages[0].height >= 2800, 'edgeless documents should start with a usable workspace');
const edgelessSession = new NotebookSession(edgelessDocument);
const anchorObject = edgelessSession.addObject('text', { text: 'anchor' }, { x: 900, y: 700, width: 120, height: 80 });
const edgelessViewport = new CanvasViewport(canvas, edgelessSession, { getStroke: points => points });
edgelessViewport.scale = 1;
edgelessViewport.offsetX = 500;
edgelessViewport.offsetY = 420;
const widthBeforeExpansion = edgelessSession.page.width;
edgelessViewport.ensureEdgelessViewport();
assert.equal(edgelessSession.page.width, widthBeforeExpansion, 'panning should not expand the edgeless canvas');
const widthBeforeContentExpansion = edgelessSession.page.width;
edgelessViewport.ensureEdgelessBounds(widthBeforeContentExpansion + 200, 800, widthBeforeContentExpansion + 200, 800);
assert.ok(edgelessSession.page.width > widthBeforeContentExpansion, 'content beyond the right edge should expand the canvas');
assert.equal(edgelessViewport.pageContains({ x: -1000, y: -1000 }), true, 'edgeless mode should accept points beyond the current bounds');
edgelessViewport.destroy();

const modeSession = new NotebookSession(createDocument({ title: 'Mode conversion' }));
assert.equal(modeSession.setDocumentMode('edgeless'), true, 'single-page notebooks should convert to edgeless mode');
assert.equal(modeSession.document.mode, 'edgeless');
assert.equal(modeSession.undo(), true, 'mode conversion should be undoable');
assert.equal(modeSession.document.mode, 'paged');

console.log(JSON.stringify({
  multiSelect: true,
  schema5Compatibility: true,
  pageBodyText: true,
  copyPaste: true,
  groupDelete: true,
  pageReorder: true,
  rotatedHitTest: true,
  anchoredZoom: true,
  boundedPan: true,
  markerTool: true,
  straightLine: true,
  polygonLasso: true,
  mergeLayerDown: true,
  edgelessExpansion: true,
  edgelessPanDoesNotExpand: true,
  touchInputModes: true,
  twoFingerPinchModes: true,
  documentModeUndo: true,
}, null, 2));
