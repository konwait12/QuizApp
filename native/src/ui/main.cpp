#include "platform/LegacyWebMigrationPlatform.h"
#include "platform/SharedStoragePlatform.h"
#include "services/DefaultBankBundleBootstrapService.h"
#include "services/LegacyMigrationService.h"
#include "services/LocalBackupService.h"
#include "storage/Database.h"
#include "ui/AppFont.h"
#include "ui/AppWindow.h"

#include <QApplication>
#include <QDebug>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFutureWatcher>
#include <QLabel>
#include <QMessageBox>
#include <QPointer>
#include <QProgressBar>
#include <QSettings>
#include <QSqlQuery>
#include <QStandardPaths>
#include <QThread>
#include <QVBoxLayout>
#include <QWidget>
#include <QtConcurrent/QtConcurrentRun>

#include <memory>

namespace {

void importLegacyWebData(quizapp::storage::Database &database)
{
#if defined(Q_OS_ANDROID)
    QSettings settings;
    quizapp::services::LegacyMigrationService migrationService;
    auto applyLegacySettings = [&](QString *error) {
        int applied = 0;
        return migrationService.applyUiSettings(
            database.connection(), settings, &applied, error);
    };
    if (settings.value(QStringLiteral("migration/legacyWebV1Checked"), false).toBool()) {
        return;
    }
    QSqlQuery existing(database.connection());
    if (existing.exec(QStringLiteral("SELECT 1 FROM legacy_migrations LIMIT 1"))
        && existing.next()) {
        QString settingsError;
        if (!applyLegacySettings(&settingsError)) {
            QMessageBox::warning(
                nullptr,
                QStringLiteral("旧版设置迁移未完成"),
                QStringLiteral("旧版数据仍保留在本机：%1").arg(settingsError));
            return;
        }
        settings.setValue(QStringLiteral("migration/legacyWebV1Checked"), true);
        return;
    }
    if (!quizapp::platform::LegacyWebMigrationPlatform::hasSourceData()) {
        qInfo() << "QuizApp startup: no legacy WebView data detected";
        settings.setValue(QStringLiteral("migration/legacyWebV1Checked"), true);
        return;
    }

    QString startError;
    if (!quizapp::platform::LegacyWebMigrationPlatform::start(
            QStringLiteral("legacy-webview"), &startError)) {
        return;
    }
    QElapsedTimer timeout;
    timeout.start();
    auto status = quizapp::platform::LegacyWebMigrationPlatform::status();
    while (status == quizapp::platform::LegacyWebExportStatus::Running
           && timeout.elapsed() < 5 * 60 * 1000) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        QThread::msleep(10);
        status = quizapp::platform::LegacyWebMigrationPlatform::status();
    }
    if (status == quizapp::platform::LegacyWebExportStatus::NoData) {
        settings.setValue(QStringLiteral("migration/legacyWebV1Checked"), true);
        quizapp::platform::LegacyWebMigrationPlatform::clearResult();
        return;
    }
    if (status != quizapp::platform::LegacyWebExportStatus::Complete) {
        const QString exportError = status == quizapp::platform::LegacyWebExportStatus::Failed
            ? quizapp::platform::LegacyWebMigrationPlatform::error()
            : QStringLiteral("旧版数据提取超时");
        QMessageBox::warning(
            nullptr,
            QStringLiteral("旧版数据迁移未完成"),
            QStringLiteral("旧数据未被删除，下次启动会重试：%1").arg(exportError));
        return;
    }

