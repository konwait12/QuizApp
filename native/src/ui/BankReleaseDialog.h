#pragma once

#include "services/BankReleaseService.h"

#include <QDialog>
#include <QHash>
#include <QUrl>

class QLabel;
class QNetworkAccessManager;
class QNetworkReply;
class QProgressBar;
class QPushButton;
class QTreeWidget;
class QTreeWidgetItem;

namespace quizapp::ui {

class BankReleaseDialog final : public QDialog {
    Q_OBJECT

public:
    explicit BankReleaseDialog(
        services::SharedStorageLayout layout,
        QWidget *parent = nullptr);

    void startCheck(const QUrl &latestReleaseUrl, bool automatic = false);
    void setCatalogForTesting(
        const services::BankReleaseCatalog &catalog,
        const QHash<QString, QByteArray> &payloads = {},
        bool automatic = false);

signals:
    void banksInstalled(int count);
    void checkCompleted(int availableUpdates, const QString &error);

private:
    enum class NetworkPhase {
        Idle,
        Release,
        Manifest,
        Bank,
    };

    void request(const QUrl &url, NetworkPhase phase);
    void handleReply(QNetworkReply *reply, NetworkPhase phase);
    void completeCatalogCheck();
    void renderCatalog();
    void updateGroupState(QTreeWidgetItem *group);
    void updateSelectionSummary();
    void setBusy(bool busy);
    void setProgress(int value, const QString &message);
    void startSelectedDownloads();
    void downloadNextBank();
    void installDownloadedBanks();
    QVector<services::BankReleaseSelection> selectedEntries() const;

    services::SharedStorageLayout layout_;
    services::BankReleaseMetadata metadata_;
    services::BankReleaseCatalog catalog_;
    QHash<QString, QByteArray> downloadedPayloads_;
    QVector<services::BankReleaseSelection> pendingSelections_;
    int pendingDownloadIndex_ = 0;
    bool updatingTree_ = false;
    bool automatic_ = false;
    QStringList outdatedEntryIds_;
    QUrl latestReleaseUrl_;

    QNetworkAccessManager *network_ = nullptr;
    QLabel *statusLabel_ = nullptr;
    QLabel *selectionLabel_ = nullptr;
    QProgressBar *progress_ = nullptr;
    QTreeWidget *tree_ = nullptr;
    QPushButton *selectAllButton_ = nullptr;
    QPushButton *clearButton_ = nullptr;
    QPushButton *releaseButton_ = nullptr;
    QPushButton *downloadButton_ = nullptr;
    QPushButton *closeButton_ = nullptr;
};

} // namespace quizapp::ui
