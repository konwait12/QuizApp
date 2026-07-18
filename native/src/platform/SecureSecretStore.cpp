#include "platform/SecureSecretStore.h"

#include <QByteArray>
#include <QSettings>

#if defined(Q_OS_ANDROID)
#include <QJniObject>
#elif defined(Q_OS_WIN)
#include <windows.h>
#include <wincrypt.h>
#endif

namespace quizapp::platform {
namespace {

void clearError(QString *error)
{
    if (error) error->clear();
}

bool fail(QString *error, const QString &message)
{
    if (error) *error = message;
    return false;
}

#if defined(Q_OS_WIN)
QByteArray entropyFor(const QString &name)
{
    return QByteArrayLiteral("QuizApp/SecureSecretStore/v1/") + name.toUtf8();
}

QByteArray protect(const QByteArray &plain, const QString &name, QString *error)
{
    QByteArray entropy = entropyFor(name);
    DATA_BLOB input{
        static_cast<DWORD>(plain.size()),
        reinterpret_cast<BYTE *>(const_cast<char *>(plain.constData()))};
    DATA_BLOB salt{
        static_cast<DWORD>(entropy.size()),
        reinterpret_cast<BYTE *>(entropy.data())};
    DATA_BLOB output{};
    if (!CryptProtectData(&input, L"QuizApp", &salt, nullptr, nullptr,
                          CRYPTPROTECT_UI_FORBIDDEN, &output)) {
        fail(error, QStringLiteral("Windows 安全存储写入失败"));
        return {};
    }
    const QByteArray encrypted(
        reinterpret_cast<const char *>(output.pbData), static_cast<qsizetype>(output.cbData));
    LocalFree(output.pbData);
    return encrypted;
}

QByteArray unprotect(const QByteArray &encrypted, const QString &name, QString *error)
{
    QByteArray entropy = entropyFor(name);
    DATA_BLOB input{
        static_cast<DWORD>(encrypted.size()),
        reinterpret_cast<BYTE *>(const_cast<char *>(encrypted.constData()))};
    DATA_BLOB salt{
        static_cast<DWORD>(entropy.size()),
        reinterpret_cast<BYTE *>(entropy.data())};
    DATA_BLOB output{};
    if (!CryptUnprotectData(&input, nullptr, &salt, nullptr, nullptr,
                            CRYPTPROTECT_UI_FORBIDDEN, &output)) {
        fail(error, QStringLiteral("Windows 安全存储读取失败"));
        return {};
    }
    const QByteArray plain(
        reinterpret_cast<const char *>(output.pbData), static_cast<qsizetype>(output.cbData));
    LocalFree(output.pbData);
    return plain;
}
#endif

} // namespace

QString SecureSecretStore::settingsKey(const QString &name)
{
    return QStringLiteral("secure/%1").arg(name);
}

QString SecureSecretStore::readSecret(
    const QString &name, QSettings &settings, QString *error)
{
    clearError(error);
#if defined(Q_OS_ANDROID)
    Q_UNUSED(settings);
    const QJniObject key = QJniObject::fromString(name);
    const QJniObject value = QJniObject::callStaticObjectMethod(
        "org/quizapp/platform/SecureSecretBridge",
        "readSecret",
        "(Ljava/lang/String;)Ljava/lang/String;",
        key.object<jstring>());
    if (!value.isValid()) {
        fail(error, QStringLiteral("Android 安全存储读取失败"));
        return {};
    }
    return value.toString();
#elif defined(Q_OS_WIN)
    const QByteArray encoded = settings.value(settingsKey(name)).toByteArray();
    if (encoded.isEmpty()) return {};
    const QByteArray encrypted = QByteArray::fromBase64(encoded);
    if (encrypted.isEmpty()) {
        fail(error, QStringLiteral("安全存储密文格式无效"));
        return {};
    }
    return QString::fromUtf8(unprotect(encrypted, name, error));
#else
    Q_UNUSED(name);
    Q_UNUSED(settings);
    fail(error, QStringLiteral("当前平台尚未提供安全密钥存储"));
    return {};
#endif
}

bool SecureSecretStore::writeSecret(
    const QString &name, const QString &value, QSettings &settings, QString *error)
{
    clearError(error);
    if (value.isEmpty()) return removeSecret(name, settings, error);
#if defined(Q_OS_ANDROID)
    Q_UNUSED(settings);
    const QJniObject key = QJniObject::fromString(name);
    const QJniObject secret = QJniObject::fromString(value);
    const bool stored = QJniObject::callStaticMethod<jboolean>(
        "org/quizapp/platform/SecureSecretBridge",
        "writeSecret",
        "(Ljava/lang/String;Ljava/lang/String;)Z",
        key.object<jstring>(), secret.object<jstring>());
    return stored || fail(error, QStringLiteral("Android 安全存储写入失败"));
#elif defined(Q_OS_WIN)
    const QByteArray encrypted = protect(value.toUtf8(), name, error);
    if (encrypted.isEmpty()) return false;
    settings.setValue(settingsKey(name), encrypted.toBase64());
    settings.sync();
    return settings.status() == QSettings::NoError
        || fail(error, QStringLiteral("安全存储密文保存失败"));
#else
    Q_UNUSED(name);
    Q_UNUSED(value);
    Q_UNUSED(settings);
    return fail(error, QStringLiteral("当前平台尚未提供安全密钥存储"));
#endif
}

bool SecureSecretStore::removeSecret(
    const QString &name, QSettings &settings, QString *error)
{
    clearError(error);
#if defined(Q_OS_ANDROID)
    Q_UNUSED(settings);
    const QJniObject key = QJniObject::fromString(name);
    const bool removed = QJniObject::callStaticMethod<jboolean>(
        "org/quizapp/platform/SecureSecretBridge",
        "removeSecret",
        "(Ljava/lang/String;)Z",
        key.object<jstring>());
    return removed || fail(error, QStringLiteral("Android 安全存储删除失败"));
#elif defined(Q_OS_WIN)
    settings.remove(settingsKey(name));
    settings.sync();
    return settings.status() == QSettings::NoError
        || fail(error, QStringLiteral("安全存储密文删除失败"));
#else
    Q_UNUSED(name);
    Q_UNUSED(settings);
    return fail(error, QStringLiteral("当前平台尚未提供安全密钥存储"));
#endif
}

} // namespace quizapp::platform