    QFile packageFile(quizapp::platform::LegacyWebMigrationPlatform::resultPath());
    if (!packageFile.open(QIODevice::ReadOnly)) {
        QMessageBox::warning(
            nullptr,
            QStringLiteral("旧版数据迁移未完成"),
            QStringLiteral("无法读取旧版迁移包，下次启动会重试"));
        return;
    }
    const auto result = migrationService.importPackage(
        packageFile.readAll(), database.connection());
    packageFile.close();
    if (result.status == quizapp::services::LegacyMigrationStatus::Imported
        || result.status == quizapp::services::LegacyMigrationStatus::AlreadyImported) {
        QString settingsError;
        if (!applyLegacySettings(&settingsError)) {
            QMessageBox::warning(
                nullptr,
                QStringLiteral("旧版设置迁移未完成"),
                QStringLiteral("旧版数据已安全暂存，但设置尚未应用：%1")
                    .arg(settingsError));
            return;
        }
        settings.setValue(QStringLiteral("migration/legacyWebV1Checked"), true);
        quizapp::platform::LegacyWebMigrationPlatform::clearResult();
        return;
    }
    QMessageBox::warning(
        nullptr,
        QStringLiteral("旧版数据迁移未完成"),
        QStringLiteral("迁移事务已回滚，旧数据未被删除：%1").arg(result.error));
#else
    Q_UNUSED(database);
#endif
}

} // namespace

