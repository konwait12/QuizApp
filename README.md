# QuizApp 刷题练习工具

纯前端单页刷题工具，支持内置题库、导入 JSON、分层练习、顺序练习、随机练习、背题模式、答案表、错题集、学习统计和本地回收站。

## 题库目录与分类规则

默认题库文件统一放在 `./data/`。新版推荐按 `./data/科目/章节.json` 组织，例如 `./data/毛概/第一章.json`；旧版平铺在 `./data/毛概-第一章.json` 的文件仍然兼容。

应用显示分类优先靠 JSON 内部字段和应用本地层级配置完成，物理文件夹只是更清楚的存放方式：

- `subject`：第一级科目，例如 `毛概`、`习思`。
- `chapter`：第二级章节，例如 `导论`、`第一章`。
- `path`：可选的完整层级数组，例如 `["毛概", "导论"]`。当前内置题库主要使用两层；以后需要更多层级时继续扩展这个字段。
- `name`：题库显示名，也会作为字段缺失时的兜底来源。

加载优先级：

1. 优先读取 JSON 内的 `path`。
2. 没有 `path` 时读取 `subject` + `chapter`。
3. 仍然缺失时，从 `name` 或文件名按 `科目-章节` 解析。
4. 解析失败时归入 `未分类 / 综合练习`。

推荐新版目录：

```text
./data/
  毛概/
    导论.json
    第一章.json
  习思/
    导论.json
    第一章.json
```

旧版平铺目录也能继续读取：

```text
./data/
  毛概-导论.json
  毛概-第一章.json
```

应用会读取每个 JSON 内部的 `subject`、`chapter` 或 `path`，再在首页归到对应科目和章节。真实文件夹用于管理文件，不作为唯一分类依据。

## JSON 格式示例

```json
{
  "name": "毛概-导论",
  "subject": "毛概",
  "chapter": "导论",
  "path": ["毛概", "导论"],
  "questions": [
    {
      "id": "q1",
      "type": "单选",
      "q": "题干",
      "options": ["A选项", "B选项", "C选项", "D选项"],
      "ans": "C",
      "exp": "解析内容"
    }
  ]
}
```

字段说明：

- `questions`：题目数组。
- `q`：题干。
- `options`：选项数组。
- `ans`：正确答案，单选可用 `A`、`B`、`C`、`D`；多选可用类似 `AC`。
- `exp`：解析，可为空。

## 本地编辑与回收站

用户在前端创建层级、重命名、移动题库时，不要求真实移动 `./data/` 里的文件。应用会把本地覆盖信息保存在浏览器本地存储中：

- `quizapp_library_nodes`：自定义层级。
- `quizapp_bank_meta`：题库显示名、路径、隐藏状态。
- `quizapp_banks`：用户导入题库。
- `quizapp_recycle_bin`：回收站数据。

设置页显示的默认题库目录是相对路径 `./data/`，回收站索引目录是 `./data/.quizapp-recycle/`。单文件浏览器运行时，真正可回档的数据保存在本地存储里；APK 内置题库删除时只做本机隐藏，不会物理删除 APK 内置资源。

“打开默认题库位置”会尽量打开真实文件夹：桌面 EXE 会在 exe 同级创建并打开 `data` 文件夹；Android APK 会在应用外部文件区创建 `Android/data/com.quizapp/files/data` 并同步一份内置题库。不同手机文件管理器对 `Android/data` 的定位能力不同，无法直达时会退到系统文件夹入口，但不会再触发 JSON 文件选择器。

## 打包与导入约定

- 内置题库放在 `./data/`，方便 APK 和 EXE 打包脚本按相对路径收集。
- 用户导入 JSON 时，可以选择任意本地文件；导入后分类仍按 JSON 内的 `path`、`subject`、`chapter` 解析。
- 如果一个 JSON 没有写分类字段，建议至少把文件名写成 `科目-章节.json`，例如 `毛概-第一章.json`。

## 运行与打包

浏览器版可以直接双击 `index.html` 打开。

