#pragma once

#include <QString>

namespace quizapp::services {

struct SharedStorageLayout {
    QString root;
    QString questionBanks;
    QString backups;
    QString exports;
    QString notes;
    QString recycleBin;
    QString error;

    bool ready() const { return error.isEmpty() && !root.isEmpty(); }
};

class SharedStorageService final {
public:
    static QString desktopRootForDataRoot(const QString &dataRoot);
    SharedStorageLayout prepare(const QString &rootPath) const;
};

} // namespace quizapp::services
