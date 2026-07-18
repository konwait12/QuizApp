import { createReadStream, mkdirSync, statSync } from 'node:fs';
import { createServer } from 'node:http';
import { dirname, extname, join, normalize, relative, resolve } from 'node:path';
import { spawn } from 'node:child_process';
import { fileURLToPath } from 'node:url';

const root = resolve(dirname(fileURLToPath(import.meta.url)), '..');
const bankDirectory = resolve(root, 'data');
const port = Number(process.env.QUIZAPP_DEV_PORT || process.argv[2] || 8139);
const mimeTypes = new Map([
  ['.css', 'text/css; charset=utf-8'],
  ['.html', 'text/html; charset=utf-8'],
  ['.js', 'text/javascript; charset=utf-8'],
  ['.json', 'application/json; charset=utf-8'],
  ['.mjs', 'text/javascript; charset=utf-8'],
  ['.png', 'image/png'],
  ['.svg', 'image/svg+xml'],
  ['.wasm', 'application/wasm'],
]);

function openBankDirectory() {
  mkdirSync(bankDirectory, { recursive: true });
  const command = process.platform === 'win32' ? 'explorer.exe' : process.platform === 'darwin' ? 'open' : 'xdg-open';
  const child = spawn(command, [bankDirectory], { detached: true, stdio: 'ignore', windowsHide: true });
  child.once('error', () => {});
  child.unref();
}

createServer((request, response) => {
  try {
    const pathname = decodeURIComponent(new URL(request.url || '/', 'http://localhost').pathname);
    if (pathname === '/__quizapp/open-bank-directory') {
      openBankDirectory();
      response.writeHead(200, {
        'Cache-Control': 'no-store',
        'Content-Type': 'text/plain; charset=utf-8',
      }).end('已打开默认题库文件夹：./data/');
      return;
    }
    const candidate = normalize(join(root, pathname === '/' ? 'index.html' : pathname.slice(1)));
    const fromRoot = relative(root, candidate);
    if (fromRoot.startsWith('..') || fromRoot.includes(`..${process.platform === 'win32' ? '\\' : '/'}`)) {
      response.writeHead(403).end('Forbidden');
      return;
    }
    const info = statSync(candidate);
    const file = info.isDirectory() ? join(candidate, 'index.html') : candidate;
    response.writeHead(200, {
      'Cache-Control': 'no-store',
      'Content-Type': mimeTypes.get(extname(file).toLowerCase()) || 'application/octet-stream',
    });
    createReadStream(file).pipe(response);
  } catch {
    response.writeHead(404, {'Content-Type': 'text/plain; charset=utf-8'}).end('Not found');
  }
}).listen(port, '127.0.0.1', () => {
  process.stdout.write(`QuizApp dev server: http://127.0.0.1:${port}/\n`);
});
