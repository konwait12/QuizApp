#!/usr/bin/env node

import fs from 'node:fs/promises';
import path from 'node:path';
import process from 'node:process';
import { fileURLToPath } from 'node:url';

const SOURCE_PAGE = 'https://xiaoyivip.com.cn/#/yuge-question-bank';
const SOURCE_ORIGIN = new URL(SOURCE_PAGE).origin;
const SCRIPT_DIR = path.dirname(fileURLToPath(import.meta.url));
const PROJECT_ROOT = path.resolve(SCRIPT_DIR, '..');
const DEFAULT_OUTPUT = path.join(PROJECT_ROOT, 'output', 'xiaoyi-question-banks');

function parseArgs(argv) {
  const result = { output: DEFAULT_OUTPUT, includeRestricted: false };
  for (let index = 0; index < argv.length; index += 1) {
    const item = argv[index];
    if (item === '--output' && argv[index + 1]) result.output = path.resolve(argv[++index]);
    if (item === '--include-restricted') result.includeRestricted = true;
  }
  return result;
}

function safeName(value) {
  return String(value || '未命名')
    .replace(/[<>:"/\\|?*\u0000-\u001f]/g, '_')
    .replace(/[. ]+$/g, '')
    .slice(0, 100) || '未命名';
}

function decodeJwtPayload(token) {
  try {
    const payload = token.split('.')[1].replace(/-/g, '+').replace(/_/g, '/');
    return JSON.parse(Buffer.from(payload, 'base64').toString('utf8'));
  } catch {
    return null;
  }
}

async function discoverApiConfig() {
  if (process.env.XIAOYI_API_BASE && process.env.XIAOYI_API_KEY) {
    return { apiBase: process.env.XIAOYI_API_BASE.replace(/\/$/, ''), apiKey: process.env.XIAOYI_API_KEY };
  }

  const html = await fetch(SOURCE_ORIGIN).then(response => {
    if (!response.ok) throw new Error(`无法读取来源页面：HTTP ${response.status}`);
    return response.text();
  });
  const scriptUrls = [...html.matchAll(/<script[^>]+src=["']([^"']+)["']/gi)]
    .map(match => new URL(match[1], SOURCE_ORIGIN).href);
  const scripts = await Promise.all(scriptUrls.map(async url => {
    const response = await fetch(url);
    return response.ok ? response.text() : '';
  }));
  const source = scripts.join('\n');
  const projectApiBase = source.match(/https:\/\/backend\.appmiaoda\.com\/projects\/[A-Za-z0-9_-]+/)?.[0];
  const apiBase = projectApiBase ? `${projectApiBase}/rest/v1` : '';
  const tokens = [...source.matchAll(/eyJ[A-Za-z0-9_-]+\.[A-Za-z0-9_-]+\.[A-Za-z0-9_-]+/g)].map(match => match[0]);
  const apiKey = tokens.find(token => decodeJwtPayload(token)?.role === 'anon');
  if (!apiBase || !apiKey) {
    throw new Error('无法从来源页面发现公开 API。可通过 XIAOYI_API_BASE 和 XIAOYI_API_KEY 提供当前公开配置。');
  }
  return { apiBase, apiKey };
}

function parseImageList(value) {
  if (Array.isArray(value)) return value.filter(item => typeof item === 'string' && item.startsWith('data:image/'));
  if (typeof value !== 'string' || !value.trim()) return [];
  if (value.startsWith('data:image/')) return [value];
  try {
    const parsed = JSON.parse(value);
    return Array.isArray(parsed) ? parsed.filter(item => typeof item === 'string' && item.startsWith('data:image/')) : [];
  } catch {
    return [];
  }
}

function parseAnswer(row) {
  const match = String(row.question_text || '').match(/答案\s*[:：]\s*([A-H]+)/i);
  return match ? match[1].toUpperCase().split('').sort().join('') : '';
}

function questionType(sectionName, answer) {
  if (answer) return answer.length > 1 ? 'multi' : 'single';
  if (String(sectionName).includes('选择')) return 'single';
  if (String(sectionName).includes('判断')) return 'bool';
  return 'subjective';
}

function convertQuestion(row, context) {
  const answer = parseAnswer(row);
  const type = questionType(context.section.name, answer);
  const questionImages = parseImageList(row.question_image_url);
  const answerImages = parseImageList(row.answer_image_url);
  const sourceAnswerText = String(row.answer_text || '').trim();
  const builtinText = sourceAnswerText && sourceAnswerText !== '(答案见图片)' ? sourceAnswerText : '';
  return {
    id: `xiaoyi:${row.id}`,
    type,
    q: `第 ${row.question_number} 题${questionImages.length ? '（题目见图片）' : ''}`,
    options: type === 'single' || type === 'multi' ? ['A', 'B', 'C', 'D'] : [],
    ans: answer,
    questionImages,
    explanations: {
      builtin: {
        label: '内置解析',
        text: builtinText,
        images: answerImages,
        videoUrl: row.video_url || '',
        source: {
          provider: 'xiaoyivip',
          page: SOURCE_PAGE,
          sourceId: row.id,
          importedAt: new Date().toISOString(),
        },
      },
    },
    source: {
      provider: 'xiaoyivip',
      type: context.type.name,
      chapter: context.chapter.name,
      section: context.section.name,
      sourceId: row.id,
    },
  };
}

async function main() {
  const args = parseArgs(process.argv.slice(2));
  if (args.includeRestricted) {
    throw new Error('导出器不会绕过 requires_activation 访问限制。请在来源平台使用其正式授权方式。');
  }
  const { apiBase, apiKey } = await discoverApiConfig();
  const headers = { apikey: apiKey, authorization: `Bearer ${apiKey}`, 'accept-profile': 'public' };
  const get = async resource => {
    const response = await fetch(`${apiBase}/${resource}`, { headers });
    if (!response.ok) throw new Error(`来源 API 请求失败：HTTP ${response.status} ${resource}`);
    return response.json();
  };

  await fs.mkdir(args.output, { recursive: true });
  const types = await get('yuge_question_bank_types?select=*&order=display_order.asc');
  const report = {
    schemaVersion: 1,
    source: SOURCE_PAGE,
    exportedAt: new Date().toISOString(),
    publicBanks: [],
    restrictedBanks: [],
    totals: { banks: 0, chapters: 0, sections: 0, questions: 0, bytes: 0 },
  };

  for (const type of types) {
    if (type.requires_activation) {
      report.restrictedBanks.push({ id: type.id, name: type.name, reason: 'requires_activation' });
      console.log(`[跳过受限题库] ${type.name}`);
      continue;
    }
    const chapters = await get(`yuge_question_bank_chapters?select=*&type_id=eq.${type.id}&is_enabled=eq.true&order=display_order.asc`);
    if (!chapters.length) continue;
    const typeReport = { id: type.id, name: type.name, chapters: [], questionCount: 0 };
    for (const chapter of chapters) {
      const sections = await get(`yuge_question_bank_sections?select=*&chapter_id=eq.${chapter.id}&is_enabled=eq.true&order=display_order.asc`);
      const chapterReport = { id: chapter.id, name: chapter.name, sections: [], questionCount: 0 };
      for (const section of sections) {
        const rows = await get(`yuge_question_bank_questions?select=*&section_id=eq.${section.id}&is_enabled=eq.true&order=question_number.asc`);
        const questions = rows.map(row => convertQuestion(row, { type, chapter, section }));
        const relativeFile = path.join(safeName(type.name), safeName(chapter.name), `${safeName(section.name)}.json`);
        const absoluteFile = path.join(args.output, relativeFile);
        const bank = {
          schemaVersion: 2,
          id: `xiaoyi:${section.id}`,
          name: `${type.name}-${chapter.name}-${section.name}`,
          subject: '考研数学',
          chapter: type.name,
          path: ['考研数学', type.name, chapter.name, section.name],
          source: {
            provider: 'xiaoyivip',
            page: SOURCE_PAGE,
            sourceTypeId: type.id,
            sourceChapterId: chapter.id,
            sourceSectionId: section.id,
            importedAt: new Date().toISOString(),
            notice: '仅导出来源站点公开可访问内容；内置解析与 AI 分析分开存储。',
          },
          questions,
        };
        await fs.mkdir(path.dirname(absoluteFile), { recursive: true });
        const json = `${JSON.stringify(bank, null, 2)}\n`;
        await fs.writeFile(absoluteFile, json, 'utf8');
        const bytes = Buffer.byteLength(json);
        chapterReport.sections.push({ id: section.id, name: section.name, file: relativeFile.replace(/\\/g, '/'), questionCount: questions.length, bytes });
        chapterReport.questionCount += questions.length;
        typeReport.questionCount += questions.length;
        report.totals.sections += 1;
        report.totals.questions += questions.length;
        report.totals.bytes += bytes;
        console.log(`[已导出] ${type.name} / ${chapter.name} / ${section.name}：${questions.length} 题`);
      }
      chapterReport.sections.length && typeReport.chapters.push(chapterReport);
      report.totals.chapters += 1;
    }
    report.publicBanks.push(typeReport);
    report.totals.banks += 1;
  }

  await fs.writeFile(path.join(args.output, 'export-report.json'), `${JSON.stringify(report, null, 2)}\n`, 'utf8');
  await fs.writeFile(path.join(args.output, 'README.md'), `# 27考研题库包\n\n公开数据来源：${SOURCE_PAGE}\n\n- 只包含来源站点标记为公开、无需激活的题库。\n- 内部来源标识仍为 \`xiaoyivip\`，用于更新兼容和数据校验。\n- 标记为 \`requires_activation\` 的题库不会尝试访问或导出。\n- \`explanations.builtin\` 是来源题库自带答案/解析。\n- AI 分析不写入这些 JSON，仍由 QuizApp 单独保存在本机。\n- 导出统计和受限题库列表见 \`export-report.json\`。\n`, 'utf8');
  console.log(`完成：${report.totals.banks} 个公开题库，${report.totals.questions} 题，${(report.totals.bytes / 1024 / 1024).toFixed(1)} MB`);
  console.log(`目录：${args.output}`);
}

main().catch(error => {
  console.error(error.stack || error.message || error);
  process.exitCode = 1;
});
