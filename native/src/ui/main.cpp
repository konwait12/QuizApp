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
    QCoreApplication::setApplicationVersion(QStringLiteral("2.0.0-alpha.2"));
    quizapp::ui::configureApplicationFont();

    const QString dataRoot = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (dataRoot.isEmpty() || !QDir().mkpath(dataRoot)) {
        QMessageBox::critical(
            nullptr, QStringLiteral("启动失败"), QStringLiteral("无法创建应用数据目录"));
        return 1;
    }

    const QString databasePath = QDir(dataRoot).filePath(QStringLiteral("quizapp.sqlite"));
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
