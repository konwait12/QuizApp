#include "ui/AnnouncementDialog.h"

#include <QCheckBox>
#include <QDesktopServices>
#include <QHeaderView>
#include <QLabel>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProgressBar>
#include <QPushButton>
#include <QSettings>
#include <QSet>
#include <QTextBrowser>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QVBoxLayout>

#include <algorithm>

namespace quizapp::ui {
namespace {

QNetworkRequest announcementRequest(const QUrl &url)
{
    QNetworkRequest request(url);
    request.setRawHeader("Accept", "application/vnd.github+json, application/json");
    request.setRawHeader("User-Agent", "QuizApp-Native");
    request.setAttribute(
        QNetworkRequest::RedirectPolicyAttribute,
        QNetworkRequest::NoLessSafeRedirectPolicy);
    return request;
}

} // namespace

AnnouncementDialog::AnnouncementDialog(QWidget *parent)
    : QDialog(parent)
    , network_(new QNetworkAccessManager(this))
{
    setObjectName(QStringLiteral("announcementDialog"));
    setWindowTitle(QStringLiteral("QuizApp 公告栏"));
    setModal(true);
    setMinimumSize(350, 500);
    resize(620, 640);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(18, 18, 18, 18);
    layout->setSpacing(12);
    auto *title = new QLabel(QStringLiteral("QuizApp 公告栏"), this);
    title->setObjectName(QStringLiteral("dialogHeading"));
    layout->addWidget(title);
    statusLabel_ = new QLabel(QStringLiteral("公告保存在当前设备"), this);
    statusLabel_->setObjectName(QStringLiteral("announcementStatus"));
    statusLabel_->setWordWrap(true);
    layout->addWidget(statusLabel_);
    progress_ = new QProgressBar(this);
    progress_->setObjectName(QStringLiteral("announcementProgress"));
    progress_->setRange(0, 100);
    progress_->setValue(0);
    progress_->setTextVisible(false);
    progress_->hide();
    layout->addWidget(progress_);

    tree_ = new QTreeWidget(this);
    tree_->setObjectName(QStringLiteral("announcementTree"));
    tree_->setHeaderHidden(true);
    tree_->setRootIsDecorated(true);
    tree_->setAnimated(true);
    tree_->setIndentation(20);
    tree_->setSelectionMode(QAbstractItemView::NoSelection);
    tree_->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    layout->addWidget(tree_, 1);

    suppressChoice_ = new QCheckBox(
        QStringLiteral("当前这些公告不再自动提醒"), this);
    suppressChoice_->setObjectName(QStringLiteral("announcementSuppressChoice"));
    suppressChoice_->hide();
    layout->addWidget(suppressChoice_);
    closeButton_ = new QPushButton(QStringLiteral("关闭"), this);
    closeButton_->setObjectName(QStringLiteral("announcementCloseButton"));
    closeButton_->setMinimumHeight(42);
    connect(closeButton_, &QPushButton::clicked, this, &QDialog::accept);
    layout->addWidget(closeButton_, 0, Qt::AlignRight);
}

void AnnouncementDialog::showCached(
    const services::AnnouncementCatalog &catalog)
{
    automatic_ = false;
    QStringList ids;
    for (const services::AnnouncementItem &item : catalog.items) {
        ids.append(item.id);
    }
    renderCatalog(catalog, ids, false);
}

void AnnouncementDialog::startCheck(
    const QUrl &feedUrl,
    const QUrl &latestReleaseUrl,
    bool automatic)
{
    automatic_ = automatic;
    latestReleaseUrl_ = latestReleaseUrl;
    catalogShown_ = false;
    catalog_ = {};
    tree_->clear();
    setBusy(true, QStringLiteral("正在检查新公告"));
    request(feedUrl, NetworkPhase::Feed);
}

void AnnouncementDialog::setCatalogForTesting(
    const services::AnnouncementCatalog &catalog,
    bool automatic)
{
    automatic_ = automatic;
    applyCatalog(catalog);
}

void AnnouncementDialog::request(const QUrl &url, NetworkPhase phase)
{
    if (!url.isValid() || url.scheme().isEmpty()) {
        finishError(QStringLiteral("公告下载地址无效"));
        return;
    }
    QNetworkReply *reply = network_->get(announcementRequest(url));
    connect(reply, &QNetworkReply::finished, this, [this, reply, phase] {
        handleReply(reply, phase);
    });
}

void AnnouncementDialog::handleReply(
    QNetworkReply *reply,
    NetworkPhase phase)
{
    const QByteArray payload = reply->readAll();
    const QString networkError = reply->error() == QNetworkReply::NoError
        ? QString() : reply->errorString();
    reply->deleteLater();
    QString error;
    if (phase == NetworkPhase::Feed) {
        services::AnnouncementCatalog catalog;
        if (networkError.isEmpty()
            && services::AnnouncementService::parseCatalog(payload, &catalog, &error)) {
            applyCatalog(catalog);
            return;
        }
        statusLabel_->setText(QStringLiteral("正在从最新 Release 读取公告"));
        progress_->setValue(45);
        request(latestReleaseUrl_, NetworkPhase::Release);
        return;
    }
    if (!networkError.isEmpty()) {
        finishError(networkError);
        return;
    }
    if (phase == NetworkPhase::Release) {
        QString assetUrl;
        if (!services::AnnouncementService::announcementAssetUrl(
                payload, &assetUrl, &error)) {
            finishError(error);
            return;
        }
        progress_->setValue(70);
        request(QUrl(assetUrl), NetworkPhase::Asset);
        return;
    }
    services::AnnouncementCatalog catalog;
    if (!services::AnnouncementService::parseCatalog(payload, &catalog, &error)) {
        finishError(error);
        return;
    }
    applyCatalog(catalog);
}

void AnnouncementDialog::applyCatalog(
    const services::AnnouncementCatalog &catalog)
{
    QSettings settings;
    QString stateError;
    if (!services::AnnouncementStateStore::saveCatalog(
            settings, catalog, QDateTime::currentDateTimeUtc(), &stateError)) {
        finishError(stateError);
        return;
    }
    const services::AnnouncementState state =
        services::AnnouncementStateStore::load(settings);
    const QStringList unread = services::AnnouncementStateStore::unreadIds(state);
    emit checkCompleted(unread.size(), {});
    emit unreadStateChanged(!unread.isEmpty());
    if (automatic_ && (unread.isEmpty()
        || state.suppressedCatalogFingerprint == catalog.fingerprint)) {
        reject();
        return;
    }
    QStringList visibleIds;
    if (automatic_) {
        visibleIds = unread;
    } else {
        for (const services::AnnouncementItem &item : catalog.items) {
            visibleIds.append(item.id);
        }
    }
    renderCatalog(catalog, visibleIds, automatic_);
    if (automatic_) {
        open();
    }
}

void AnnouncementDialog::renderCatalog(
    const services::AnnouncementCatalog &catalog,
    const QStringList &visibleIds,
    bool startup)
{
    catalog_ = catalog;
    catalogShown_ = true;
    tree_->clear();
    const QSet<QString> visible(visibleIds.cbegin(), visibleIds.cend());
    bool expanded = false;
    for (const services::AnnouncementItem &item : catalog.items) {
        if (!visible.contains(item.id)) {
            continue;
        }
        const QString suffix = item.date.isEmpty()
            ? QString() : QStringLiteral("  ·  %1").arg(item.date);
        auto *header = new QTreeWidgetItem(
            tree_, {item.title + suffix + (item.latest ? QStringLiteral("  ·  最新") : QString())});
        header->setFlags(header->flags() & ~Qt::ItemIsSelectable);
        auto *bodyItem = new QTreeWidgetItem(header);
        bodyItem->setFlags(bodyItem->flags() & ~Qt::ItemIsSelectable);
        bodyItem->setFirstColumnSpanned(true);
        auto *body = new QTextBrowser(tree_);
        body->setObjectName(QStringLiteral("announcementBody"));
        body->setOpenExternalLinks(true);
        body->setHtml(item.bodyHtml);
        body->setMinimumHeight(150);
        body->setMaximumHeight(240);
        bodyItem->setSizeHint(0, QSize(0, 180));
        tree_->setItemWidget(bodyItem, 0, body);
        if (!expanded && (item.latest || startup)) {
            header->setExpanded(true);
            expanded = true;
        }
    }
    suppressChoice_->setVisible(startup);
    suppressChoice_->setChecked(false);
    setBusy(false, startup
        ? QStringLiteral("发现 %1 篇新公告").arg(visibleIds.size())
        : QStringLiteral("共 %1 篇公告").arg(visibleIds.size()));
}

void AnnouncementDialog::finishError(const QString &error)
{
    emit checkCompleted(0, error);
    if (automatic_) {
        reject();
        return;
    }
    setBusy(false, QStringLiteral("公告检查失败：%1").arg(error));
}

void AnnouncementDialog::setBusy(bool busy, const QString &message)
{
    progress_->setVisible(busy);
    progress_->setValue(busy ? std::max(12, progress_->value()) : 100);
    tree_->setEnabled(!busy);
    closeButton_->setEnabled(!busy);
    if (!message.isEmpty()) {
        statusLabel_->setText(message);
    }
}

void AnnouncementDialog::done(int result)
{
    if (catalogShown_) {
        QSettings settings;
        services::AnnouncementStateStore::markAllRead(
            settings, catalog_, suppressChoice_->isChecked());
        const services::AnnouncementState state =
            services::AnnouncementStateStore::load(settings);
        emit unreadStateChanged(
            !services::AnnouncementStateStore::unreadIds(state).isEmpty());
        catalogShown_ = false;
    }
    QDialog::done(result);
}

} // namespace quizapp::ui
