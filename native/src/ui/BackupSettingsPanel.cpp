#include "ui/BackupSettingsPanel.h"

#include "platform/BackupDocumentPlatform.h"
#include "storage/Database.h"

#include <QCheckBox>
#include <QCoreApplication>
#include <QDateTime>
#include <QDialog>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QProgressBar>
#include <QPointer>
#include <QPushButton>
#include <QSettings>
#include <QStandardPaths>
#include <QTimer>
#include <QUuid>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrentRun>

#include <algorithm>
#include <array>
#include <utility>

namespace quizapp::ui {
namespace {

#ifndef QUIZAPP_APP_VERSION
#define QUIZAPP_APP_VERSION "2.0.0-alpha.3"
#endif
#ifndef QUIZAPP_BUILD_COMMIT
#define QUIZAPP_BUILD_COMMIT "dev"
#endif

QString countText(const QJsonObject &counts, const QString &key)
{
    return QString::number(static_cast<qint64>(counts.value(key).toDouble()));
}

QString humanBytes(qint64 bytes)
{
    if (bytes < 1024) {
        return QStringLiteral("%1 B").arg(bytes);
    }
    if (bytes < 1024 * 1024) {
        return QStringLiteral("%1 KB").arg(bytes / 1024.0, 0, 'f', 1);
    }
    if (bytes < 1024LL * 1024 * 1024) {
        return QStringLiteral("%1 MB").arg(bytes / (1024.0 * 1024), 0, 'f', 1);
    }
    return QStringLiteral("%1 GB").arg(bytes / (1024.0 * 1024 * 1024), 0, 'f', 2);
}

QString stageText(const QString &stage)
{
    if (stage == QStringLiteral("database")) return QStringLiteral("正在创建数据库快照");
    if (stage == QStringLiteral("archive")) return QStringLiteral("正在写入完整备份");
    if (stage == QStringLiteral("verify")) return QStringLiteral("正在校验备份完整性");
    if (stage == QStringLiteral("stage")) return QStringLiteral("正在暂存恢复数据");
    if (stage == QStringLiteral("complete")) return QStringLiteral("备份已完成");
    return QStringLiteral("正在处理备份");
}

} // namespace

BackupSettingsPanel::BackupSettingsPanel(
    QString databasePath,
    QString dataRoot,
    QString sharedRoot,
    QWidget *parent)
    : QFrame(parent)
    , databasePath_(std::move(databasePath))
    , dataRoot_(std::move(dataRoot))
    , sharedRoot_(std::move(sharedRoot))
    , watcher_(new QFutureWatcher<BackupTaskResult>(this))
    , documentPollTimer_(new QTimer(this))
{
    setObjectName(QStringLiteral("backupSettingsSurface"));
    setMaximumWidth(760);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(20, 20, 20, 20);
    layout->setSpacing(10);
    auto *title = new QLabel(QStringLiteral("完整本地备份"), this);
    title->setObjectName(QStringLiteral("settingsSectionHeading"));
    auto *description = new QLabel(
        QStringLiteral("备份 SQLite、设置、学习进度、错题集、复习、考试、手写笔记、共享题库和回收站。文件只保存在本机。"),
        this);
    description->setObjectName(QStringLiteral("pageSupportingText"));
    description->setWordWrap(true);
    includeSecretsChoice_ = new QCheckBox(
        QStringLiteral("把 API Key 明文写入备份（不推荐）"), this);
    includeSecretsChoice_->setObjectName(QStringLiteral("backupIncludeSecretsChoice"));
    auto *security = new QLabel(
        QStringLiteral("默认不导出 API Key；恢复无密钥备份时会保留当前设备已有密钥。"),
        this);
    security->setObjectName(QStringLiteral("backupSecurityNote"));
    security->setWordWrap(true);
    auto *actions = new QHBoxLayout;
    actions->setSpacing(8);
    exportButton_ = new QPushButton(QStringLiteral("导出完整备份"), this);
    exportButton_->setObjectName(QStringLiteral("backupExportButton"));
    exportButton_->setMinimumHeight(44);
    restoreButton_ = new QPushButton(QStringLiteral("从备份恢复"), this);
    restoreButton_->setObjectName(QStringLiteral("backupRestoreButton"));
    restoreButton_->setMinimumHeight(44);
    connect(exportButton_, &QPushButton::clicked, this, &BackupSettingsPanel::chooseExportPath);
    connect(restoreButton_, &QPushButton::clicked, this, &BackupSettingsPanel::chooseRestorePath);
    actions->addWidget(exportButton_);
    actions->addWidget(restoreButton_);
    progress_ = new QProgressBar(this);
    progress_->setObjectName(QStringLiteral("backupProgress"));
    progress_->setRange(0, 100);
    progress_->setValue(0);
    progress_->hide();
    status_ = new QLabel(QStringLiteral("尚未执行备份或恢复"), this);
    status_->setObjectName(QStringLiteral("backupStatus"));
    status_->setWordWrap(true);
    layout->addWidget(title);
    layout->addWidget(description);
    layout->addWidget(includeSecretsChoice_);
    layout->addWidget(security);
    layout->addLayout(actions);
    layout->addWidget(progress_);
    layout->addWidget(status_);

    connect(watcher_, &QFutureWatcher<BackupTaskResult>::finished, this, [this] {
        const BackupTaskResult result = watcher_->result();
        const TaskKind completedTask = taskKind_;
        taskKind_ = TaskKind::None;
        setBusy(false);
        if (!result.success) {
#if defined(Q_OS_ANDROID)
            if (completedTask == TaskKind::Export) {
                QFile::remove(result.path);
            }
#endif
            if (!androidImportTempPath_.isEmpty() && result.path == androidImportTempPath_) {
                QFile::remove(androidImportTempPath_);
                androidImportTempPath_.clear();
            }
            status_->setText(result.error);
            return;
        }
        if (completedTask == TaskKind::Inspect) {
            inspection_ = result.inspection;
            inspectedPath_ = result.path;
            status_->setText(QStringLiteral("备份校验通过：%1")
                                 .arg(QFileInfo(result.path).fileName()));
            emit backupInspected(true);
            showInspection(result.inspection);
        } else if (completedTask == TaskKind::Stage) {
            if (!androidImportTempPath_.isEmpty()) {
                QFile::remove(androidImportTempPath_);
                androidImportTempPath_.clear();
            }
            status_->setText(QStringLiteral("恢复数据已安全暂存，重启应用后自动应用"));
            emit restoreStaged();
            QMessageBox::information(
                this,
                QStringLiteral("等待重启恢复"),
                QStringLiteral("备份已验证并暂存。关闭并重新打开应用后，将在数据库启动前完成恢复；失败会自动回滚原数据。"));
        } else {
#if defined(Q_OS_ANDROID)
            androidExportTempPath_ = result.path;
            QString error;
            platform::BackupDocumentPlatform::clearResult();
            if (!platform::BackupDocumentPlatform::createDocument(
                    result.path, QFileInfo(result.path).fileName(), &error)) {
                QFile::remove(result.path);
                androidExportTempPath_.clear();
                status_->setText(error);
                return;
            }
            status_->setText(QStringLiteral("请选择备份文件的保存位置"));
            setBusy(true);
            documentPollTimer_->start();
#else
            status_->setText(QStringLiteral("完整备份已保存：%1")
                                 .arg(QFileInfo(result.path).fileName()));
            emit backupCreated(result.path);
#endif
        }
    });
    documentPollTimer_->setInterval(200);
    connect(documentPollTimer_, &QTimer::timeout,
            this, &BackupSettingsPanel::pollAndroidDocumentResult);
}

BackupSettingsPanel::~BackupSettingsPanel()
{
    if (watcher_->isRunning()) {
        watcher_->waitForFinished();
    }
}

QString BackupSettingsPanel::defaultBackupDirectory() const
{
    if (!sharedRoot_.isEmpty()) {
        return QDir(sharedRoot_).filePath(QStringLiteral("Backups"));
    }
    return QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
}

void BackupSettingsPanel::chooseExportPath()
{
    if (watcher_->isRunning()) {
        return;
    }
    const QString directory = defaultBackupDirectory();
    const QString suggested = services::LocalBackupService::suggestedFileName();
#if defined(Q_OS_ANDROID)
    Q_UNUSED(directory);
    const QString temporaryDirectory = QDir(dataRoot_).filePath(
        QStringLiteral(".backup-export-pending"));
    if (!QDir().mkpath(temporaryDirectory)) {
        status_->setText(QStringLiteral("无法创建备份导出临时目录"));
        return;
    }
    exportTo(QDir(temporaryDirectory).filePath(suggested));
#else
    const QString path = QFileDialog::getSaveFileName(
        this,
        QStringLiteral("导出完整备份"),
        QDir(directory).filePath(suggested),
        QStringLiteral("QuizApp 完整备份 (*.quizbackup)"));
    if (!path.isEmpty()) {
        exportTo(path.endsWith(QStringLiteral(".quizbackup"), Qt::CaseInsensitive)
            ? path : path + QStringLiteral(".quizbackup"));
    }
#endif
}

void BackupSettingsPanel::chooseRestorePath()
{
    if (watcher_->isRunning()) {
        return;
    }
#if defined(Q_OS_ANDROID)
    const QString temporaryDirectory = QDir(dataRoot_).filePath(
        QStringLiteral(".backup-import-pending"));
    if (!QDir().mkpath(temporaryDirectory)) {
        status_->setText(QStringLiteral("无法创建备份导入临时目录"));
        return;
    }
    androidImportTempPath_ = QDir(temporaryDirectory).filePath(
        QUuid::createUuid().toString(QUuid::WithoutBraces)
        + QStringLiteral(".quizbackup"));
    QString error;
    platform::BackupDocumentPlatform::clearResult();
    if (!platform::BackupDocumentPlatform::openDocument(androidImportTempPath_, &error)) {
        androidImportTempPath_.clear();
        status_->setText(error);
        return;
    }
    status_->setText(QStringLiteral("请选择要恢复的 .quizbackup 文件"));
    setBusy(true);
    documentPollTimer_->start();
#else
    const QString path = QFileDialog::getOpenFileName(
        this,
        QStringLiteral("选择完整备份"),
        defaultBackupDirectory(),
        QStringLiteral("QuizApp 完整备份 (*.quizbackup)"));
    if (!path.isEmpty()) {
        inspectForRestore(path);
    }
#endif
}

void BackupSettingsPanel::pollAndroidDocumentResult()
{
#if defined(Q_OS_ANDROID)
    const platform::BackupDocumentResult result = platform::BackupDocumentPlatform::result();
    if (result.state == platform::BackupDocumentState::Idle
        || result.state == platform::BackupDocumentState::Pending) {
        return;
    }
    documentPollTimer_->stop();
    platform::BackupDocumentPlatform::clearResult();
    setBusy(false);
    if (result.kind == platform::BackupDocumentKind::Export) {
        const QString temporaryPath = androidExportTempPath_;
        androidExportTempPath_.clear();
        QFile::remove(temporaryPath);
        if (result.state == platform::BackupDocumentState::Succeeded) {
            status_->setText(QStringLiteral("完整备份已保存到所选位置"));
            emit backupCreated(result.path);
        } else if (result.state == platform::BackupDocumentState::Cancelled) {
            status_->setText(QStringLiteral("已取消导出备份"));
        } else {
            status_->setText(result.error.isEmpty()
                ? QStringLiteral("导出备份失败") : result.error);
        }
        return;
    }
    if (result.kind == platform::BackupDocumentKind::Import) {
        if (result.state == platform::BackupDocumentState::Succeeded) {
            inspectForRestore(androidImportTempPath_);
        } else {
            QFile::remove(androidImportTempPath_);
            androidImportTempPath_.clear();
            status_->setText(result.state == platform::BackupDocumentState::Cancelled
                ? QStringLiteral("已取消选择备份")
                : (result.error.isEmpty() ? QStringLiteral("读取备份失败") : result.error));
        }
    }
#endif
}

void BackupSettingsPanel::exportTo(const QString &path)
{
    if (watcher_->isRunning() || path.isEmpty()) {
        return;
    }
    setBusy(true, QStringLiteral("正在准备完整备份"));
    taskKind_ = TaskKind::Export;
    const QString databasePath = databasePath_;
    const QString dataRoot = dataRoot_;
    const QString sharedRoot = sharedRoot_;
    const bool includeSecrets = includeSecretsChoice_->isChecked();
    QPointer<BackupSettingsPanel> safeThis(this);
    watcher_->setFuture(QtConcurrent::run([
        databasePath, dataRoot, sharedRoot, includeSecrets, path, safeThis]() {
        BackupTaskResult result;
        result.path = path;
        storage::Database database(QStringLiteral("backup-ui-%1")
            .arg(QUuid::createUuid().toString(QUuid::WithoutBraces)));
        if (!database.open(databasePath, &result.error)
            || !database.migrate(&result.error)) {
            return result;
        }
        QSettings settings;
        services::LocalBackupService service;
        result.success = service.create(
            path,
            database.connection(),
            databasePath,
            dataRoot,
            sharedRoot,
            settings,
            includeSecrets,
            QString::fromLatin1(QUIZAPP_APP_VERSION),
            QString::fromLatin1(QUIZAPP_BUILD_COMMIT),
            [safeThis](const QString &stage, qint64 completed, qint64 total) {
                if (!safeThis) return;
                QMetaObject::invokeMethod(
                    safeThis,
                    [safeThis, stage, completed, total] {
                        if (safeThis) safeThis->updateProgress(stage, completed, total);
                    },
                    Qt::QueuedConnection);
            },
            &result.error);
        return result;
    }));
}

void BackupSettingsPanel::inspectForRestore(const QString &path)
{
    if (watcher_->isRunning() || path.isEmpty()) {
        return;
    }
    inspectedPath_.clear();
    setBusy(true, QStringLiteral("正在校验完整备份"));
    taskKind_ = TaskKind::Inspect;
    QPointer<BackupSettingsPanel> safeThis(this);
    watcher_->setFuture(QtConcurrent::run([path, safeThis] {
        BackupTaskResult result;
        result.path = path;
        services::LocalBackupService service;
        result.inspection = service.inspect(
            path,
            true,
            [safeThis](const QString &stage, qint64 completed, qint64 total) {
                if (!safeThis) return;
                QMetaObject::invokeMethod(
                    safeThis,
                    [safeThis, stage, completed, total] {
                        if (safeThis) safeThis->updateProgress(stage, completed, total);
                    },
                    Qt::QueuedConnection);
            });
        result.success = result.inspection.valid;
        result.error = result.inspection.error;
        return result;
    }));
}

void BackupSettingsPanel::stageInspectedRestore()
{
    if (watcher_->isRunning() || inspectedPath_.isEmpty() || !inspection_.valid) {
        return;
    }
    if (previewDialog_) {
        previewDialog_->accept();
    }
    setBusy(true, QStringLiteral("正在暂存恢复数据"));
    taskKind_ = TaskKind::Stage;
    const QString path = inspectedPath_;
    const QString dataRoot = dataRoot_;
    QPointer<BackupSettingsPanel> safeThis(this);
    watcher_->setFuture(QtConcurrent::run([path, dataRoot, safeThis] {
        BackupTaskResult result;
        result.path = path;
        services::LocalBackupService service;
        result.success = service.stageRestore(
            path,
            dataRoot,
            [safeThis](const QString &stage, qint64 completed, qint64 total) {
                if (!safeThis) return;
                QMetaObject::invokeMethod(
                    safeThis,
                    [safeThis, stage, completed, total] {
                        if (safeThis) safeThis->updateProgress(stage, completed, total);
                    },
                    Qt::QueuedConnection);
            },
            &result.error);
        return result;
    }));
}

void BackupSettingsPanel::showInspection(const services::BackupInspection &inspection)
{
    if (previewDialog_) {
        previewDialog_->close();
        previewDialog_->deleteLater();
    }
    previewDialog_ = new QDialog(this);
    previewDialog_->setObjectName(QStringLiteral("backupPreviewDialog"));
    previewDialog_->setWindowTitle(QStringLiteral("确认恢复"));
    previewDialog_->setModal(true);
    previewDialog_->resize(width() < 700 ? 360 : 620, width() < 700 ? 610 : 500);
    previewDialog_->setAttribute(Qt::WA_DeleteOnClose);
    connect(previewDialog_, &QObject::destroyed, this, [this] { previewDialog_ = nullptr; });
    connect(previewDialog_, &QDialog::rejected, this, [this] {
        if (!androidImportTempPath_.isEmpty()) {
            QFile::remove(androidImportTempPath_);
            androidImportTempPath_.clear();
        }
    });
    auto *layout = new QVBoxLayout(previewDialog_);
    layout->setContentsMargins(18, 18, 18, 18);
    layout->setSpacing(12);
    auto *title = new QLabel(QStringLiteral("完整备份校验通过"), previewDialog_);
    title->setObjectName(QStringLiteral("dialogHeading"));
    auto *meta = new QLabel(
        QStringLiteral("版本 %1 · %2\n%3 · 数据 %4")
            .arg(inspection.appVersion.isEmpty() ? QStringLiteral("未知") : inspection.appVersion)
            .arg(inspection.createdAt.toLocalTime().toString(QStringLiteral("yyyy-MM-dd HH:mm")))
            .arg(inspection.platform)
            .arg(humanBytes(inspection.payloadBytes)),
        previewDialog_);
    meta->setObjectName(QStringLiteral("backupPreviewMeta"));
    meta->setWordWrap(true);
    auto *grid = new QGridLayout;
    const std::array<std::pair<QString, QString>, 6> counts{{
        {QStringLiteral("题库"), QStringLiteral("banks")},
        {QStringLiteral("题目"), QStringLiteral("questions")},
        {QStringLiteral("练习记录"), QStringLiteral("practiceSessions")},
        {QStringLiteral("错题"), QStringLiteral("wrongBook")},
        {QStringLiteral("考试"), QStringLiteral("examSessions")},
        {QStringLiteral("笔记"), QStringLiteral("notebooks")},
    }};
    for (qsizetype index = 0; index < counts.size(); ++index) {
        auto *card = new QFrame(previewDialog_);
        card->setObjectName(QStringLiteral("backupPreviewStat"));
        auto *cardLayout = new QVBoxLayout(card);
        cardLayout->setContentsMargins(10, 8, 10, 8);
        auto *value = new QLabel(countText(inspection.counts, counts.at(index).second), card);
        value->setObjectName(QStringLiteral("backupPreviewValue"));
        auto *caption = new QLabel(counts.at(index).first, card);
        caption->setObjectName(QStringLiteral("pageSupportingText"));
        cardLayout->addWidget(value);
        cardLayout->addWidget(caption);
        grid->addWidget(card, static_cast<int>(index) / 2, static_cast<int>(index) % 2);
    }
    auto *integrity = new QLabel(
        inspection.includesSecrets
            ? QStringLiteral("SHA-256 全量校验通过。此备份包含明文 API Key。")
            : QStringLiteral("SHA-256 全量校验通过。此备份不包含 API Key，恢复时保留本机密钥。"),
        previewDialog_);
    integrity->setObjectName(inspection.includesSecrets
        ? QStringLiteral("backupSecretWarning") : QStringLiteral("backupIntegrity"));
    integrity->setWordWrap(true);
    auto *warning = new QLabel(
        QStringLiteral("恢复会替换本机数据库、设置、笔记及共享题库内容。操作先暂存，重启时应用；任何写入失败都会回滚。"),
        previewDialog_);
    warning->setObjectName(QStringLiteral("pageSupportingText"));
    warning->setWordWrap(true);
    auto *actions = new QHBoxLayout;
    auto *cancel = new QPushButton(QStringLiteral("取消"), previewDialog_);
    auto *confirm = new QPushButton(QStringLiteral("确认恢复"), previewDialog_);
    confirm->setObjectName(QStringLiteral("backupConfirmRestoreButton"));
    connect(cancel, &QPushButton::clicked, previewDialog_, &QDialog::reject);
    connect(confirm, &QPushButton::clicked, this, &BackupSettingsPanel::stageInspectedRestore);
    actions->addStretch();
    actions->addWidget(cancel);
    actions->addWidget(confirm);
    layout->addWidget(title);
    layout->addWidget(meta);
    layout->addLayout(grid);
    layout->addWidget(integrity);
    layout->addWidget(warning);
    layout->addStretch();
    layout->addLayout(actions);
    previewDialog_->open();
}

void BackupSettingsPanel::setBusy(bool busy, const QString &status)
{
    exportButton_->setEnabled(!busy);
    restoreButton_->setEnabled(!busy);
    includeSecretsChoice_->setEnabled(!busy);
    progress_->setVisible(busy);
    if (busy) {
        progress_->setRange(0, 0);
        progress_->setValue(0);
    }
    if (!status.isEmpty()) {
        status_->setText(status);
    }
}

void BackupSettingsPanel::updateProgress(
    const QString &stage,
    qint64 completed,
    qint64 total)
{
    status_->setText(stageText(stage));
    if (total > 0) {
        progress_->setRange(0, 1000);
        progress_->setValue(static_cast<int>(std::clamp<qint64>(
            completed * 1000 / total, 0, 1000)));
    } else {
        progress_->setRange(0, 0);
    }
}

} // namespace quizapp::ui
