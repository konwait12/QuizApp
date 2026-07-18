#include "platform/AppUpdatePlatform.h"

#include <QDesktopServices>
#include <QFileInfo>
#include <QUrl>

#ifdef Q_OS_ANDROID
#include <QJniObject>
#endif

namespace quizapp::platform {

bool AppUpdatePlatform::installDownloadedPackage(const QString &path, QString *error)
{
    if (!QFileInfo::exists(path)) {
        if (error) {
            *error = QStringLiteral("下载文件不存在");
        }
        return false;
    }
#ifdef Q_OS_ANDROID
    const QJniObject javaPath = QJniObject::fromString(path);
    const jboolean started = QJniObject::callStaticMethod<jboolean>(
        "org/quizapp/platform/QuizAppActivity",
        "installDownloadedPackage",
        "(Ljava/lang/String;)Z",
        javaPath.object<jstring>());
    if (!started && error) {
        *error = QStringLiteral("系统安装器未能启动，请在 Release 页面手动下载");
    }
    return started;
#else
    const bool started = QDesktopServices::openUrl(QUrl::fromLocalFile(path));
    if (!started && error) {
        *error = QStringLiteral("无法打开下载文件");
    }
    return started;
#endif
}

} // namespace quizapp::platform
