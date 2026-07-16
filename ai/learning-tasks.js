/* QuizApp AI learning tasks. Generated content stays separate from question banks. GPL-3.0-or-later. */
(function attachQuizAiLearning(global) {
  'use strict';

  const STORAGE_KEY = 'quizapp_ai_learning_tasks_v1';
  const TASK_TYPES = Object.freeze({
    ANALYSIS: 'question-analysis',
    SIMILAR: 'similar-question',
    WRONG_REASON: 'wrong-reason',
    NOTEBOOK_SUMMARY: 'notebook-summary',
  });
  const MAX_RECORDS = 500;

  function readStore() {
    try {
      const parsed = JSON.parse(localStorage.getItem(STORAGE_KEY) || '{}');
      return parsed && typeof parsed === 'object' && !Array.isArray(parsed) ? parsed : {};
    } catch(e) {
      return {};
    }
  }

  function writeStore(store) {
    const entries = Object.entries(store || {})
      .filter(([, value]) => value?.content)
      .sort((a, b) => Number(b[1].updatedAt || 0) - Number(a[1].updatedAt || 0))
      .slice(0, MAX_RECORDS);
    const normalized = Object.fromEntries(entries);
    try {
      localStorage.setItem(STORAGE_KEY, JSON.stringify(normalized));
    } catch(e) {
      localStorage.setItem(STORAGE_KEY, JSON.stringify(Object.fromEntries(entries.slice(0, 160))));
    }
    return normalized;
  }

  function recordId(type, key) {
    return `${String(type || '')}:${String(key || '')}`;
  }

  function get(type, key) {
    return readStore()[recordId(type, key)] || null;
  }

  function set(type, key, value) {
    const store = readStore();
    const record = {
      taskType: type,
      key: String(key || ''),
      content: String(value?.content || ''),
      parsed: value?.parsed || null,
      model: String(value?.model || ''),
      usage: value?.usage || null,
      sourceHash: String(value?.sourceHash || ''),
      capability: value?.capability || null,
      updatedAt: Number(value?.updatedAt || Date.now()),
    };
    store[recordId(type, key)] = record;
    writeStore(store);
    return record;
  }

  function remove(type, key) {
    const store = readStore();
    const id = recordId(type, key);
    if (!store[id]) return false;
    delete store[id];
    writeStore(store);
    return true;
  }

  function clear() {
    localStorage.removeItem(STORAGE_KEY);
  }

  function normalizeImages(value) {
    return Array.isArray(value) ? value.map(item => String(item || '').trim()).filter(Boolean).slice(0, 4) : [];
  }

  function isImageDependentQuestion(question = {}) {
    const images = normalizeImages(question.questionImages || question.images);
    if (!images.length) return false;
    const text = String(question.q || question.question || '').replace(/\s+/g, ' ').trim();
    const options = Array.isArray(question.options) ? question.options.filter(Boolean) : [];
    return !text || /^(?:图片题|请见图|如图|见下图)[。，,：:\s]*$/i.test(text) || (!options.length && text.length < 16);
  }

  function capabilityFor(type, input = {}, config = {}) {
    const images = normalizeImages(input.images || input.question?.questionImages);
    const visionEnabled = Boolean(config.visionEnabled);
    const imageDependent = Boolean(input.imageDependent ?? isImageDependentQuestion(input.question));
    const hasInk = Boolean(input.hasInk);
    const needsVision = (imageDependent && images.length > 0) || (type === TASK_TYPES.NOTEBOOK_SUMMARY && hasInk);
    if (needsVision && !visionEnabled) {
      return {
        allowed: false,
        visionEnabled,
        needsVision,
        includeImages: false,
        message: type === TASK_TYPES.NOTEBOOK_SUMMARY
          ? '当前页包含手写笔迹，需要在 AI 设置中启用确实支持图片的模型后再生成笔记摘要。'
          : '这道题依赖题目图片，需要在 AI 设置中启用确实支持图片的模型。',
      };
    }
    return {
      allowed: true,
      visionEnabled,
      needsVision,
      includeImages: visionEnabled && images.length > 0,
      message: '',
    };
  }

  function asUserContent(text, images, capability) {
    if (!capability.includeImages) return text;
    return [
      { type: 'text', text },
      ...normalizeImages(images).map(url => ({ type: 'image_url', image_url: { url } })),
    ];
  }

  function similarMessages(input, capability) {
    const prompt = `根据下面的题目事实生成 2 道考查同一核心知识点、但题干和数据不同的相似练习题。
严格输出 JSON，不要输出 Markdown 代码块或其他文本：
{"questions":[{"type":"single|multi|bool|subjective","question":"题干","options":["选项1","选项2"],"answer":"A","analysis":"简明解析"}]}
要求：选择题选项数量合理；答案字母与选项对应；判断题 options 使用 ["正确","错误"]；不要复制原题原句；不得改写传入的题库答案或内置解析。

题目事实：
${JSON.stringify(input.context || {}, null, 2)}`;
    return [
      { role: 'system', content: '你是 QuizApp 的相似题生成器。生成内容是 AI 练习，不是题库原题或内置解析。' },
      { role: 'user', content: asUserContent(prompt, input.images, capability) },
    ];
  }

  function wrongReasonMessages(input, capability) {
    const prompt = `请根据题目、用户错误答案、题库正确答案、用户手动错因标签和已有 AI 题目分析，归纳这次错误。
输出结构：
### 核心错因
一句话说明最可能的根因，区分概念、审题、计算、记忆或粗心。
### 答题偏差
对比用户答案和正确答案，说明错在哪一步或哪个判断。
### 下次提醒
给出 1-3 条可执行的复习或检查动作。
不要把推测写成确定事实；不要修改用户的手动标签；不要覆盖题库解析。

数据：
${JSON.stringify(input.context || {}, null, 2)}`;
    return [
      { role: 'system', content: '你是 QuizApp 的错因归纳助手。所有结论都必须标记为 AI 建议，不得改写用户数据。' },
      { role: 'user', content: asUserContent(prompt, input.images, capability) },
    ];
  }

  function notebookSummaryMessages(input, capability) {
    const prompt = `请总结当前笔记页，输出：
### 页面主题
### 核心要点
### 待确认内容
### 下一步复习
对不清楚的手写不得猜测；题库内置解析只是参考上下文，不能伪装成用户笔记。

笔记结构与文本对象：
${JSON.stringify(input.context || {}, null, 2)}`;
    return [
      { role: 'system', content: '你是 QuizApp 的笔记摘要助手。输出属于 AI 生成内容，不得改写原笔记。' },
      { role: 'user', content: asUserContent(prompt, input.images, capability) },
    ];
  }

  function extractJson(text) {
    const value = String(text || '').trim();
    const unfenced = value.replace(/^```(?:json)?\s*/i, '').replace(/\s*```$/i, '').trim();
    const start = unfenced.indexOf('{');
    const end = unfenced.lastIndexOf('}');
    if (start < 0 || end <= start) throw new Error('模型没有返回可解析的 JSON 相似题');
    return JSON.parse(unfenced.slice(start, end + 1));
  }

  function normalizeSimilarQuestions(content) {
    const payload = extractJson(content);
    const questions = Array.isArray(payload.questions) ? payload.questions : [];
    const normalized = questions.slice(0, 4).map((item, index) => {
      const type = ['single', 'multi', 'bool', 'subjective'].includes(item?.type) ? item.type : 'single';
      const options = Array.isArray(item?.options) ? item.options.map(option => String(option || '').trim()).filter(Boolean).slice(0, 8) : [];
      const question = String(item?.question || item?.q || '').trim();
      const answer = String(item?.answer || item?.ans || '').toUpperCase().replace(/[^A-H]/g, '');
      if (!question) return null;
      return {
        id: `ai-similar:${index}`,
        type,
        question,
        options: type === 'bool' && options.length < 2 ? ['正确', '错误'] : options,
        answer,
        analysis: String(item?.analysis || item?.explanation || '').trim(),
      };
    }).filter(Boolean);
    if (!normalized.length) throw new Error('模型返回的相似题列表为空');
    return { questions: normalized };
  }

  function messagesFor(type, input, capability) {
    if (type === TASK_TYPES.SIMILAR) return similarMessages(input, capability);
    if (type === TASK_TYPES.WRONG_REASON) return wrongReasonMessages(input, capability);
    if (type === TASK_TYPES.NOTEBOOK_SUMMARY) return notebookSummaryMessages(input, capability);
    throw new Error('未知 AI 学习任务');
  }

  async function run(options = {}) {
    const type = options.type;
    const key = String(options.key || '');
    const request = options.request || global.requestAiCompletion;
    if (!key) throw new Error('AI 学习任务缺少关联键');
    if (typeof request !== 'function') throw new Error('AI 请求器未加载');
    const capability = capabilityFor(type, options.input, options.config);
    if (!capability.allowed) {
      const error = new Error(capability.message);
      error.code = 'AI_VISION_REQUIRED';
      error.capability = capability;
      throw error;
    }
    const result = await request(messagesFor(type, options.input || {}, capability), {
      controller: options.controller,
      stream: type !== TASK_TYPES.SIMILAR,
      onDelta: options.onDelta,
    });
    const content = String(result?.content || '').trim();
    if (!content) throw new Error('模型没有返回文本内容');
    const parsed = type === TASK_TYPES.SIMILAR ? normalizeSimilarQuestions(content) : null;
    return set(type, key, {
      content,
      parsed,
      model: options.config?.model || '',
      usage: result?.usage || null,
      sourceHash: options.sourceHash || '',
      capability,
      updatedAt: Date.now(),
    });
  }

  global.QuizAiLearning = {
    STORAGE_KEY,
    TASK_TYPES,
    recordId,
    get,
    set,
    remove,
    clear,
    run,
    capabilityFor,
    isImageDependentQuestion,
    normalizeSimilarQuestions,
  };
})(window);
