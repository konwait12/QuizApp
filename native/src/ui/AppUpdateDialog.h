#pragma once

#include "services/AppUpdateService.h"

#include <QCryptographicHash>
#include <QDialog>
#include <QUrl>

#include <memory>

class QCheckBox;
class QLabel;
class QNetworkAccessManager;
class QNetworkReply;
class QProgressBar;
class QPushButton;
class QSaveFile;
class QTextBrowser;

namespace quizapp::ui {

class AppUpdateDialog final : public QDialog
{
    Q_OBJECT

public:
    explicit AppUpdateDialog(
        QString currentVersion,
        QString currentBuildCommit,
        QWidget *parent = nullptr);
    ~AppUpdateDialog() override;

    void startCheck(const QUrl &latestReleaseApiUrl, bool automatic);
    void setReleaseForTesting(const services::AppReleaseInfo &release);

signals:
    void checkCompleted(bool updateAvailable, const QString &error);
    void packageDownloaded(const QString &path);

private:
    enum class NetworkPhase {
        None,
        Release,
        Package,
    };

    services::AppUpdatePlatform platform() const;
    void requestRelease(const QUrl &url);
    void handleReleaseReply(const QByteArray &payload);
    void renderRelease();
    void startDownload();
    void handleDownloadReadyRead();
    void handleNetworkFinished();
    void finishDownload();
    void failOperation(const QString &message);
    void setBusy(bool busy);
    void closeWithPreference();
    QString downloadTargetPath() const;

    QString currentVersion_;
    QString currentBuildCommit_;
    services::AppReleaseInfo release_;
    services::AppUpdateDecision decision_;
    QNetworkAccessManager *network_ = nullptr;
    QNetworkReply *reply_ = nullptr;
    std::unique_ptr<QSaveFile> downloadFile_;
    QCryptographicHash downloadHash_{QCryptographicHash::Sha256};
    qint64 downloadedBytes_ = 0;
    NetworkPhase phase_ = NetworkPhase::None;
    bool automatic_ = false;

    QLabel *statusLabel_ = nullptr;
    QLabel *versionLabel_ = nullptr;
    QTextBrowser *notesLabel_ = nullptr;
    QProgressBar *progress_ = nullptr;
    QCheckBox *dismissChoice_ = nullptr;
    QPushButton *releaseButton_ = nullptr;
    QPushButton *downloadButton_ = nullptr;
    QPushButton *closeButton_ = nullptr;
};

} // namespace quizapp::ui
