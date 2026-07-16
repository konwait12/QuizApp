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
const document = createDocument({ title: 'Notebook engine test' });
const session = new NotebookSession(document);

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
  getBoundingClientRect() { return { left: 0, top: 0, width: 800, height: 600 }; },
  getContext() { return null; },
};
const viewport = new CanvasViewport(canvas, session, { getStroke: points => points });
assert.equal(viewport.hitTest({ x: 95, y: 20 }), null, 'rotated hit testing should reject points outside the rotated object');
assert.equal(viewport.hitTest({ x: 50, y: 60 })?.id, rotated.id, 'rotated hit testing should accept points inside the rotated object');
viewport.destroy();

console.log(JSON.stringify({
  multiSelect: true,
  copyPaste: true,
  groupDelete: true,
  pageReorder: true,
  rotatedHitTest: true,
}, null, 2));
