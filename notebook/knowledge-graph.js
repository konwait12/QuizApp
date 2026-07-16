/* Notebook knowledge-graph data builder. GPL-3.0-or-later. */
(function attachQuizNotebookKnowledgeGraph(global) {
  'use strict';

  function stableHash(value) {
    let hash = 2166136261;
    for (const character of String(value || '')) {
      hash ^= character.charCodeAt(0);
      hash = Math.imul(hash, 16777619);
    }
    return (hash >>> 0).toString(36);
  }

  function trimLabel(value, max = 52) {
    const text = String(value || '').replace(/\s+/g, ' ').trim();
    return text.length > max ? `${text.slice(0, max - 1)}…` : text;
  }

  function build(documents = [], options = {}) {
    const source = (Array.isArray(documents) ? documents : [])
      .filter(document => document && document.id)
      .slice(0, Math.max(1, Number(options.maxDocuments || 260)));
    const maxNodes = Math.max(40, Number(options.maxNodes || 700));
    const nodes = new Map();
    const edges = new Map();

    function addNode(data) {
      if (!data?.id || nodes.has(data.id) || nodes.size >= maxNodes) return nodes.get(data?.id) || null;
      const element = { data };
      nodes.set(data.id, element);
      return element;
    }

    function addEdge(sourceId, targetId, relation) {
      if (!nodes.has(sourceId) || !nodes.has(targetId) || sourceId === targetId) return;
      const key = `${sourceId}|${targetId}|${relation}`;
      if (edges.has(key)) return;
      edges.set(key, {
        data: {
          id: `edge:${stableHash(key)}`,
          source: sourceId,
          target: targetId,
          relation,
        },
      });
    }

    source.forEach(document => {
      addNode({
        id: `document:${stableHash(document.id)}`,
        type: 'document',
        target: document.id,
        label: trimLabel(document.title || '未命名笔记'),
        detail: `${document.kind === 'question' ? '题目笔记' : '自由笔记'} · ${(document.pages || []).length} 页`,
      });
    });

    source.forEach(document => {
      const documentId = `document:${stableHash(document.id)}`;
      if (!nodes.has(documentId)) return;
      (document.tags || []).forEach(tag => {
        const value = String(tag || '').trim();
        if (!value) return;
        const tagId = `tag:${stableHash(value.toLowerCase())}`;
        addNode({ id: tagId, type: 'tag', target: value, label: trimLabel(value), detail: '标签' });
        addEdge(documentId, tagId, 'tag');
      });
      if (document.binding?.questionKey) {
        const target = String(document.binding.questionKey);
        const questionId = `question:${stableHash(target)}`;
        addNode({
          id: questionId,
          type: 'question',
          target,
          label: trimLabel(document.title?.replace(/^题目笔记\s*·\s*/, '') || document.binding.questionId || '绑定题目'),
          detail: '笔记绑定题目',
        });
        addEdge(documentId, questionId, 'binding');
      }
      (document.links || []).forEach(link => {
        if (!link?.target) return;
        if (link.type === 'notebook') {
          const targetId = `document:${stableHash(link.target)}`;
          addEdge(documentId, targetId, 'notebook');
          return;
        }
        if (link.type === 'question') {
          const target = String(link.target);
          const questionId = `question:${stableHash(target)}`;
          addNode({ id: questionId, type: 'question', target, label: trimLabel(link.label || '关联题目'), detail: '关联题目' });
          addEdge(documentId, questionId, 'question');
          return;
        }
        if (link.type === 'url' && /^https:\/\//i.test(link.target)) {
          const target = String(link.target);
          const resourceId = `resource:${stableHash(target)}`;
          addNode({ id: resourceId, type: 'resource', target, label: trimLabel(link.label || target), detail: '外部资料' });
          addEdge(documentId, resourceId, 'resource');
        }
      });
    });

    const typeCounts = {};
    nodes.forEach(node => {
      typeCounts[node.data.type] = (typeCounts[node.data.type] || 0) + 1;
    });
    return {
      elements: [...nodes.values(), ...edges.values()],
      counts: {
        nodes: nodes.size,
        edges: edges.size,
        documents: source.length,
        typeCounts,
        truncated: source.length < (Array.isArray(documents) ? documents.length : 0) || nodes.size >= maxNodes,
      },
    };
  }

  global.QuizNotebookKnowledgeGraph = { build, stableHash };
})(window);
