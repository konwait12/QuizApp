#include "platform/LegacyWebMigrationPlatform.h"

#if defined(Q_OS_ANDROID)
#include <QJniObject>
#endif

namespace quizapp::platform {
namespace {

void clearError(QString *error)
{
    if (error) error->clear();
}

} // namespace

bool LegacyWebMigrationPlatform::hasSourceData()
{
#if defined(Q_OS_ANDROID)
    return QJniObject::callStaticMethod<jboolean>(
        "org/quizapp/platform/LegacyMigrationBridge", "hasSourceData", "()Z");
#else
    return false;
#endif
}

bool LegacyWebMigrationPlatform::start(const QString &sourceVersion, QString *error)
{
    clearError(error);
#if defined(Q_OS_ANDROID)
    const QJniObject version = QJniObject::fromString(sourceVersion);
    const bool started = QJniObject::callStaticMethod<jboolean>(
        "org/quizapp/platform/LegacyMigrationBridge",
        "start",
        "(Ljava/lang/String;)Z",
        version.object<jstring>());
    if (!started && error) {
        *error = QStringLiteral("无法启动旧版 WebView 数据提取器");
    }
    return started;
#else
    Q_UNUSED(sourceVersion);
    if (error) *error = QStringLiteral("当前平台没有旧版 WebView 数据");
    return false;
#endif
}

LegacyWebExportStatus LegacyWebMigrationPlatform::status()
{
#if defined(Q_OS_ANDROID)
    const int value = QJniObject::callStaticMethod<jint>(
        "org/quizapp/platform/LegacyMigrationBridge", "status", "()I");
    switch (value) {
    case 0: return LegacyWebExportStatus::Idle;
    case 1: return LegacyWebExportStatus::Running;
    case 2: return LegacyWebExportStatus::Complete;
    case 3: return LegacyWebExportStatus::Failed;
    case 4: return LegacyWebExportStatus::NoData;
    default: return LegacyWebExportStatus::Unavailable;
    }
#else
    return LegacyWebExportStatus::Unavailable;
#endif
}

QString LegacyWebMigrationPlatform::resultPath()
{
#if defined(Q_OS_ANDROID)
    const QJniObject value = QJniObject::callStaticObjectMethod(
        "org/quizapp/platform/LegacyMigrationBridge",
        "resultPath",
        "()Ljava/lang/String;");
    return value.isValid() ? value.toString() : QString();
#else
    return {};
#endif
}

QString LegacyWebMigrationPlatform::error()
{
#if defined(Q_OS_ANDROID)
    const QJniObject value = QJniObject::callStaticObjectMethod(
        "org/quizapp/platform/LegacyMigrationBridge",
        "error",
        "()Ljava/lang/String;");
    return value.isValid() ? value.toString() : QString();
#else
    return {};
#endif
}

void LegacyWebMigrationPlatform::clearResult()
{
#if defined(Q_OS_ANDROID)
    QJniObject::callStaticMethod<void>(
        "org/quizapp/platform/LegacyMigrationBridge", "clearResult", "()V");
#endif
}

} // namespace quizapp::platform
