/* QuizApp local backup package and transactional restore. GPL-3.0-or-later. */
(function attachQuizBackup(global) {
  'use strict';

  const FORMAT = 'quizapp-local-backup';
  const SCHEMA_VERSION = 1;
  const DEFAULT_STORAGE_PREFIX = 'quizapp_';
  const DEFAULT_DATABASE_NAME = 'quizapp_study_data';
  const AI_CONFIG_KEY = 'quizapp_ai_config';

  function progress(callback, stage, percent, message) {
    if (typeof callback === 'function') callback({ stage, percent, message });
  }

  function clone(value) {
    if (typeof structuredClone === 'function') return structuredClone(value);
    return JSON.parse(JSON.stringify(value));
  }

  function byteLength(value) {
    return new TextEncoder().encode(String(value || '')).byteLength;
  }

  function readAppStorage(storage = global.localStorage, prefix = DEFAULT_STORAGE_PREFIX) {
    const result = {};
    for (let index = 0; index < storage.length; index++) {
      const key = storage.key(index);
      if (key && key.startsWith(prefix)) result[key] = storage.getItem(key);
    }
    return result;
  }

  function removeSecrets(entries) {
    const sanitized = { ...entries };
    try {
      const config = JSON.parse(sanitized[AI_CONFIG_KEY] || 'null');
      if (config && typeof config === 'object') {
        delete config.apiKey;
        sanitized[AI_CONFIG_KEY] = JSON.stringify(config);
      }
    } catch(e) {}
    return sanitized;
  }

  function preserveCurrentSecrets(entries, storage, includesSecrets) {
    if (includesSecrets) return entries;
    const merged = { ...entries };
    try {
      const current = JSON.parse(storage.getItem(AI_CONFIG_KEY) || 'null');
      const restored = JSON.parse(merged[AI_CONFIG_KEY] || '{}');
      if (current?.apiKey) restored.apiKey = current.apiKey;
      merged[AI_CONFIG_KEY] = JSON.stringify(restored);
    } catch(e) {}
    return merged;
  }

  function requestValue(request) {
    return new Promise((resolve, reject) => {
      request.onsuccess = () => resolve(request.result);
      request.onerror = () => reject(request.error || new Error('IndexedDB 请求失败'));
    });
  }

  function transactionDone(transaction) {
    return new Promise((resolve, reject) => {
      transaction.oncomplete = () => resolve();
      transaction.onerror = () => reject(transaction.error || new Error('IndexedDB 事务失败'));
      transaction.onabort = () => reject(transaction.error || new Error('IndexedDB 事务已中止'));
    });
  }

  function openDatabase(name, indexedDBFactory = global.indexedDB) {
    return new Promise((resolve, reject) => {
      if (!indexedDBFactory) return reject(new Error('当前环境不支持 IndexedDB'));
      const request = indexedDBFactory.open(name);
      request.onsuccess = () => resolve(request.result);
      request.onerror = () => reject(request.error || new Error('无法打开本地数据库'));
    });
  }

  function normalizeKeyPath(keyPath) {
    if (keyPath === null || keyPath === undefined) return null;
    if (typeof keyPath === 'string') return keyPath;
    return Array.from(keyPath);
  }

  async function readDatabase(name = DEFAULT_DATABASE_NAME, indexedDBFactory = global.indexedDB) {
    const database = await openDatabase(name, indexedDBFactory);
    try {
      const storeNames = Array.from(database.objectStoreNames);
      if (!storeNames.length) return { name, version: database.version, stores: {} };
      const transaction = database.transaction(storeNames, 'readonly');
      const tasks = storeNames.map(async storeName => {
        const store = transaction.objectStore(storeName);
        const schema = {
          keyPath: normalizeKeyPath(store.keyPath),
          autoIncrement: Boolean(store.autoIncrement),
          indexes: Array.from(store.indexNames).map(indexName => {
            const index = store.index(indexName);
            return {
              name: index.name,
              keyPath: normalizeKeyPath(index.keyPath),
              unique: Boolean(index.unique),
              multiEntry: Boolean(index.multiEntry),
            };
          }),
        };
        const [values, keys] = await Promise.all([
          requestValue(store.getAll()),
          requestValue(store.getAllKeys()),
        ]);
        return [storeName, {
          schema,
          records: (values || []).map((value, index) => ({ key: keys?.[index], value })),
        }];
      });
      const stores = Object.fromEntries(await Promise.all(tasks));
      await transactionDone(transaction);
      return { name, version: database.version, stores };
    } finally {
      database.close();
    }
  }

  async function replaceDatabase(snapshot, indexedDBFactory = global.indexedDB) {
    const stores = snapshot?.stores && typeof snapshot.stores === 'object' ? snapshot.stores : {};
    const storeNames = Object.keys(stores);
    if (!storeNames.length) return;
    const database = await openDatabase(snapshot.name || DEFAULT_DATABASE_NAME, indexedDBFactory);
    try {
      const missing = storeNames.filter(name => !database.objectStoreNames.contains(name));
      if (missing.length) throw new Error(`当前版本不支持备份中的数据库仓库：${missing.join('、')}`);
      const transaction = database.transaction(storeNames, 'readwrite');
      for (const storeName of storeNames) {
        const store = transaction.objectStore(storeName);
        store.clear();
        for (const record of stores[storeName]?.records || []) {
          if (store.keyPath === null && record.key !== undefined) store.put(record.value, record.key);
          else store.put(record.value);
        }
      }
      await transactionDone(transaction);
    } finally {
      database.close();
    }
  }

  function replaceAppStorage(entries, storage = global.localStorage, prefix = DEFAULT_STORAGE_PREFIX) {
    const existingKeys = [];
    for (let index = 0; index < storage.length; index++) {
      const key = storage.key(index);
      if (key && key.startsWith(prefix)) existingKeys.push(key);
    }
    existingKeys.forEach(key => storage.removeItem(key));
    Object.entries(entries || {}).forEach(([key, value]) => {
      if (key.startsWith(prefix) && value !== null && value !== undefined) storage.setItem(key, String(value));
    });
  }

  function fnv1a32(text) {
    let hash = 0x811c9dc5;
    for (let index = 0; index < text.length; index++) {
      hash ^= text.charCodeAt(index);
      hash = Math.imul(hash, 0x01000193);
    }
    return (hash >>> 0).toString(16).padStart(8, '0');
  }

  async function sha256(text) {
    if (!global.crypto?.subtle) return null;
    const digest = await global.crypto.subtle.digest('SHA-256', new TextEncoder().encode(text));
    return Array.from(new Uint8Array(digest)).map(value => value.toString(16).padStart(2, '0')).join('');
  }

  async function checksum(value, preferredAlgorithm = 'sha256') {
    const text = typeof value === 'string' ? value : JSON.stringify(value);
    if (preferredAlgorithm === 'sha256') {
      const valueHash = await sha256(text);
      if (valueHash) return { algorithm: 'sha256', value: valueHash };
    }
    if (preferredAlgorithm !== 'sha256' && preferredAlgorithm !== 'fnv1a32') {
      throw new Error(`不支持的备份校验算法：${preferredAlgorithm}`);
    }
    return { algorithm: 'fnv1a32', value: fnv1a32(text) };
  }

  function summarizeData(data) {
    const stores = data?.indexedDB?.stores || {};
    const recordCounts = Object.fromEntries(Object.entries(stores).map(([name, store]) => [name, store.records?.length || 0]));
    return {
      localStorageKeys: Object.keys(data?.localStorage || {}).length,
      databaseStores: Object.keys(stores).length,
      databaseRecords: Object.values(recordCounts).reduce((sum, count) => sum + count, 0),
      recordCounts,
      importedBankRecords: recordCounts.large_banks || 0,
      notebookRecords: recordCounts.notebooks || 0,
      legacyInkRecords: recordCounts.ink_notes || 0,
    };
  }

  async function create(options = {}) {
    const storage = options.storage || global.localStorage;
    const databaseName = options.databaseName || DEFAULT_DATABASE_NAME;
    progress(options.onProgress, 'storage', 12, '正在整理设置与学习记录');
    const storedEntries = readAppStorage(storage, options.storagePrefix || DEFAULT_STORAGE_PREFIX);
    const localStorageEntries = options.includeSecrets ? storedEntries : removeSecrets(storedEntries);
    progress(options.onProgress, 'database', 36, '正在读取题库与手写笔记');
    const indexedDBData = await readDatabase(databaseName, options.indexedDB || global.indexedDB);
    const data = { localStorage: localStorageEntries, indexedDB: indexedDBData };
    const dataText = JSON.stringify(data);
    progress(options.onProgress, 'checksum', 72, '正在计算备份校验值');
    const digest = await checksum(dataText);
    const packageData = {
      format: FORMAT,
      schemaVersion: SCHEMA_VERSION,
      manifest: {
        createdAt: new Date().toISOString(),
        appVersion: String(options.appVersion || ''),
        buildCommit: String(options.buildCommit || ''),
        platform: String(options.platform || global.navigator?.userAgent || ''),
        includesSecrets: Boolean(options.includeSecrets),
        dataBytes: byteLength(dataText),
        counts: summarizeData(data),
        checksum: digest,
      },
      data,
    };
    progress(options.onProgress, 'complete', 100, '备份包已生成');
    return packageData;
  }

  function parse(value) {
    let backup = value;
    if (typeof value === 'string') {
      try {
        backup = JSON.parse(value.replace(/^\uFEFF/, ''));
      } catch(e) {
        throw new Error('备份文件不是有效 JSON');
      }
    }
    if (!backup || typeof backup !== 'object') throw new Error('备份文件内容为空');
    if (backup.format !== FORMAT) throw new Error('这不是 QuizApp 完整备份文件');
    if (Number(backup.schemaVersion) !== SCHEMA_VERSION) throw new Error(`不支持的备份格式版本：${backup.schemaVersion}`);
    if (!backup.data?.localStorage || !backup.data?.indexedDB?.stores) throw new Error('备份文件缺少本地数据区');
    if (!backup.manifest?.checksum?.algorithm || !backup.manifest?.checksum?.value) throw new Error('备份文件缺少校验值');
    return backup;
  }

  async function inspect(value) {
    const backup = parse(value);
    const expected = backup.manifest.checksum;
    const actual = await checksum(JSON.stringify(backup.data), expected.algorithm);
    const valid = actual.value === expected.value;
    return {
      backup,
      valid,
      expected,
      actual,
      counts: summarizeData(backup.data),
      dataBytes: byteLength(JSON.stringify(backup.data)),
    };
  }

  async function restore(value, options = {}) {
    const storage = options.storage || global.localStorage;
    const databaseName = options.databaseName || DEFAULT_DATABASE_NAME;
    progress(options.onProgress, 'verify', 8, '正在校验备份文件');
    const inspection = await inspect(value);
    if (!inspection.valid) throw new Error('备份校验失败，文件可能已损坏或被修改');

    progress(options.onProgress, 'snapshot', 24, '正在创建恢复前快照');
    const before = {
      localStorage: readAppStorage(storage, options.storagePrefix || DEFAULT_STORAGE_PREFIX),
      indexedDB: await readDatabase(databaseName, options.indexedDB || global.indexedDB),
    };
    const target = clone(inspection.backup.data);
    target.localStorage = preserveCurrentSecrets(
      target.localStorage,
      storage,
      Boolean(inspection.backup.manifest.includesSecrets),
    );

    let applied = false;
    try {
      progress(options.onProgress, 'database', 48, '正在恢复题库与手写笔记');
      await replaceDatabase(target.indexedDB, options.indexedDB || global.indexedDB);
      progress(options.onProgress, 'storage', 76, '正在恢复设置与学习记录');
      replaceAppStorage(target.localStorage, storage, options.storagePrefix || DEFAULT_STORAGE_PREFIX);
      applied = true;
      progress(options.onProgress, 'complete', 100, '恢复完成');
      return { inspection, rolledBack: false };
    } catch(error) {
      progress(options.onProgress, 'rollback', 84, '恢复失败，正在回滚原数据');
      try {
        await replaceDatabase(before.indexedDB, options.indexedDB || global.indexedDB);
        replaceAppStorage(before.localStorage, storage, options.storagePrefix || DEFAULT_STORAGE_PREFIX);
      } catch(rollbackError) {
        const failure = new Error(`恢复失败且回滚未完成：${rollbackError.message || rollbackError}`);
        failure.cause = error;
        throw failure;
      }
      const failure = new Error(`恢复失败，已回滚原数据：${error.message || error}`);
      failure.cause = error;
      failure.applied = applied;
      throw failure;
    }
  }

  function stringify(backup) {
    return JSON.stringify(parse(backup));
  }

  function download(backup, filename) {
    const text = typeof backup === 'string' ? backup : stringify(backup);
    const blob = new Blob([text], { type: 'application/json;charset=utf-8' });
    const url = URL.createObjectURL(blob);
    const link = document.createElement('a');
    link.href = url;
    link.download = filename || 'QuizApp-backup.quizbackup';
    document.body.appendChild(link);
    link.click();
    link.remove();
    global.setTimeout(() => URL.revokeObjectURL(url), 1000);
  }

  global.QuizBackup = {
    FORMAT,
    SCHEMA_VERSION,
    DEFAULT_STORAGE_PREFIX,
    DEFAULT_DATABASE_NAME,
    create,
    parse,
    inspect,
    restore,
    stringify,
    download,
    readAppStorage,
    readDatabase,
    replaceAppStorage,
    replaceDatabase,
    summarizeData,
  };
})(window);
