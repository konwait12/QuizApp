#pragma once

#include "services/LocalBackupService.h"

#include <QFrame>

class QCheckBox;
class QLabel;
class QProgressBar;
class QPushButton;
class QDialog;
class QTimer;

template<typename T>
class QFutureWatcher;

namespace quizapp::ui {

struct BackupTaskResult {
    bool success = false;
    QString path;
    QString error;
    services::BackupInspection inspection;
};

class BackupSettingsPanel final : public QFrame {
    Q_OBJECT

public:
    BackupSettingsPanel(
        QString databasePath,
        QString dataRoot,
        QString sharedRoot,
        QWidget *parent = nullptr);
    ~BackupSettingsPanel() override;

    void exportTo(const QString &path);
    void inspectForRestore(const QString &path);
    void stageInspectedRestore();

signals:
    void backupCreated(const QString &path);
    void backupInspected(bool valid);
    void restoreStaged();

private:
    enum class TaskKind { None, Export, Inspect, Stage };
    void chooseExportPath();
    void chooseRestorePath();
    void showInspection(const services::BackupInspection &inspection);
    void setBusy(bool busy, const QString &status = {});
    void updateProgress(const QString &stage, qint64 completed, qint64 total);
    void pollAndroidDocumentResult();
    QString defaultBackupDirectory() const;

    QString databasePath_;
    QString dataRoot_;
    QString sharedRoot_;
    QString inspectedPath_;
    services::BackupInspection inspection_;
    QFutureWatcher<BackupTaskResult> *watcher_ = nullptr;
    QDialog *previewDialog_ = nullptr;
    TaskKind taskKind_ = TaskKind::None;
    QCheckBox *includeSecretsChoice_ = nullptr;
    QPushButton *exportButton_ = nullptr;
    QPushButton *restoreButton_ = nullptr;
    QProgressBar *progress_ = nullptr;
    QLabel *status_ = nullptr;
    QTimer *documentPollTimer_ = nullptr;
    QString androidExportTempPath_;
    QString androidImportTempPath_;
};

} // namespace quizapp::ui
