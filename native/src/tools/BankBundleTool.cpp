#include "services/BankInstallService.h"
#include "services/BlobStore.h"
#include "storage/Database.h"
#include "storage/SqliteQuestionRepository.h"

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QSqlQuery>
#include <QTextStream>

#include <algorithm>

namespace {

bool writeManifest(
    const QString &path,
    int sectionCount,
    qsizetype questionCount,
    int blobCount,
    qint64 blobBytes,
    QString *error)
{
    const QJsonObject manifest{
        {QStringLiteral("schemaVersion"), 1},
        {QStringLiteral("displayName"), QStringLiteral("27考研题库包")},
        {QStringLiteral("provider"), QStringLiteral("xiaoyivip")},
        {QStringLiteral("sectionCount"), sectionCount},
        {QStringLiteral("questionCount"), static_cast<qint64>(questionCount)},
        {QStringLiteral("blobCount"), blobCount},
        {QStringLiteral("blobBytes"), blobBytes},
        {QStringLiteral("generatedAt"),
         QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)},
    };
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)
        || file.write(QJsonDocument(manifest).toJson(QJsonDocument::Indented)) < 0
        || !file.commit()) {
        if (error) {
            *error = QStringLiteral("Bundle manifest could not be written");
        }
        return false;
    }
    return true;
}

} // namespace

int main(int argc, char *argv[])
{
    QCoreApplication application(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("QuizAppBankBundleTool"));
    QCommandLineParser parser;
    parser.setApplicationDescription(
        QStringLiteral("Builds a validated native QuizApp bank bundle"));
    parser.addHelpOption();
    parser.addOptions({
        {{QStringLiteral("i"), QStringLiteral("input")},
         QStringLiteral("Directory containing exported 27 postgraduate exam bank JSON files"),
         QStringLiteral("directory")},
        {{QStringLiteral("o"), QStringLiteral("output")},
         QStringLiteral("New directory for the native bundle"),
         QStringLiteral("directory")},
    });
    parser.process(application);

    QTextStream standardError(stderr);
    QTextStream standardOutput(stdout);
    const QString inputPath = QDir::cleanPath(parser.value(QStringLiteral("input")));
    const QString outputPath = QDir::cleanPath(parser.value(QStringLiteral("output")));
    if (inputPath.isEmpty() || outputPath.isEmpty()
        || !QFileInfo(inputPath).isDir() || QFileInfo::exists(outputPath)) {
        standardError << "Input must be a directory and output must not already exist.\n";
        return 2;
    }
    if (!QDir().mkpath(outputPath)) {
        standardError << "Output directory could not be created.\n";
        return 3;
    }

    quizapp::storage::Database database(QStringLiteral("bundle-tool"));
    QString error;
    if (!database.open(QDir(outputPath).filePath(QStringLiteral("quizapp.sqlite")), &error)
        || !database.migrate(&error)) {
        standardError << error << u'\n';
        return 4;
    }
    quizapp::storage::SqliteQuestionRepository repository(database.connection());
    quizapp::services::BlobStore blobStore(outputPath);
    quizapp::services::BankInstallService installer;

    QStringList files;
    QDirIterator iterator(
        inputPath,
        {QStringLiteral("*.json")},
        QDir::Files,
        QDirIterator::Subdirectories);
    while (iterator.hasNext()) {
        const QString path = iterator.next();
        if (QFileInfo(path).fileName() != QStringLiteral("export-report.json")) {
            files.append(path);
        }
    }
    std::sort(files.begin(), files.end());

    qsizetype questionCount = 0;
    int installedSections = 0;
    for (const QString &filePath : files) {
        QFile file(filePath);
        if (!file.open(QIODevice::ReadOnly)) {
            standardError << "Cannot read " << QDir(inputPath).relativeFilePath(filePath) << u'\n';
            return 5;
        }
        const QString sourceKey = QDir(inputPath).relativeFilePath(filePath).replace(u'\\', u'/');
        const auto result = installer.installJson(file.readAll(), sourceKey, blobStore, repository);
        if (!result.installed) {
            standardError << sourceKey << ": " << result.error << u'\n';
            for (const quizapp::domain::ImportDiagnostic &diagnostic : result.import.diagnostics) {
                standardError << "  [" << diagnostic.code << "] question="
                              << diagnostic.questionIndex << " source="
                              << diagnostic.sourceQuestionId << " message="
                              << diagnostic.message << u'\n';
            }
            return 6;
        }
        questionCount += result.import.acceptedQuestionCount;
        ++installedSections;
        standardOutput << installedSections << '/' << files.size() << ' ' << sourceKey << u'\n';
        standardOutput.flush();
    }

    QSqlQuery blobSummary(database.connection());
    if (!blobSummary.exec(QStringLiteral("SELECT COUNT(*), COALESCE(SUM(byte_size), 0) FROM blobs"))
        || !blobSummary.next()) {
        standardError << "Blob summary query failed.\n";
        return 7;
    }
    const int blobCount = blobSummary.value(0).toInt();
    const qint64 blobBytes = blobSummary.value(1).toLongLong();
    if (!writeManifest(
            QDir(outputPath).filePath(QStringLiteral("manifest.json")),
            installedSections,
            questionCount,
            blobCount,
            blobBytes,
            &error)) {
        standardError << error << u'\n';
        return 8;
    }

    standardOutput << "Bundle complete: " << installedSections << " sections, "
                   << questionCount << " questions, " << blobCount << " blobs.\n";
    return 0;
}
