#pragma once

#include "services/AnnouncementService.h"

#include <QDialog>
#include <QUrl>

class QCheckBox;
class QLabel;
class QNetworkAccessManager;
class QNetworkReply;
class QProgressBar;
class QPushButton;
class QTreeWidget;

namespace quizapp::ui {

class AnnouncementDialog final : public QDialog {
    Q_OBJECT

public:
    explicit AnnouncementDialog(QWidget *parent = nullptr);

    void showCached(const services::AnnouncementCatalog &catalog);
    void startCheck(
        const QUrl &feedUrl,
        const QUrl &latestReleaseUrl,
        bool automatic = false);
    void setCatalogForTesting(
        const services::AnnouncementCatalog &catalog,
        bool automatic = false);

signals:
    void unreadStateChanged(bool hasUnread);
    void checkCompleted(int unreadCount, const QString &error);

protected:
    void done(int result) override;

private:
    enum class NetworkPhase {
        Feed,
        Release,
        Asset,
    };

    void request(const QUrl &url, NetworkPhase phase);
    void handleReply(QNetworkReply *reply, NetworkPhase phase);
    void applyCatalog(const services::AnnouncementCatalog &catalog);
    void renderCatalog(
        const services::AnnouncementCatalog &catalog,
        const QStringList &visibleIds,
        bool startup);
    void finishError(const QString &error);
    void setBusy(bool busy, const QString &message = {});

    QUrl latestReleaseUrl_;
    bool automatic_ = false;
    bool catalogShown_ = false;
    services::AnnouncementCatalog catalog_;

    QNetworkAccessManager *network_ = nullptr;
    QLabel *statusLabel_ = nullptr;
    QProgressBar *progress_ = nullptr;
    QTreeWidget *tree_ = nullptr;
    QCheckBox *suppressChoice_ = nullptr;
    QPushButton *closeButton_ = nullptr;
};

} // namespace quizapp::ui
