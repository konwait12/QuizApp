# QuizApp 刷题练习工具

纯前端单页刷题工具，支持内置题库、导入 JSON、分层练习、顺序练习、随机练习、背题模式、答案表、错题集、学习统计和本地回收站。

## 题库目录与分类规则

默认题库文件统一放在 `./data/`。这些 JSON 可以全部平铺在同一层，不需要再按真实文件夹拆成“科目/章节”。

分类不是靠文件夹路径完成的，而是靠 JSON 内部字段和应用本地层级配置完成：

- `subject`：第一级科目，例如 `毛概`、`习思`。
- `chapter`：第二级章节，例如 `导论`、`第一章`。
- `path`：可选的完整层级数组，例如 `["毛概", "导论"]`。当前内置题库主要使用两层；以后需要更多层级时继续扩展这个字段。
- `name`：题库显示名，也会作为字段缺失时的兜底来源。

加载优先级：

1. 优先读取 JSON 内的 `path`。
2. 没有 `path` 时读取 `subject` + `chapter`。
3. 仍然缺失时，从 `name` 或文件名按 `科目-章节` 解析。
4. 解析失败时归入 `未分类 / 综合练习`。

因此 `./data/` 可以这样平铺：

```text
./data/
  毛概-导论.json
  毛概-第一章.json
  习思-导论.json
  习思-第一章.json
```

应用会读取每个 JSON 内部的 `subject`、`chapter` 或 `path`，再在首页归到对应科目和章节。真实文件夹只负责存放文件，不负责决定分类。

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

版本检测不需要单独服务器。应用读取 GitHub Releases Latest，发现 release 版本号高于当前 `APP_VERSION` 且资产里存在 APK 时，会提示用户更新。

- Android APK 内会优先调用系统下载器下载 Release 里的 APK，下载完成后拉起系统安装器。
- 浏览器或网络环境不稳定时，可以打开 GitHub Release 页面手动下载。
- 覆盖安装同包名、同签名的 APK 不会清除本机题库、做题进度、错题集、设置和学习统计；卸载旧版或清除应用数据会删除本地数据。

## Release 题库分发

题库更新同样不需要服务器。设置页里的“检查题库更新”会读取 GitHub Releases Latest，先弹窗展示可用题库包，用户确认“下载题库”后再写入本机，并按下面规则拉取题库：

1. 优先读取 Release asset 里的 `quizapp-bank-manifest.json`、`QuizApp-bank-manifest.json` 或 `bank-manifest.json`。
2. 清单可以直接放题库对象，也可以写 `banks` 数组；每项可以是完整题库 JSON，也可以用 `file` / `asset` / `name` 指向同一个 Release 里的题库 JSON。
3. 没有清单时，会自动识别 Release asset 中以 `quizapp-bank-` 开头、以 `.json` 结尾的文件。
4. 拉取到的题库会保存到本机导入题库区，并标记为 Release 分发题库；同一路径下的 Release 题库会覆盖内置题库显示，但不会删除用户自己导入的其他题库和本地做题数据。

推荐清单格式：

```json
{
  "banks": [
    { "file": "quizapp-bank-maogai-intro.json" },
    { "file": "quizapp-bank-maogai-chapter-1.json" }
  ]
}
```

被引用的题库 JSON 仍使用本项目原有格式，`subject`、`chapter`、`path` 负责分类；真实文件可以都平铺在 Release asset 中。

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
