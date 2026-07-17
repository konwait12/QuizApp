#pragma once

#include <QString>

#include <functional>

namespace quizapp::services {

enum class DefaultBankBundleBootstrapStatus {
    Unavailable,
    Installed,
    SkippedNonEmpty,
    Failed,
};

struct DefaultBankBundleBootstrapResult {
    DefaultBankBundleBootstrapStatus status = DefaultBankBundleBootstrapStatus::Unavailable;
    QString displayName;
    qsizetype questionCount = 0;
    int sectionCount = 0;
    int blobCount = 0;
    QString error;

    bool installed() const
    {
        return status == DefaultBankBundleBootstrapStatus::Installed;
    }
};

class DefaultBankBundleBootstrapService final {
public:
    using CopyGate = std::function<bool(const QString &relativePath)>;

    DefaultBankBundleBootstrapResult install(
        const QString &bundleRoot,
        const QString &dataRoot,
        const QString &databasePath,
        const CopyGate &copyGate = {}) const;
};

} // namespace quizapp::services
