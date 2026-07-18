#include "ui/AppUpdateDialog.h"

#include "platform/AppUpdatePlatform.h"

#include <QCheckBox>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProgressBar>
#include <QPushButton>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSettings>
#include <QStandardPaths>
#include <QTextBrowser>
#include <QVBoxLayout>

#include <algorithm>
#include <utility>

namespace quizapp::ui {
namespace {

QNetworkRequest updateRequest(const QUrl &url)
{
    QNetworkRequest request(url);
    request.setRawHeader("Accept", "application/vnd.github+json, application/octet-stream");
    request.setRawHeader("User-Agent", "QuizApp-Native-Updater");
    request.setAttribute(
        QNetworkRequest::RedirectPolicyAttribute,
        QNetworkRequest::NoLessSafeRedirectPolicy);
    return request;
}

QString compactNotes(QString value)
{
    value.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    value.replace(u'\r', u'\n');
    value = value.trimmed();
    if (value.size() > 1000) {
        value = value.left(1000) + QStringLiteral("\n…");
    }
    return value;
}

} // namespace

AppUpdateDialog::AppUpdateDialog(
    QString currentVersion,
    QString currentBuildCommit,
    QWidget *parent)
    : QDialog(parent)
    , currentVersion_(std::move(currentVersion))
    , currentBuildCommit_(std::move(currentBuildCommit))
    , network_(new QNetworkAccessManager(this))
{
    setObjectName(QStringLiteral("appUpdateDialog"));
    setWindowTitle(QStringLiteral("应用更新"));
    setModal(true);
    resize(560, 520);
    setMinimumSize(350, 460);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(18, 18, 18, 18);
    layout->setSpacing(12);
    auto *title = new QLabel(QStringLiteral("版本更新"), this);
    title->setObjectName(QStringLiteral("dialogHeading"));
    layout->addWidget(title);

    statusLabel_ = new QLabel(QStringLiteral("正在连接 GitHub Release"), this);
    statusLabel_->setObjectName(QStringLiteral("appUpdateStatus"));
    statusLabel_->setWordWrap(true);
    layout->addWidget(statusLabel_);

    versionLabel_ = new QLabel(this);
    versionLabel_->setObjectName(QStringLiteral("appUpdateVersions"));
    versionLabel_->setWordWrap(true);
    versionLabel_->setText(QStringLiteral("当前版本：%1").arg(currentVersion_));
    layout->addWidget(versionLabel_);

    notesLabel_ = new QTextBrowser(this);
    notesLabel_->setObjectName(QStringLiteral("appUpdateNotes"));
    notesLabel_->setReadOnly(true);
    notesLabel_->setOpenExternalLinks(false);
    notesLabel_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    notesLabel_->hide();
    layout->addWidget(notesLabel_, 1);

    progress_ = new QProgressBar(this);
    progress_->setObjectName(QStringLiteral("appUpdateProgress"));
    progress_->setRange(0, 100);
    progress_->setValue(0);
    progress_->setTextVisible(true);
    progress_->hide();
    layout->addWidget(progress_);

    dismissChoice_ = new QCheckBox(
        QStringLiteral("这个 Release 不再自动提醒"), this);
    dismissChoice_->setObjectName(QStringLiteral("appUpdateDismissChoice"));
    dismissChoice_->hide();
    layout->addWidget(dismissChoice_);

    auto *actions = new QHBoxLayout;
    actions->setSpacing(8);
    releaseButton_ = new QPushButton(QStringLiteral("打开 Release"), this);
    releaseButton_->setObjectName(QStringLiteral("appUpdateReleaseButton"));
    closeButton_ = new QPushButton(QStringLiteral("稍后"), this);
    closeButton_->setObjectName(QStringLiteral("appUpdateCloseButton"));
    downloadButton_ = new QPushButton(QStringLiteral("下载并安装"), this);
    downloadButton_->setObjectName(QStringLiteral("appUpdateDownloadButton"));
    actions->addWidget(releaseButton_);
    actions->addStretch();
    actions->addWidget(closeButton_);
    actions->addWidget(downloadButton_);
    layout->addLayout(actions);

    releaseButton_->setEnabled(false);
    downloadButton_->setEnabled(false);
    connect(closeButton_, &QPushButton::clicked, this, &AppUpdateDialog::closeWithPreference);
    connect(releaseButton_, &QPushButton::clicked, this, [this] {
        if (!release_.releasePageUrl.isEmpty()) {
            QDesktopServices::openUrl(QUrl(release_.releasePageUrl));
        }
    });
    connect(downloadButton_, &QPushButton::clicked, this, &AppUpdateDialog::startDownload);
}

AppUpdateDialog::~AppUpdateDialog()
{
    if (reply_) {
        reply_->abort();
    }
}

services::AppUpdatePlatform AppUpdateDialog::platform() const
{
#ifdef Q_OS_ANDROID
    return services::AppUpdatePlatform::Android;
#else
    return services::AppUpdatePlatform::Windows;
#endif
}

void AppUpdateDialog::startCheck(const QUrl &latestReleaseApiUrl, bool automatic)
{
    automatic_ = automatic;
    setBusy(true);
    statusLabel_->setText(QStringLiteral("正在检查最新版本"));
    requestRelease(latestReleaseApiUrl);
}

void AppUpdateDialog::requestRelease(const QUrl &url)
{
    phase_ = NetworkPhase::Release;
    reply_ = network_->get(updateRequest(url));
    connect(reply_, &QNetworkReply::finished, this, &AppUpdateDialog::handleNetworkFinished);
}

void AppUpdateDialog::handleNetworkFinished()
{
    QNetworkReply *finished = reply_;
    reply_ = nullptr;
    if (!finished) {
        return;
    }
    const NetworkPhase completedPhase = phase_;
    phase_ = NetworkPhase::None;
    const QNetworkReply::NetworkError networkError = finished->error();
    const QString networkMessage = finished->errorString();
    const QByteArray payload = finished->readAll();
    if (completedPhase == NetworkPhase::Package && !payload.isEmpty() && downloadFile_) {
        if (downloadFile_->write(payload) != payload.size()) {
            finished->deleteLater();
            downloadFile_->cancelWriting();
            downloadFile_.reset();
            failOperation(QStringLiteral("写入更新文件失败"));
            return;
        }
        downloadHash_.addData(payload);
        downloadedBytes_ += payload.size();
    }
    finished->deleteLater();
    if (networkError != QNetworkReply::NoError) {
        if (downloadFile_) {
            downloadFile_->cancelWriting();
            downloadFile_.reset();
        }
        failOperation(QStringLiteral("网络请求失败：%1").arg(networkMessage));
        return;
    }
    if (completedPhase == NetworkPhase::Release) {
        handleReleaseReply(payload);
    } else if (completedPhase == NetworkPhase::Package) {
        finishDownload();
    }
}

void AppUpdateDialog::handleReleaseReply(const QByteArray &payload)
{
    QString error;
    if (!services::AppUpdateService::parseLatestRelease(payload, &release_, &error)) {
        failOperation(error);
        return;
    }
    decision_ = services::AppUpdateService::evaluate(
        release_, currentVersion_, currentBuildCommit_, platform());
    QSettings settings;
    settings.setValue(
        QStringLiteral("updates/appLastCheckedAt"), QDateTime::currentDateTimeUtc());
    settings.setValue(QStringLiteral("updates/appLastCheckedTag"), release_.tagName);
    settings.setValue(QStringLiteral("updates/appLastFingerprint"), decision_.fingerprint);
    settings.sync();
    emit checkCompleted(decision_.updateAvailable, {});

    if (!decision_.updateAvailable) {
        if (automatic_) {
            reject();
            return;
        }
        setBusy(false);
        releaseButton_->setEnabled(!release_.releasePageUrl.isEmpty());
        statusLabel_->setText(QStringLiteral("当前已经是最新版本"));
        versionLabel_->setText(QStringLiteral("当前版本：%1 · 最新版本：%2")
                                   .arg(currentVersion_, release_.tagName));
        closeButton_->setText(QStringLiteral("关闭"));
        return;
    }
    const QString dismissed = settings.value(
        QStringLiteral("updates/appDismissedFingerprint")).toString();
    if (automatic_ && dismissed == decision_.fingerprint) {
        reject();
        return;
    }
    renderRelease();
    setBusy(false);
    if (automatic_) {
        open();
    }
}

void AppUpdateDialog::setReleaseForTesting(const services::AppReleaseInfo &release)
{
    release_ = release;
    decision_ = services::AppUpdateService::evaluate(
        release_, currentVersion_, currentBuildCommit_, platform());
    if (decision_.updateAvailable) {
        renderRelease();
    } else {
        statusLabel_->setText(QStringLiteral("当前已经是最新版本"));
        versionLabel_->setText(QStringLiteral("当前版本：%1 · 最新版本：%2")
                                   .arg(currentVersion_, release_.tagName));
        notesLabel_->hide();
        dismissChoice_->hide();
        releaseButton_->setEnabled(!release_.releasePageUrl.isEmpty());
        downloadButton_->setEnabled(false);
        closeButton_->setText(QStringLiteral("关闭"));
    }
    setBusy(false);
}

void AppUpdateDialog::renderRelease()
{
    statusLabel_->setText(decision_.asset
        ? QStringLiteral("发现可安装的新版本")
        : QStringLiteral("发现新版本，但当前平台没有安装包"));
    versionLabel_->setText(QStringLiteral("当前版本：%1 · 最新版本：%2")
                               .arg(currentVersion_, release_.tagName));
    const QString notes = compactNotes(release_.body);
    notesLabel_->setPlainText(
        notes.isEmpty() ? QStringLiteral("此 Release 没有更新说明") : notes);
    notesLabel_->show();
    dismissChoice_->show();
    releaseButton_->setEnabled(!release_.releasePageUrl.isEmpty());
    downloadButton_->setEnabled(decision_.asset.has_value());
}

QString AppUpdateDialog::downloadTargetPath() const
{
    if (!decision_.asset) {
        return {};
    }
    QString fileName = QFileInfo(decision_.asset->name).fileName();
    fileName.replace(QRegularExpression(QStringLiteral("[^A-Za-z0-9._-]")), QStringLiteral("_"));
    if (fileName.isEmpty()) {
#ifdef Q_OS_ANDROID
        fileName = QStringLiteral("QuizApp-update.apk");
#else
        fileName = QStringLiteral("QuizApp-update.exe");
#endif
    }
    const QString directory = QDir(QStandardPaths::writableLocation(
        QStandardPaths::CacheLocation)).filePath(QStringLiteral("updates"));
    QDir().mkpath(directory);
    return QDir(directory).filePath(fileName);
}

void AppUpdateDialog::startDownload()
{
    if (!decision_.asset || reply_) {
        return;
    }
    const QString target = downloadTargetPath();
    downloadFile_ = std::make_unique<QSaveFile>(target);
    if (!downloadFile_->open(QIODevice::WriteOnly)) {
        failOperation(QStringLiteral("无法创建更新下载文件"));
        return;
    }
    downloadHash_.reset();
    downloadedBytes_ = 0;
    progress_->setValue(0);
    progress_->show();
    statusLabel_->setText(QStringLiteral("正在下载 %1").arg(decision_.asset->name));
    setBusy(true);
    closeButton_->setEnabled(true);
    phase_ = NetworkPhase::Package;
    reply_ = network_->get(updateRequest(QUrl(decision_.asset->downloadUrl)));
    connect(reply_, &QNetworkReply::readyRead, this, &AppUpdateDialog::handleDownloadReadyRead);
    connect(reply_, &QNetworkReply::downloadProgress, this, [this](qint64 received, qint64 total) {
        if (total > 0) {
            progress_->setRange(0, 100);
            progress_->setValue(static_cast<int>(std::clamp<qint64>(
                received * 100 / total, 0, 100)));
        } else {
            progress_->setRange(0, 0);
        }
    });
    connect(reply_, &QNetworkReply::finished, this, &AppUpdateDialog::handleNetworkFinished);
}

void AppUpdateDialog::handleDownloadReadyRead()
{
    if (!reply_ || !downloadFile_) {
        return;
    }
    const QByteArray chunk = reply_->readAll();
    if (chunk.isEmpty()) {
        return;
    }
    if (downloadFile_->write(chunk) != chunk.size()) {
        reply_->abort();
        failOperation(QStringLiteral("写入更新文件失败"));
        return;
    }
    downloadHash_.addData(chunk);
    downloadedBytes_ += chunk.size();
}

void AppUpdateDialog::finishDownload()
{
    if (!decision_.asset || !downloadFile_) {
        failOperation(QStringLiteral("更新下载状态无效"));
        return;
    }
    QString error;
    const QByteArray digest = downloadHash_.result();
    if (!services::AppUpdateService::verifyDownloadedPackage(
            *decision_.asset, downloadedBytes_, digest, &error)) {
        downloadFile_->cancelWriting();
        downloadFile_.reset();
        failOperation(error);
        return;
    }
    const QString path = downloadFile_->fileName();
    if (!downloadFile_->commit()) {
        downloadFile_.reset();
        failOperation(QStringLiteral("无法保存完整更新文件"));
        return;
    }
    downloadFile_.reset();
    progress_->setRange(0, 100);
    progress_->setValue(100);
    emit packageDownloaded(path);
    if (!platform::AppUpdatePlatform::installDownloadedPackage(path, &error)) {
        setBusy(false);
        statusLabel_->setText(QStringLiteral("下载完成，但安装器启动失败：%1").arg(error));
        releaseButton_->setEnabled(!release_.releasePageUrl.isEmpty());
        return;
    }
    setBusy(false);
    statusLabel_->setText(QStringLiteral("下载完成，已交给系统安装器"));
    downloadButton_->setEnabled(false);
}

void AppUpdateDialog::failOperation(const QString &message)
{
    setBusy(false);
    statusLabel_->setText(message);
    releaseButton_->setEnabled(!release_.releasePageUrl.isEmpty());
    emit checkCompleted(false, message);
    if (automatic_ && !isVisible()) {
        reject();
    }
}

void AppUpdateDialog::setBusy(bool busy)
{
    releaseButton_->setEnabled(!busy && !release_.releasePageUrl.isEmpty());
    downloadButton_->setEnabled(
        !busy && decision_.updateAvailable && decision_.asset.has_value());
    closeButton_->setEnabled(true);
}

void AppUpdateDialog::closeWithPreference()
{
    if (reply_ && phase_ == NetworkPhase::Package) {
        reply_->abort();
    }
    if (dismissChoice_->isChecked() && !decision_.fingerprint.isEmpty()) {
        QSettings settings;
        settings.setValue(
            QStringLiteral("updates/appDismissedFingerprint"), decision_.fingerprint);
        settings.sync();
    }
    reject();
}

} // namespace quizapp::ui
