#include "services/DefaultBankBundleBootstrapService.h"
#include "storage/Database.h"
#include "ui/AppFont.h"
#include "ui/AppWindow.h"

#include <QApplication>
#include <QDir>
#include <QMessageBox>
#include <QStandardPaths>

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

    const QString databasePath = QDir(dataRoot).filePath(QStringLiteral("quizapp.sqlite"));
#if defined(Q_OS_ANDROID) && defined(QUIZAPP_HAS_DEFAULT_BANK_BUNDLE)
    quizapp::services::DefaultBankBundleBootstrapService bootstrapService;
    const auto bootstrap = bootstrapService.install(
        QStringLiteral("assets:/quizapp/27-postgraduate-bank-bundle"),
        dataRoot,
        databasePath);
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

    quizapp::ui::AppWindow window(databasePath, dataRoot);
#if defined(Q_OS_ANDROID)
    window.showMaximized();
#else
    window.show();
#endif
    return application.exec();
}
