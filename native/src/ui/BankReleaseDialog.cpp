#include "ui/BankReleaseDialog.h"

#include "services/BankReleaseStateStore.h"
#include "ui/ChoiceComboBox.h"

#include <QComboBox>
#include <QDesktopServices>
#include <QFileInfo>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProgressBar>
#include <QPushButton>
#include <QSettings>
#include <QSet>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QVBoxLayout>

#include <algorithm>
#include <utility>

namespace quizapp::ui {
namespace {

constexpr int kEntryIndexRole = Qt::UserRole + 1;

QNetworkRequest releaseRequest(const QUrl &url)
{
    QNetworkRequest request(url);
    request.setRawHeader("Accept", "application/vnd.github+json, application/json");
    request.setRawHeader("User-Agent", "QuizApp-Native");
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
    return request;
}

QString bankSummary(const services::BankReleaseEntry &entry)
{
    const QString count = entry.questionCount > 0
        ? QStringLiteral("%1 题").arg(entry.questionCount)
        : QStringLiteral("题量下载后确认");
    return QStringLiteral("%1 · %2").arg(entry.path.join(QStringLiteral(" / ")), count);
}

} // namespace

BankReleaseDialog::BankReleaseDialog(
    services::SharedStorageLayout layout,
    QWidget *parent)
    : QDialog(parent)
    , layout_(std::move(layout))
    , network_(new QNetworkAccessManager(this))
{
    setObjectName(QStringLiteral("bankReleaseDialog"));
    setWindowTitle(QStringLiteral("题库更新"));
    setModal(true);
    resize(720, 620);
    setMinimumSize(350, 520);

    auto *pageLayout = new QVBoxLayout(this);
    pageLayout->setContentsMargins(18, 18, 18, 18);
    pageLayout->setSpacing(12);
    auto *title = new QLabel(QStringLiteral("Release 题库"), this);
    title->setObjectName(QStringLiteral("dialogHeading"));
    pageLayout->addWidget(title);
    statusLabel_ = new QLabel(QStringLiteral("正在连接 GitHub Release"), this);
    statusLabel_->setObjectName(QStringLiteral("bankReleaseStatus"));
    statusLabel_->setWordWrap(true);
    pageLayout->addWidget(statusLabel_);

    progress_ = new QProgressBar(this);
    progress_->setObjectName(QStringLiteral("bankReleaseProgress"));
    progress_->setRange(0, 100);
    progress_->setValue(0);
    progress_->setTextVisible(true);
    pageLayout->addWidget(progress_);

    auto *toolbar = new QHBoxLayout;
    toolbar->setSpacing(8);
    selectionLabel_ = new QLabel(QStringLiteral("等待题库清单"), this);
    selectionLabel_->setObjectName(QStringLiteral("bankReleaseSelectionSummary"));
    selectAllButton_ = new QPushButton(QStringLiteral("全选"), this);
    selectAllButton_->setObjectName(QStringLiteral("bankReleaseSelectAllButton"));
    clearButton_ = new QPushButton(QStringLiteral("清空"), this);
    clearButton_->setObjectName(QStringLiteral("bankReleaseClearButton"));
    toolbar->addWidget(selectionLabel_, 1);
    toolbar->addWidget(selectAllButton_);
    toolbar->addWidget(clearButton_);
    pageLayout->addLayout(toolbar);

    tree_ = new QTreeWidget(this);
    tree_->setObjectName(QStringLiteral("bankReleaseTree"));
    tree_->setHeaderLabels({QStringLiteral("科目与题库"), QStringLiteral("本地处理")});
    tree_->setRootIsDecorated(true);
    tree_->setAlternatingRowColors(false);
    tree_->setAnimated(true);
    tree_->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    tree_->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    pageLayout->addWidget(tree_, 1);

    auto *actions = new QHBoxLayout;
    actions->setSpacing(8);
    releaseButton_ = new QPushButton(QStringLiteral("打开 Release"), this);
    releaseButton_->setObjectName(QStringLiteral("bankReleaseOpenButton"));
    downloadButton_ = new QPushButton(QStringLiteral("下载所选题库"), this);
    downloadButton_->setObjectName(QStringLiteral("bankReleaseDownloadButton"));
    closeButton_ = new QPushButton(QStringLiteral("关闭"), this);
    closeButton_->setObjectName(QStringLiteral("bankReleaseCloseButton"));
    actions->addWidget(releaseButton_);
    actions->addStretch();
    actions->addWidget(closeButton_);
    actions->addWidget(downloadButton_);
    pageLayout->addLayout(actions);

    selectAllButton_->setEnabled(false);
    clearButton_->setEnabled(false);
    releaseButton_->setEnabled(false);
    downloadButton_->setEnabled(false);

    connect(closeButton_, &QPushButton::clicked, this, &QDialog::reject);
    connect(releaseButton_, &QPushButton::clicked, this, [this] {
        if (!catalog_.releasePageUrl.isEmpty()) {
            QDesktopServices::openUrl(QUrl(catalog_.releasePageUrl));
        }
    });
    connect(selectAllButton_, &QPushButton::clicked, this, [this] {
        updatingTree_ = true;
        for (int groupIndex = 0; groupIndex < tree_->topLevelItemCount(); ++groupIndex) {
            QTreeWidgetItem *group = tree_->topLevelItem(groupIndex);
            group->setCheckState(0, Qt::Checked);
            for (int childIndex = 0; childIndex < group->childCount(); ++childIndex) {
                group->child(childIndex)->setCheckState(0, Qt::Checked);
            }
        }
        updatingTree_ = false;
        updateSelectionSummary();
    });
    connect(clearButton_, &QPushButton::clicked, this, [this] {
        updatingTree_ = true;
        for (int groupIndex = 0; groupIndex < tree_->topLevelItemCount(); ++groupIndex) {
            QTreeWidgetItem *group = tree_->topLevelItem(groupIndex);
            group->setCheckState(0, Qt::Unchecked);
            for (int childIndex = 0; childIndex < group->childCount(); ++childIndex) {
                group->child(childIndex)->setCheckState(0, Qt::Unchecked);
            }
        }
        updatingTree_ = false;
        updateSelectionSummary();
    });
    connect(tree_, &QTreeWidget::itemChanged, this,
            [this](QTreeWidgetItem *item, int column) {
        if (updatingTree_ || column != 0) {
            return;
        }
        updatingTree_ = true;
        if (!item->parent()) {
            for (int childIndex = 0; childIndex < item->childCount(); ++childIndex) {
                item->child(childIndex)->setCheckState(0, item->checkState(0));
            }
        } else {
            updateGroupState(item->parent());
        }
        updatingTree_ = false;
        updateSelectionSummary();
    });
    connect(downloadButton_, &QPushButton::clicked,
            this, &BankReleaseDialog::startSelectedDownloads);
}

void BankReleaseDialog::startCheck(const QUrl &latestReleaseUrl, bool automatic)
{
    latestReleaseUrl_ = latestReleaseUrl;
    automatic_ = automatic;
    catalog_ = {};
    metadata_ = {};
    downloadedPayloads_.clear();
    outdatedEntryIds_.clear();
    tree_->clear();
    setBusy(true);
    setProgress(12, QStringLiteral("正在读取最新 Release"));
    request(latestReleaseUrl_, NetworkPhase::Release);
}

void BankReleaseDialog::setCatalogForTesting(
    const services::BankReleaseCatalog &catalog,
    const QHash<QString, QByteArray> &payloads,
    bool automatic)
{
    automatic_ = automatic;
    catalog_ = catalog;
    downloadedPayloads_ = payloads;
    completeCatalogCheck();
}

void BankReleaseDialog::request(const QUrl &url, NetworkPhase phase)
{
    if (!url.isValid() || url.scheme().isEmpty()) {
        setBusy(false);
        setProgress(0, QStringLiteral("下载地址无效"));
        return;
    }
    QNetworkReply *reply = network_->get(releaseRequest(url));
    connect(reply, &QNetworkReply::finished, this, [this, reply, phase] {
        handleReply(reply, phase);
    });
}

void BankReleaseDialog::handleReply(QNetworkReply *reply, NetworkPhase phase)
{
    const QByteArray bytes = reply->readAll();
    const QString networkError = reply->error() == QNetworkReply::NoError
        ? QString() : reply->errorString();
    reply->deleteLater();
    if (!networkError.isEmpty()) {
        emit checkCompleted(0, networkError);
        if (automatic_) {
            reject();
            return;
        }
        setBusy(false);
        setProgress(0, QStringLiteral("网络请求失败：%1").arg(networkError));
        return;
    }
    QString error;
    if (phase == NetworkPhase::Release) {
        if (!services::BankReleaseService::parseReleaseMetadata(
                bytes, &metadata_, &error)) {
            emit checkCompleted(0, error);
            if (automatic_) {
                reject();
                return;
            }
            setBusy(false);
            setProgress(0, error);
            return;
        }
        if (metadata_.manifestAssetIndex >= 0) {
            setProgress(42, QStringLiteral("正在读取题库清单"));
            request(QUrl(metadata_.assets.at(metadata_.manifestAssetIndex).downloadUrl),
                    NetworkPhase::Manifest);
            return;
        }
        if (!services::BankReleaseService::buildCatalog(
                metadata_, {}, &catalog_, &error)) {
            emit checkCompleted(0, error);
            if (automatic_) {
                reject();
                return;
            }
            setBusy(false);
            setProgress(100, error);
            releaseButton_->setEnabled(!metadata_.releasePageUrl.isEmpty());
            catalog_.releasePageUrl = metadata_.releasePageUrl;
            return;
        }
        completeCatalogCheck();
        return;
    }
    if (phase == NetworkPhase::Manifest) {
        if (!services::BankReleaseService::buildCatalog(
                metadata_, bytes, &catalog_, &error)) {
            emit checkCompleted(0, error);
            if (automatic_) {
                reject();
                return;
            }
            setBusy(false);
            setProgress(100, error);
            catalog_.releasePageUrl = metadata_.releasePageUrl;
            releaseButton_->setEnabled(!metadata_.releasePageUrl.isEmpty());
            return;
        }
        completeCatalogCheck();
        return;
    }
    if (phase == NetworkPhase::Bank) {
        const services::BankReleaseSelection &selection =
            pendingSelections_.at(pendingDownloadIndex_);
        downloadedPayloads_.insert(selection.entry.id, bytes);
        ++pendingDownloadIndex_;
        downloadNextBank();
    }
}

void BankReleaseDialog::completeCatalogCheck()
{
    QSettings settings;
    const services::BankReleaseState state =
        services::BankReleaseStateStore::load(settings);
    outdatedEntryIds_ = services::BankReleaseStateStore::outdatedEntryIds(
        catalog_, state);
    QString stateError;
    services::BankReleaseStateStore::recordCheck(
        settings, catalog_, QDateTime::currentDateTimeUtc(), &stateError);
    emit checkCompleted(outdatedEntryIds_.size(), stateError);
    if (automatic_ && outdatedEntryIds_.isEmpty()) {
        reject();
        return;
    }
    renderCatalog();
    setBusy(false);
    setProgress(
        100,
        outdatedEntryIds_.isEmpty()
            ? QStringLiteral("检查完成，当前题库已经是最新")
            : QStringLiteral("检查完成，发现 %1 个可更新题库")
                  .arg(outdatedEntryIds_.size()));
    if (automatic_) {
        open();
    }
}

void BankReleaseDialog::renderCatalog()
{
    updatingTree_ = true;
    tree_->clear();
    QSet<QString> outdatedIds;
    for (const QString &id : std::as_const(outdatedEntryIds_)) {
        outdatedIds.insert(id);
    }
    QHash<QString, QTreeWidgetItem *> groups;
    for (qsizetype index = 0; index < catalog_.entries.size(); ++index) {
        const services::BankReleaseEntry &entry = catalog_.entries.at(index);
        const QString subject = entry.path.isEmpty()
            ? QStringLiteral("未分类") : entry.path.constFirst();
        QTreeWidgetItem *group = groups.value(subject);
        if (!group) {
            group = new QTreeWidgetItem(tree_, {subject, QString()});
            group->setFlags(group->flags() | Qt::ItemIsUserCheckable);
            group->setCheckState(0, Qt::Unchecked);
            group->setExpanded(true);
            groups.insert(subject, group);
        }
        auto *item = new QTreeWidgetItem(group, {entry.name, bankSummary(entry)});
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(
            0, outdatedIds.contains(entry.id) ? Qt::Checked : Qt::Unchecked);
        item->setData(0, kEntryIndexRole, static_cast<qlonglong>(index));
        item->setToolTip(0, entry.path.join(QStringLiteral(" / ")));
        const QString destination = services::BankReleaseService::destinationPath(layout_, entry);
        if (!destination.isEmpty() && QFileInfo::exists(destination)) {
            auto *choice = new ChoiceComboBox(tree_);
            choice->setObjectName(QStringLiteral("bankReleaseConflictChoice"));
            choice->setMinimumHeight(42);
            choice->setMinimumWidth(156);
            choice->addItem(QStringLiteral("覆盖本地"),
                            static_cast<int>(services::BankReleaseConflictPolicy::Overwrite));
            choice->addItem(QStringLiteral("另存新版"),
                            static_cast<int>(services::BankReleaseConflictPolicy::KeepBoth));
            choice->setToolTip(QStringLiteral("本地存在同路径题库"));
            tree_->setItemWidget(item, 1, choice);
        }
    }
    for (auto iterator = groups.cbegin(); iterator != groups.cend(); ++iterator) {
        updateGroupState(iterator.value());
        iterator.value()->setText(
            1, QStringLiteral("%1 个题库").arg(iterator.value()->childCount()));
    }
    updatingTree_ = false;
    statusLabel_->setText(
        outdatedEntryIds_.isEmpty()
            ? QStringLiteral("Release %1：当前已安装记录没有变化")
                  .arg(catalog_.tagName)
            : QStringLiteral("Release %1：%2/%3 个题库有更新")
                  .arg(catalog_.tagName)
                  .arg(outdatedEntryIds_.size())
                  .arg(catalog_.entries.size()));
    selectAllButton_->setEnabled(!catalog_.entries.isEmpty());
    clearButton_->setEnabled(!catalog_.entries.isEmpty());
    releaseButton_->setEnabled(!catalog_.releasePageUrl.isEmpty());
    downloadButton_->setEnabled(!catalog_.entries.isEmpty());
    updateSelectionSummary();
}

void BankReleaseDialog::updateGroupState(QTreeWidgetItem *group)
{
    int checked = 0;
    for (int childIndex = 0; childIndex < group->childCount(); ++childIndex) {
        if (group->child(childIndex)->checkState(0) == Qt::Checked) {
            ++checked;
        }
    }
    group->setCheckState(0, checked == 0
        ? Qt::Unchecked
        : checked == group->childCount() ? Qt::Checked : Qt::PartiallyChecked);
}

void BankReleaseDialog::updateSelectionSummary()
{
    int selected = 0;
    int total = 0;
    for (int groupIndex = 0; groupIndex < tree_->topLevelItemCount(); ++groupIndex) {
        QTreeWidgetItem *group = tree_->topLevelItem(groupIndex);
        for (int childIndex = 0; childIndex < group->childCount(); ++childIndex) {
            ++total;
            if (group->child(childIndex)->checkState(0) == Qt::Checked) {
                ++selected;
            }
        }
    }
    selectionLabel_->setText(QStringLiteral("已选择 %1/%2 个题库").arg(selected).arg(total));
    downloadButton_->setEnabled(selected > 0 && progress_->value() >= 100);
}

void BankReleaseDialog::setBusy(bool busy)
{
    tree_->setEnabled(!busy);
    selectAllButton_->setEnabled(!busy && !catalog_.entries.isEmpty());
    clearButton_->setEnabled(!busy && !catalog_.entries.isEmpty());
    downloadButton_->setEnabled(!busy && !selectedEntries().isEmpty());
    closeButton_->setEnabled(!busy);
}

void BankReleaseDialog::setProgress(int value, const QString &message)
{
    progress_->setValue(std::clamp(value, 0, 100));
    statusLabel_->setText(message);
}

QVector<services::BankReleaseSelection> BankReleaseDialog::selectedEntries() const
{
    QVector<services::BankReleaseSelection> selections;
    for (int groupIndex = 0; groupIndex < tree_->topLevelItemCount(); ++groupIndex) {
        QTreeWidgetItem *group = tree_->topLevelItem(groupIndex);
        for (int childIndex = 0; childIndex < group->childCount(); ++childIndex) {
            QTreeWidgetItem *item = group->child(childIndex);
            if (item->checkState(0) != Qt::Checked) {
                continue;
            }
            const qsizetype index = item->data(0, kEntryIndexRole).toLongLong();
            if (index < 0 || index >= catalog_.entries.size()) {
                continue;
            }
            services::BankReleaseConflictPolicy policy =
                services::BankReleaseConflictPolicy::Overwrite;
            if (auto *choice = qobject_cast<QComboBox *>(tree_->itemWidget(item, 1))) {
                policy = static_cast<services::BankReleaseConflictPolicy>(
                    choice->currentData().toInt());
            }
            selections.append({catalog_.entries.at(index), policy});
        }
    }
    return selections;
}

void BankReleaseDialog::startSelectedDownloads()
{
    pendingSelections_ = selectedEntries();
    if (pendingSelections_.isEmpty()) {
        statusLabel_->setText(QStringLiteral("请至少选择一个题库"));
        return;
    }
    pendingDownloadIndex_ = 0;
    setBusy(true);
    setProgress(4, QStringLiteral("准备下载题库"));
    downloadNextBank();
}

void BankReleaseDialog::downloadNextBank()
{
    while (pendingDownloadIndex_ < pendingSelections_.size()) {
        const services::BankReleaseEntry &entry =
            pendingSelections_.at(pendingDownloadIndex_).entry;
        if (!entry.embeddedPayload.isEmpty()) {
            downloadedPayloads_.insert(entry.id, entry.embeddedPayload);
            ++pendingDownloadIndex_;
            continue;
        }
        if (downloadedPayloads_.contains(entry.id)) {
            ++pendingDownloadIndex_;
            continue;
        }
        const int progress = 10 + static_cast<int>(
            (70.0 * pendingDownloadIndex_)
            / std::max<qsizetype>(1, pendingSelections_.size()));
        setProgress(progress, QStringLiteral("正在下载 %1（%2/%3）")
            .arg(entry.name).arg(pendingDownloadIndex_ + 1).arg(pendingSelections_.size()));
        request(QUrl(entry.downloadUrl), NetworkPhase::Bank);
        return;
    }
    installDownloadedBanks();
}

void BankReleaseDialog::installDownloadedBanks()
{
    setProgress(88, QStringLiteral("正在校验并写入本地题库"));
    services::BankReleaseService service;
    const auto result = service.install(
        layout_, catalog_.tagName, pendingSelections_, downloadedPayloads_);
    if (!result.succeeded()) {
        setBusy(false);
        setProgress(0, QStringLiteral("题库更新失败：%1").arg(result.error));
        return;
    }
    QSettings settings;
    QString stateError;
    const bool stateSaved = services::BankReleaseStateStore::recordInstall(
        settings, catalog_, pendingSelections_, QDateTime::currentDateTimeUtc(),
        &stateError);
    setProgress(
        100,
        stateSaved
            ? QStringLiteral("题库更新完成：%1 个题库").arg(result.installedEntries)
            : QStringLiteral("题库已更新，但本地更新记录保存失败"));
    setBusy(false);
    emit banksInstalled(result.installedEntries);
}

} // namespace quizapp::ui
