# SpeedyNote 功能移植矩阵

对照来源：`alpha-liu-01/SpeedyNote`。QuizApp 保持 HTML/WebView 主线，移植目标是题库绑定的手写学习工作台，不宣称复刻 Qt 原生渲染器的实现方式或性能数字。

## 已完成

| 上游能力 | QuizApp 实现 | 验证 |
| --- | --- | --- |
| 钢笔、Marker、荧光笔 | 独立工具、压感点、不同透明度和粗细 | `verify-notebook-engine.mjs`、`notebook-ui.mjs` |
| 矢量笔迹、直线 | perfect-freehand 矢量轮廓；直线只保存端点 | 同上 |
| 图层 | 新增、删除、锁定、隐藏、排序、透明度、向下合并、撤销 | 同上 |
| 笔画橡皮 | 按笔画命中并删除，支持撤销 | `notebook-ui.mjs` |
| 手势 | 手写笔输入、掌托过滤、双指平移缩放、触点锚定、可配置越界 | `notebook-ui.mjs` |
| 分页笔记 | 新建、复制、删除、重命名、缩略图、拖动排序 | `notebook-ui.mjs` |
| 无边界画布 | 独立创建/模式切换；书写或平移到边缘时分块扩展；左上扩展保持内容屏幕锚点 | `verify-notebook-engine.mjs`、`notebook-ui.mjs` |
| PDF 背景 | 离线导入、分页范围、文字索引、独立资源存储 | `pdf-notebook-ui.mjs` |
| PDF 目录和内部链接 | 导入目录元数据；目录跳页；选择工具点击页面链接 | `pdf-notebook-ui.mjs` |
| SNBX 分享 | ZIP 笔记包导入导出，包含文档和 PDF 页面资源；支持多文件导入 | `notebook-ui.mjs` |
| OCR | 图片和页面识别、结果编辑与插入 | `notebook-ocr-ui.mjs` |
| 链接与 Markdown | 外部链接、题目/笔记关联、Markdown 和公式对象 | `notebook-rich-objects-ui.mjs` |
| 多文档 | 题目笔记与自由笔记统一标签切换、关闭标签、最多保留 8 个打开标签 | `notebook-ui.mjs` |
| 操作栏和子工具栏 | 主工具栏负责工具/撤销/插入；子工具栏按绘图或选择状态显示笔触和对象操作 | `notebook-ui.mjs` |
| 批处理 | 多文件 SNBX 导入；最多 60 份笔记批量打包 PDF 或 SNBX | `notebook-ui.mjs` |
| 题库联动 | 从刷题页进入当前题笔记，返回恢复题号、答案、模式和视角 | `notebook-ui.mjs` |

## 部分完成

| 上游能力 | 当前边界 | 后续工作 |
| --- | --- | --- |
| 无边界画布存储性能 | 用户坐标范围会按 800px 分块扩展，但持久化仍是单页 JSON | 后续可引入 IndexedDB 空间索引和可见块按需加载，面向超大型白板优化 |

## 不直接等价

- 上游的 C++ 原生 360Hz 轮询、MuPDF 原生渲染、平台安装包和 CLI 不能直接复制到 HTML/WebView。QuizApp 使用 Pointer Events、PDF.js 和应用内批处理入口实现对应用户能力。
- 上游 `.snb` 分块目录格式与 QuizApp IndexedDB 数据模型不同。QuizApp 使用动态扩展坐标和兼容自身资源库的 `.snbx` 包，避免破坏旧数据和 APK WebView 存储。
