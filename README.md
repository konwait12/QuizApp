# QuizApp

QuizApp 是一款本地优先的刷题与笔记工具，支持 Android、Windows 和浏览器使用。题库、练习记录、错题、笔记与设置默认保存在本机。

## 主要功能

- 按科目和章节管理题库，支持顺序、随机、背题和模拟考试。
- 支持单选、多选、判断、填空与解答题，可手动标记掌握状态、加入错题集或复习队列。
- 提供 FSRS 复习、学习统计、练习恢复和考试记录。
- 支持 JSON 题库导入、题库更新、本地编辑与回收站。
- 题目可绑定独立笔记，返回刷题时保留原题号、答案和进度。
- 自由笔记支持文件夹、封面、文字输入、手写、图层、书签、PDF 批注、OCR、公式、知识链接和导出。
- 手写模式支持手写笔与触摸切换、双指缩放、页面滚动和横向翻阅。
- 可选 AI 题目分析、追问、相似题、错因归纳和笔记摘要。
- 支持浅色、深色、多套配色与终末地独立主题。

## 下载与安装

最新版安装包和更新说明见 [GitHub Releases](https://github.com/konwait12/QuizApp/releases)。

Android 使用同一签名的 APK 覆盖安装时会保留本机数据。卸载应用、清除应用数据、使用不同签名或更换包名可能导致数据无法继续读取，更新前建议先导出完整备份。

应用会通过 GitHub Releases 检查版本、公告和题库更新，不依赖单独的更新服务器。自动检查没有新内容时保持静默，手动检查会显示检查结果。

## 基本使用

1. 从内置题库、Release 题库或本地 JSON 中选择需要的内容。
2. 进入科目后选择练习方式，也可以从学习页进入复习、统计或模拟考试。
3. 做题时可打开题目笔记，使用文字、手写或 PDF 资料辅助学习。
4. 在设置中导出完整备份，用于换机、重装前保存或恢复数据。

自由笔记入口位于首页。手机默认使用紧凑列表，桌面和平板默认使用封面网格，显示方式可随时切换。

## 题库导入

应用支持包含以下常用字段的 JSON 题库：

```json
{
  "name": "示例题库",
  "subject": "示例科目",
  "chapter": "第一章",
  "path": ["示例科目", "第一章"],
  "questions": [
    {
      "id": "q1",
      "type": "单选",
      "q": "题干",
      "options": ["A 选项", "B 选项", "C 选项", "D 选项"],
      "ans": "C",
      "exp": "解析内容"
    }
  ]
}
```

`path` 用于多级分类；缺少时会尝试使用 `subject` 和 `chapter`。题目还可以包含图片、内置解析和主观题标记。AI 生成内容与题库原有答案分开保存，不会覆盖题库解析。

## 笔记工作台

笔记工作台借鉴 [SpeedyNote](https://github.com/alpha-liu-01/SpeedyNote) 的分层文档与视口交互设计，并结合 QuizApp 的题目绑定、资料库和学习流程进行了适配。

笔记支持分页与连续文档、文字和笔迹叠加、PDF 导入与批注、图层、对象选择、书签、搜索、OCR、Markdown、KaTeX 公式、知识链接、PNG/PDF 导出以及 SNBX 导入导出。手机、平板和桌面使用不同的紧凑布局，工具面板可以按需展开或收起。

## 数据与隐私

- 题库、进度、错题、复习、考试、笔记和 AI 历史默认保存在本机。
- 完整备份包含题库索引、学习状态、设置、笔记和媒体资源。
- AI 功能只有在用户配置接口并主动使用时才会发送当前任务所需内容。
- API Key 不会写入题库、安装包或 Git 仓库；备份默认不包含 API Key。
- 图片题目不会在未确认模型具备视觉能力时发送给纯文本模型。

## 本地运行与构建

浏览器版可直接打开 `index.html`。Android 和 Windows 构建入口：

```powershell
powershell -ExecutionPolicy Bypass -File scripts/build-apk.ps1
powershell -ExecutionPolicy Bypass -File scripts/build-exe.ps1
```

项目代码和资源均使用相对路径，构建产物位于 `output/`。

## 作者

- GitHub: [konwait12](https://github.com/konwait12)
- AutoQuiz: [konwait12/AutoQuiz](https://github.com/konwait12/AutoQuiz)
- Bilibili: [konwait12](https://space.bilibili.com/493568339)

## 开源说明

本项目按 GNU GPL v3.0 或更高版本分发。笔记模块参考了 SpeedyNote 的开源设计；第三方项目、许可证和修改边界见 [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)，完整许可条款见 [LICENSE](LICENSE)。