int main(int argc, char *argv[])
{
    QApplication application(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("QuizApp"));
    QCoreApplication::setApplicationName(QStringLiteral("QuizAppNative"));
    QCoreApplication::setApplicationVersion(QStringLiteral("2.0.0-alpha.3"));
    quizapp::ui::configureApplicationFont();

    const QString dataRoot = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (dataRoot.isEmpty() || !QDir().mkpath(dataRoot)) {
        QMessageBox::critical(
            nullptr, QStringLiteral("启动失败"), QStringLiteral("无法创建应用数据目录"));
        return 1;
    }
    qInfo() << "QuizApp startup: application data ready";

    const QString databasePath = QDir(dataRoot).filePath(QStringLiteral("quizapp.sqlite"));
    quizapp::services::LocalBackupService backupService;
    if (backupService.hasPendingRestore(dataRoot)) {
        const QString sharedRoot = quizapp::platform::SharedStoragePlatform::defaultRootPath(dataRoot);
        const bool needsPermission = quizapp::platform::SharedStoragePlatform::requiresDirectAccessPermission()
            && !quizapp::platform::SharedStoragePlatform::hasDirectAccess();
        if (needsPermission) {
            QMessageBox::warning(
                nullptr,
                QStringLiteral("恢复尚未应用"),
                QStringLiteral("完整备份包含共享题库文件。请先授予文件访问权限，再重新打开应用完成恢复。"));
            quizapp::platform::SharedStoragePlatform::requestDirectAccess();
        } else {
            QSettings restoreSettings;
            QString restoreError;
            if (!backupService.applyPendingRestore(
                    dataRoot, databasePath, sharedRoot, restoreSettings, &restoreError)) {
                QMessageBox::warning(
                    nullptr,
                    QStringLiteral("恢复未完成"),
                    QStringLiteral("原有数据已保留：%1").arg(restoreError));
            } else {
                qInfo() << "QuizApp startup: pending local backup restored";
            }
        }
    }
#if defined(Q_OS_ANDROID) && defined(QUIZAPP_HAS_DEFAULT_BANK_BUNDLE)
    std::unique_ptr<QWidget> startupWindow;
    QLabel *startupStatus = nullptr;
    QProgressBar *startupProgress = nullptr;
    QSettings startupSettings;
    const bool needsLegacyMigrationCheck = !startupSettings.value(
        QStringLiteral("migration/legacyWebV1Checked"), false).toBool();
    if (!QFileInfo::exists(databasePath) || needsLegacyMigrationCheck) {
        startupWindow = std::make_unique<QWidget>();
        startupWindow->setObjectName(QStringLiteral("startupWindow"));
        startupWindow->setWindowTitle(QStringLiteral("QuizApp"));
        startupWindow->setStyleSheet(QStringLiteral(
            "#startupWindow { background: #f5f8f5; color: #18231c; }"
            "QLabel#startupTitle { color: #18231c; font-size: 28px; font-weight: 700; }"
            "QLabel#startupStatus { color: #526157; font-size: 15px; }"
            "QProgressBar { min-height: 8px; max-height: 8px; border: 0; "
            "background: #dce5de; border-radius: 4px; }"
            "QProgressBar::chunk { background: #2f7650; border-radius: 4px; }"));
        auto *layout = new QVBoxLayout(startupWindow.get());
        layout->setContentsMargins(32, 32, 32, 32);
        layout->setSpacing(14);
        layout->addStretch();
        auto *title = new QLabel(QStringLiteral("QuizApp"), startupWindow.get());
        title->setObjectName(QStringLiteral("startupTitle"));
        title->setAlignment(Qt::AlignCenter);
        layout->addWidget(title);
        startupStatus = new QLabel(
            QStringLiteral("正在准备 27考研题库包"), startupWindow.get());
        startupStatus->setObjectName(QStringLiteral("startupStatus"));
        startupStatus->setAlignment(Qt::AlignCenter);
        layout->addWidget(startupStatus);
        startupProgress = new QProgressBar(startupWindow.get());
        startupProgress->setRange(0, 0);
        startupProgress->setTextVisible(false);
        layout->addWidget(startupProgress);
        layout->addStretch();
        startupWindow->showMaximized();
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    }
    using BootstrapResult = quizapp::services::DefaultBankBundleBootstrapResult;
    QFutureWatcher<BootstrapResult> bootstrapWatcher;
    QEventLoop bootstrapLoop;
    QPointer<QLabel> safeStartupStatus(startupStatus);
    QPointer<QProgressBar> safeStartupProgress(startupProgress);
    QObject::connect(&bootstrapWatcher, &QFutureWatcher<BootstrapResult>::finished,
                     &bootstrapLoop, &QEventLoop::quit);
    qInfo() << "QuizApp startup: preparing bundled question banks";
    bootstrapWatcher.setFuture(QtConcurrent::run(
        [dataRoot, databasePath, safeStartupStatus, safeStartupProgress]() {
            quizapp::services::DefaultBankBundleBootstrapService bootstrapService;
            return bootstrapService.install(
                QStringLiteral("assets:/quizapp/27-postgraduate-bank-bundle"),
                dataRoot,
                databasePath,
                {},
                [safeStartupStatus, safeStartupProgress](
                    const QString &phase, int completed, int total) {
                    QMetaObject::invokeMethod(
                        qApp,
                        [safeStartupStatus, safeStartupProgress,
                         phase, completed, total]() {
                            if (!safeStartupStatus || !safeStartupProgress) return;
                            safeStartupStatus->setText(phase);
                            safeStartupProgress->setRange(0, total);
                            safeStartupProgress->setValue(completed);
                        },
                        Qt::QueuedConnection);
                });
        }));
    bootstrapLoop.exec();
    const BootstrapResult bootstrap = bootstrapWatcher.result();
    qInfo() << "QuizApp startup: bundled question banks finished with status"
            << static_cast<int>(bootstrap.status);
    if (bootstrap.status
        == quizapp::services::DefaultBankBundleBootstrapStatus::Failed) {
        QMessageBox::warning(
            nullptr,
            QStringLiteral("预置题库安装失败"),
            QStringLiteral("27考研题库包未能安全安装，原有数据未被覆盖：%1")
                .arg(bootstrap.error));
    }
#endif
    quizapp::storage::Database database(QStringLiteral("quizapp-main"));
    QString databaseError;
    if (!database.open(databasePath, &databaseError)
        || !database.migrate(&databaseError)) {
        QMessageBox::critical(
            nullptr,
            QStringLiteral("数据库错误"),
            QStringLiteral("无法初始化本地数据库：%1").arg(databaseError));
        return 2;
    }
    qInfo() << "QuizApp startup: database ready";
#if defined(Q_OS_ANDROID)
    if (startupWindow && needsLegacyMigrationCheck) {
        startupStatus->setText(QStringLiteral("正在迁移旧版学习数据"));
        startupProgress->setRange(0, 0);
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    }
#endif
    importLegacyWebData(database);
    qInfo() << "QuizApp startup: legacy migration check finished";

    quizapp::ui::AppWindow window(databasePath, dataRoot);
#if defined(Q_OS_ANDROID)
    window.showMaximized();
    if (startupWindow) {
        startupWindow->close();
        startupWindow.reset();
    }
#else
    window.show();
#endif
    qInfo() << "QuizApp startup: main window shown";
    return application.exec();
}
