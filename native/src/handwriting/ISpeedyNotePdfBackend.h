#pragma once

#include <QHash>
#include <QList>
#include <QString>

#include <memory>

class PdfProvider;

namespace quizapp::handwriting {

class ISpeedyNotePdfBackend {
public:
    virtual ~ISpeedyNotePdfBackend() = default;

    virtual bool isAvailable() const = 0;
    virtual std::unique_ptr<PdfProvider> createProvider(const QString& path) const = 0;
    virtual bool materialize(const QString& originPath,
                             const QString& bundledPath,
                             const QList<int>& originalPages,
                             QHash<int, int>& pageMap,
                             QString* error) const = 0;
};

void setSpeedyNotePdfBackend(std::shared_ptr<ISpeedyNotePdfBackend> backend);
std::shared_ptr<ISpeedyNotePdfBackend> speedyNotePdfBackend();

} // namespace quizapp::handwriting
