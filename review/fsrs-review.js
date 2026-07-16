/* QuizApp FSRS review repository. Uses ts-fsrs 5.4.1 (MIT). GPL-3.0-or-later. */
(function attachQuizReview(global) {
  'use strict';

  const STORAGE_KEY = 'quizapp_fsrs_review_cards';
  const HISTORY_LIMIT = 120;

  function getScheduler() {
    if (!global.FSRS?.fsrs) throw new Error('FSRS 调度器未加载');
    return global.FSRS.fsrs(global.FSRS.generatorParameters({
      request_retention: 0.9,
      maximum_interval: 36500,
      enable_fuzz: true,
      enable_short_term: true,
    }));
  }

  function serializeCard(card) {
    return {
      ...card,
      due: new Date(card.due).getTime(),
      last_review: card.last_review ? new Date(card.last_review).getTime() : null,
    };
  }

  function hydrateCard(card) {
    if (!card) return global.FSRS.createEmptyCard();
    return {
      ...card,
      due: new Date(card.due),
      last_review: card.last_review ? new Date(card.last_review) : undefined,
    };
  }

  function load() {
    try {
      const parsed = JSON.parse(localStorage.getItem(STORAGE_KEY) || '{}');
      return parsed && typeof parsed === 'object' ? parsed : {};
    } catch(e) {
      return {};
    }
  }

  function save(records) {
    localStorage.setItem(STORAGE_KEY, JSON.stringify(records));
  }

  function snapshotQuestion(question) {
    return {
      id: question.id || '',
      type: question.type || '',
      q: String(question.q || ''),
      options: Array.isArray(question.options) ? question.options.slice(0, 12) : [],
      ans: String(question.ans || ''),
      exp: String(question.exp || ''),
      sourceSubject: question.sourceSubject || '',
      sourceChapter: question.sourceChapter || '',
      sourcePath: Array.isArray(question.sourcePath) ? question.sourcePath : [],
    };
  }

  function add(key, question) {
    const records = load();
    if (records[key]) return records[key];
    const now = Date.now();
    records[key] = {
      key,
      question: snapshotQuestion(question),
      card: serializeCard(global.FSRS.createEmptyCard()),
      history: [],
      createdAt: now,
      updatedAt: now,
    };
    save(records);
    return records[key];
  }

  function remove(key) {
    const records = load();
    if (!records[key]) return false;
    delete records[key];
    save(records);
    return true;
  }

  function has(key) {
    return Boolean(load()[key]);
  }

  function all() {
    return Object.values(load()).sort((a, b) => Number(a.card?.due || 0) - Number(b.card?.due || 0));
  }

  function due(now = new Date()) {
    const timestamp = new Date(now).getTime();
    return all().filter(record => Number(record.card?.due || 0) <= timestamp);
  }

  function preview(key, now = new Date()) {
    const record = load()[key];
    if (!record) return null;
    return getScheduler().repeat(hydrateCard(record.card), now);
  }

  function rate(key, rating, now = new Date()) {
    const records = load();
    const record = records[key];
    if (!record) throw new Error('复习卡不存在');
    const result = getScheduler().next(hydrateCard(record.card), now, Number(rating));
    record.card = serializeCard(result.card);
    record.history = [...(record.history || []), {
      rating: Number(rating),
      review: new Date(result.log.review).getTime(),
      due: new Date(result.log.due).getTime(),
      state: result.log.state,
      scheduled_days: result.log.scheduled_days,
    }].slice(-HISTORY_LIMIT);
    record.updatedAt = Date.now();
    records[key] = record;
    save(records);
    return record;
  }

  function stats(now = new Date()) {
    const records = all();
    const dueCount = due(now).length;
    return {
      total: records.length,
      due: dueCount,
      newCards: records.filter(record => Number(record.card?.state || 0) === 0).length,
      learned: records.filter(record => Number(record.card?.state || 0) !== 0).length,
    };
  }

  global.QuizReview = {
    STORAGE_KEY,
    Rating: global.FSRS?.Rating || { Again: 1, Hard: 2, Good: 3, Easy: 4 },
    add,
    remove,
    has,
    all,
    due,
    preview,
    rate,
    stats,
    hydrateCard,
  };
})(window);
