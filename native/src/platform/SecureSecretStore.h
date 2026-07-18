#pragma once

#include <QString>

class QSettings;

namespace quizapp::platform {

class SecureSecretStore final {
public:
    static QString readSecret(
        const QString &name,
        QSettings &settings,
        QString *error = nullptr);
    static bool writeSecret(
        const QString &name,
        const QString &value,
        QSettings &settings,
        QString *error = nullptr);
    static bool removeSecret(
        const QString &name,
        QSettings &settings,
        QString *error = nullptr);
    static QString settingsKey(const QString &name);
};

} // namespace quizapp::platform
