#include "platform/SharedStoragePlatform.h"

#include "services/SharedStorageService.h"

#include <QDesktopServices>
#include <QDir>
#include <QUrl>

#if defined(Q_OS_ANDROID)
#include <QJniObject>
#endif

namespace quizapp::platform {
namespace {

void clearError(QString *error)
{
    if (error) {
        error->clear();
    }
}

} // namespace

QString SharedStoragePlatform::defaultRootPath(const QString &dataRoot)
{
#if defined(Q_OS_ANDROID)
    const QJniObject value = QJniObject::callStaticObjectMethod(
        "org/quizapp/platform/SharedStorageBridge",
        "rootPath",
        "()Ljava/lang/String;");
    return value.isValid() ? value.toString() : QString();
#else
    return services::SharedStorageService::desktopRootForDataRoot(dataRoot);
#endif
}

bool SharedStoragePlatform::hasDirectAccess()
{
#if defined(Q_OS_ANDROID)
    return QJniObject::callStaticMethod<jboolean>(
        "org/quizapp/platform/SharedStorageBridge",
        "hasDirectAccess",
        "()Z");
#else
    return true;
#endif
}

bool SharedStoragePlatform::requiresDirectAccessPermission()
{
#if defined(Q_OS_ANDROID)
    return QJniObject::callStaticMethod<jboolean>(
        "org/quizapp/platform/SharedStorageBridge",
        "requiresDirectAccessPermission",
        "()Z");
#else
    return false;
#endif
}

bool SharedStoragePlatform::requestDirectAccess(QString *error)
{
    clearError(error);
#if defined(Q_OS_ANDROID)
    const bool opened = QJniObject::callStaticMethod<jboolean>(
        "org/quizapp/platform/SharedStorageBridge",
        "requestDirectAccess",
        "()Z");
    if (!opened && error) {
        *error = QStringLiteral("无法打开 Android 文件访问授权页");
    }
    return opened;
#else
    return true;
#endif
}

bool SharedStoragePlatform::openInSystemFileManager(
    const QString &path,
    QString *error)
{
    clearError(error);
#if defined(Q_OS_ANDROID)
    const QJniObject javaPath = QJniObject::fromString(QDir::cleanPath(path));
    const bool opened = QJniObject::callStaticMethod<jboolean>(
        "org/quizapp/platform/SharedStorageBridge",
        "openDirectory",
        "(Ljava/lang/String;)Z",
        javaPath.object<jstring>());
#else
    const bool opened = QDesktopServices::openUrl(QUrl::fromLocalFile(path));
#endif
    if (!opened && error) {
        *error = QStringLiteral("系统文件管理器无法打开该目录");
    }
    return opened;
}

} // namespace quizapp::platform