版本检测不需要单独服务器。应用读取 GitHub Releases Latest，优先比较 release 版本号；当版本号相同但 Release 目标提交与当前 APK 内置构建提交不一致时，也会提示更新。APK 资产名称、大小、digest 和更新时间会参与提醒指纹，避免只按 tag 判断。

- Android APK 内会优先调用系统下载器下载 Release 里的 APK，下载完成后拉起系统安装器。
- 浏览器或网络环境不稳定时，可以打开 GitHub Release 页面手动下载。
- 覆盖安装同包名、同签名的 APK 不会清除本机题库、做题进度、错题集、设置和学习统计；卸载旧版或清除应用数据会删除本地数据。

## Release 题库分发

题库更新同样不需要服务器。设置页里的“检查题库更新”会读取 GitHub Releases Latest，检查期间显示进度条；检查完成后弹窗按“科目 -> 章节”展示可用题库包。用户可以直接勾选整个科目，也可以展开科目后只勾选部分章节；确认“下载题库”后，应用只拉取被勾选的题库文件并写入本机。

1. 优先读取 Release asset 里的 `quizapp-bank-manifest.json`、`QuizApp-bank-manifest.json` 或 `bank-manifest.json`。
2. 新版清单推荐只放题库元信息，每项用 `file` 指向同一个 Release 里的单独题库 JSON；`path` 按 `["科目", "章节"]` 记录逻辑分类。旧版清单直接内嵌完整题库对象仍然兼容。
3. 没有清单时，会自动识别 Release asset 中以 `quizapp-bank-` 开头、以 `.json` 结尾的文件。
4. 拉取到的题库会保存到本机导入题库区，并标记为 Release 分发题库；如果本地已有同路径或同名题库，用户可以选择覆盖，也可以另存为新题库，不会删除做题进度、错题集和其他未选题库。

推荐清单格式：

```json
{
  "banks": [
    {
      "file": "quizapp-bank-001.json",
      "name": "毛概-导论",
      "subject": "毛概",
      "chapter": "导论",
      "path": ["毛概", "导论"],
      "questionCount": 12
    }
  ]
}
```

被引用的题库 JSON 仍使用本项目原有格式，`subject`、`chapter`、`path` 负责分类。以后本项目内置题库推荐使用 `data/科目/章节.json` 物理目录；Release asset 可以继续平铺上传单个题库 JSON，因为分级显示依赖清单和 JSON 内部 `path`，不依赖资产文件夹。Release asset 文件名建议只使用 ASCII，例如 `quizapp-bank-001.json`，避免中文文件名在上传后被平台或命令行工具改写。

## Release 公告分发

公告同样不需要服务器。应用启动时会读取 GitHub Releases Latest，如果 Release asset 里存在 `quizapp-announcements.json`，会下载并缓存公告清单；发现未读公告时自动弹窗，首页信件按钮显示红点。没有新公告时自动检测保持静默；只有用户在设置页手动点击“检查公告”时，才提示“没有未读公告”。

推荐公告文件格式：

```json
{
  "announcements": [
    {
      "id": "notice-20260710-1",
      "title": "题库更新说明",
      "date": "2026-07-10",
      "body": "<p>这里写公告正文，可以使用简单 HTML。</p>"
    }
  ]
}
```

`id` 必须稳定且唯一。以后只要在最新版 Release 中替换 `quizapp-announcements.json`，用户下次打开应用就能收到新公告；不需要重新发布 APK。公告正文建议使用简单的 `<p>`、`<ul>`、`<li>`，不要放脚本。

APK 打包：

```powershell
powershell -ExecutionPolicy Bypass -File scripts/build-apk.ps1
```

Windows EXE 打包：

```powershell
powershell -ExecutionPolicy Bypass -File scripts/build-exe.ps1
```

EXE 默认启动本地服务 `http://127.0.0.1:0721/`，题库资源会从内置资源同步到 exe 同级 `data` 文件夹，不依赖用户电脑上的绝对路径。

## 作者

- GitHub: [konwait12](https://github.com/konwait12)
- AutoQuiz 导出工具: [konwait12/AutoQuiz](https://github.com/konwait12/AutoQuiz)
- B站: [konwait12](https://space.bilibili.com/493568339)
