#include "handwriting/ISpeedyNotePdfBackend.h"

#include "pdf/PdfMaterializer.h"
#include "pdf/PdfProvider.h"

#include <mutex>
#include <utility>

namespace {

std::mutex backendMutex;
std::shared_ptr<quizapp::handwriting::ISpeedyNotePdfBackend> backendInstance;

} // namespace

namespace quizapp::handwriting {

void setSpeedyNotePdfBackend(std::shared_ptr<ISpeedyNotePdfBackend> backend)
{
    const std::lock_guard<std::mutex> lock(backendMutex);
    backendInstance = std::move(backend);
}

std::shared_ptr<ISpeedyNotePdfBackend> speedyNotePdfBackend()
{
    const std::lock_guard<std::mutex> lock(backendMutex);
    return backendInstance;
}

} // namespace quizapp::handwriting

std::unique_ptr<PdfProvider> PdfProvider::create(const QString& pdfPath)
{
    const auto backend = quizapp::handwriting::speedyNotePdfBackend();
    return backend && backend->isAvailable() ? backend->createProvider(pdfPath) : nullptr;
}

bool PdfProvider::isAvailable()
{
    const auto backend = quizapp::handwriting::speedyNotePdfBackend();
    return backend && backend->isAvailable();
}

bool PdfMaterializer::materialize(const QString& originPath,
                                  const QString& bundledAbsPath,
                                  const QList<int>& originalPagesToAdd,
                                  QHash<int, int>& pageMap,
                                  QString* errorOut)
{
    const auto backend = quizapp::handwriting::speedyNotePdfBackend();
    if (!backend || !backend->isAvailable()) {
        if (errorOut) {
            *errorOut = QStringLiteral("PDF backend is not configured");
        }
        return false;
    }
    return backend->materialize(originPath,
                                bundledAbsPath,
                                originalPagesToAdd,
                                pageMap,
                                errorOut);
}
