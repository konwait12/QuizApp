#include "platform/BackupDocumentPlatform.h"

#if defined(Q_OS_ANDROID)
#include <QJniObject>
#endif

namespace quizapp::platform {
namespace {

void clearError(QString *error)
{
    if (error) error->clear();
}

#if defined(Q_OS_ANDROID)
constexpr auto kBridgeClass = "org/quizapp/platform/BackupDocumentBridge";
#endif

} // namespace

bool BackupDocumentPlatform::openDocument(const QString &destinationPath, QString *error)
{
    clearError(error);
#if defined(Q_OS_ANDROID)
    const QJniObject destination = QJniObject::fromString(destinationPath);
    const bool started = QJniObject::callStaticMethod<jboolean>(
        kBridgeClass, "openDocument", "(Ljava/lang/String;)Z",
        destination.object<jstring>());
    if (!started && error) *error = QStringLiteral("无法打开 Android 系统文件选择器");
    return started;
#else
    Q_UNUSED(destinationPath);
    if (error) *error = QStringLiteral("当前平台不使用 Android 文档选择器");
    return false;
#endif
}

bool BackupDocumentPlatform::createDocument(
    const QString &sourcePath, const QString &suggestedName, QString *error)
{
    clearError(error);
#if defined(Q_OS_ANDROID)
    const QJniObject source = QJniObject::fromString(sourcePath);
    const QJniObject name = QJniObject::fromString(suggestedName);
    const bool started = QJniObject::callStaticMethod<jboolean>(
        kBridgeClass, "createDocument", "(Ljava/lang/String;Ljava/lang/String;)Z",
        source.object<jstring>(), name.object<jstring>());
    if (!started && error) *error = QStringLiteral("无法打开 Android 系统另存为界面");
    return started;
#else
    Q_UNUSED(sourcePath);
    Q_UNUSED(suggestedName);
    if (error) *error = QStringLiteral("当前平台不使用 Android 文档选择器");
    return false;
#endif
}

BackupDocumentResult BackupDocumentPlatform::result()
{
    BackupDocumentResult result;
#if defined(Q_OS_ANDROID)
    result.state = static_cast<BackupDocumentState>(
        QJniObject::callStaticMethod<jint>(kBridgeClass, "resultState", "()I"));
    result.kind = static_cast<BackupDocumentKind>(
        QJniObject::callStaticMethod<jint>(kBridgeClass, "resultKind", "()I"));
    const QJniObject path = QJniObject::callStaticObjectMethod(
        kBridgeClass, "resultPath", "()Ljava/lang/String;");
    const QJniObject message = QJniObject::callStaticObjectMethod(
        kBridgeClass, "resultError", "()Ljava/lang/String;");
    if (path.isValid()) result.path = path.toString();
    if (message.isValid()) result.error = message.toString();
#endif
    return result;
}

void BackupDocumentPlatform::clearResult()
{
#if defined(Q_OS_ANDROID)
    QJniObject::callStaticMethod<void>(kBridgeClass, "clearResult", "()V");
#endif
}

} // namespace quizapp::platform
