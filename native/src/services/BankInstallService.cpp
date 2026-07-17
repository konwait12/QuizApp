#include "services/BankInstallService.h"

#include "repositories/IQuestionRepository.h"
#include "services/BlobStore.h"
#include "services/LegacyBankImporter.h"

namespace quizapp::services {

BankInstallResult BankInstallService::installJson(
    const QByteArray &json,
    const QString &sourceKey,
    const BlobStore &blobStore,
    repositories::IQuestionRepository &repository,
    const QString &requiredSourceProvider,
    const QStringList &pathOverride) const
{
    BankInstallResult result;
    LegacyBankImporter importer;
    result.import = importer.importJson(json, sourceKey, pathOverride);
    if (!result.import.succeeded()) {
        result.error = QStringLiteral("题库内容校验失败");
        return result;
    }
    if (!requiredSourceProvider.isEmpty()
        && result.import.package->bank.sourceProvider != requiredSourceProvider) {
        result.error = QStringLiteral("题库来源不是允许的小易公开题库");
        return result;
    }
    if (!blobStore.materialize(
            result.import.pendingMedia, &result.import.package.value(), &result.error)) {
        return result;
    }
    if (!repository.replaceBank(result.import.package.value(), &result.error)) {
        return result;
    }
    result.installed = true;
    return result;
}

} // namespace quizapp::services
