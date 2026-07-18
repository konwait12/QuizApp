#include "ui/NotebookLibraryPage.h"

#include "storage/Database.h"

#include <QGridLayout>
#include <QFont>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QResizeEvent>
#include <QToolButton>
#include <QVBoxLayout>
#include <QStyle>

#include <algorithm>
#include <utility>

namespace quizapp::ui {

class NotebookDatabaseHolder final {
public:
    explicit NotebookDatabaseHolder(const QString &connectionName)
        : database(connectionName)
    {
    }

    storage::Database database;
};

namespace {

void clearLayout(QGridLayout *layout)
{
    while (layout && layout->count() > 0) delete layout->takeAt(0);
}

} // namespace

NotebookLibraryPage::NotebookLibraryPage(
    QString databasePath, QString dataRoot, QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("notebookLibraryPage"));
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(24, 20, 24, 24);
    layout->setSpacing(12);

    auto *top = new QHBoxLayout;
    auto *back = new QToolButton(this);
    back->setObjectName(QStringLiteral("notebookLibraryBackButton"));
    back->setText(QStringLiteral("←"));
    back->setToolTip(QStringLiteral("返回学习"));
    back->setAccessibleName(QStringLiteral("返回学习"));
    back->setFixedSize(42, 42);
    connect(back, &QToolButton::clicked, this, &NotebookLibraryPage::backRequested);
    auto *heading = new QLabel(QStringLiteral("自由笔记"), this);
    heading->setObjectName(QStringLiteral("pageHeading"));
    recycleViewButton_ = new QToolButton(this);
    recycleViewButton_->setObjectName(QStringLiteral("notebookRecycleViewButton"));
    recycleViewButton_->setText(QStringLiteral("回收站"));
    recycleViewButton_->setCheckable(true);
    recycleViewButton_->setMinimumSize(84, 40);
    connect(recycleViewButton_, &QToolButton::toggled,
            this, &NotebookLibraryPage::setRecycleView);
    top->addWidget(back);
    top->addWidget(heading, 1);
    top->addWidget(recycleViewButton_);
    layout->addLayout(top);

    auto *description = new QLabel(
        QStringLiteral("自由笔记使用与题目笔记相同的 SpeedyNote 画布，保存在当前设备。"), this);
    description->setObjectName(QStringLiteral("pageSupportingText"));
    description->setWordWrap(true);
    layout->addWidget(description);

    auto *createRow = new QHBoxLayout;
    createRow->setSpacing(8);
    titleInput_ = new QLineEdit(this);
    titleInput_->setObjectName(QStringLiteral("notebookTitleInput"));
    titleInput_->setPlaceholderText(QStringLiteral("输入笔记标题"));
    titleInput_->setMaxLength(80);
    titleInput_->setMinimumHeight(42);
    createButton_ = new QPushButton(QStringLiteral("+"), this);
    createButton_->setObjectName(QStringLiteral("notebookCreateButton"));
    createButton_->setToolTip(QStringLiteral("创建自由笔记"));
    createButton_->setAccessibleName(QStringLiteral("创建自由笔记"));
    createButton_->setFixedSize(44, 42);
    QFont createFont = createButton_->font();
    createFont.setPointSize(17);
    createButton_->setFont(createFont);
    connect(createButton_, &QPushButton::clicked, this, &NotebookLibraryPage::createNotebook);
    connect(titleInput_, &QLineEdit::returnPressed,
            this, &NotebookLibraryPage::createNotebook);
    createRow->addWidget(titleInput_, 1);
    createRow->addWidget(createButton_);
    layout->addLayout(createRow);

    list_ = new QListWidget(this);
    list_->setObjectName(QStringLiteral("notebookList"));
    list_->setSelectionMode(QAbstractItemView::SingleSelection);
    list_->setMinimumHeight(220);
    connect(list_, &QListWidget::itemSelectionChanged,
            this, &NotebookLibraryPage::updateActions);
    connect(list_, &QListWidget::itemDoubleClicked,
            this, [this] { openSelected(); });
    layout->addWidget(list_, 1);
    emptyLabel_ = new QLabel(this);
    emptyLabel_->setObjectName(QStringLiteral("notebookEmptyLabel"));
    emptyLabel_->setAlignment(Qt::AlignCenter);
    emptyLabel_->setMinimumHeight(42);
    layout->addWidget(emptyLabel_);

    actionsLayout_ = new QGridLayout;
    actionsLayout_->setHorizontalSpacing(8);
    actionsLayout_->setVerticalSpacing(8);
    openButton_ = new QPushButton(QStringLiteral("打开"), this);
    openButton_->setObjectName(QStringLiteral("notebookOpenButton"));
    renameButton_ = new QPushButton(QStringLiteral("重命名"), this);
    renameButton_->setObjectName(QStringLiteral("notebookRenameButton"));
    recycleButton_ = new QPushButton(QStringLiteral("移入回收站"), this);
    recycleButton_->setObjectName(QStringLiteral("notebookRecycleButton"));
    permanentDeleteButton_ = new QPushButton(QStringLiteral("彻底删除"), this);
    permanentDeleteButton_->setObjectName(QStringLiteral("notebookPermanentDeleteButton"));
    for (QPushButton *button : {openButton_, renameButton_, recycleButton_, permanentDeleteButton_}) {
        button->setMinimumHeight(42);
    }
    connect(openButton_, &QPushButton::clicked, this, &NotebookLibraryPage::openSelected);
    connect(renameButton_, &QPushButton::clicked, this, &NotebookLibraryPage::renameSelected);
    connect(recycleButton_, &QPushButton::clicked,
            this, &NotebookLibraryPage::recycleOrRestoreSelected);
    connect(permanentDeleteButton_, &QPushButton::clicked,
            this, &NotebookLibraryPage::permanentlyDeleteSelected);
    layout->addLayout(actionsLayout_);
    status_ = new QLabel(this);
    status_->setObjectName(QStringLiteral("notebookStatus"));
    status_->setWordWrap(true);
    layout->addWidget(status_);
    databaseHolder_ = std::make_unique<NotebookDatabaseHolder>(
        QStringLiteral("notebook-library-%1").arg(
            QUuid::createUuid().toString(QUuid::WithoutBraces)));
    QString databaseError;
    if (databaseHolder_->database.open(databasePath, &databaseError)
        && databaseHolder_->database.migrate(&databaseError)) {
        service_ = std::make_unique<services::NotebookService>(
            databaseHolder_->database.connection(), std::move(dataRoot));
    } else {
        setStatus(QStringLiteral("无法打开自由笔记库：%1").arg(databaseError), true);
    }
    applyResponsiveLayout();
    refresh();
}

NotebookLibraryPage::~NotebookLibraryPage() = default;

void NotebookLibraryPage::refresh()
{
    const QUuid selected = selectedRecord() ? selectedRecord()->id : QUuid();
    QString error;
    records_ = service_ ? service_->listFree(recycleView_, &error)
                        : QVector<domain::NotebookRecord>{};
    list_->clear();
    for (const domain::NotebookRecord &record : std::as_const(records_)) {
        auto *item = new QListWidgetItem(
            QStringLiteral("%1\n%2")
                .arg(record.title,
                     record.updatedAt.toLocalTime().toString(QStringLiteral("yyyy-MM-dd HH:mm"))),
            list_);
        item->setData(Qt::UserRole, record.id.toString(QUuid::WithoutBraces));
        item->setSizeHint(QSize(0, 62));
        if (record.id == selected) list_->setCurrentItem(item);
    }
    emptyLabel_->setText(recycleView_
        ? QStringLiteral("回收站为空") : QStringLiteral("还没有自由笔记"));
    emptyLabel_->setVisible(records_.isEmpty());
    if (!error.isEmpty()) setStatus(error, true);
    updateActions();
}

void NotebookLibraryPage::markSaved(const QUuid &notebookId)
{
    QString error;
    if (!service_ || !service_->markSaved(notebookId, &error)) setStatus(error, true);
    else setStatus(QStringLiteral("笔记已保存"));
    refresh();
}

std::optional<domain::NotebookRecord> NotebookLibraryPage::record(
    const QUuid &notebookId) const
{
    const auto iterator = std::find_if(records_.cbegin(), records_.cend(),
        [&notebookId](const domain::NotebookRecord &record) {
            return record.id == notebookId;
        });
    return iterator == records_.cend()
        ? std::nullopt : std::optional<domain::NotebookRecord>(*iterator);
}

void NotebookLibraryPage::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    applyResponsiveLayout();
}

void NotebookLibraryPage::createNotebook()
{
    QString error;
    const auto created = service_
        ? service_->createFree(titleInput_->text(), &error) : std::nullopt;
    if (!created) {
        setStatus(error, true);
        return;
    }
    titleInput_->clear();
    refresh();
    emit openRequested(*created);
}

void NotebookLibraryPage::openSelected()
{
    const auto selected = selectedRecord();
    if (selected && !recycleView_) emit openRequested(*selected);
}

void NotebookLibraryPage::renameSelected()
{
    const auto selected = selectedRecord();
    if (!selected || recycleView_) return;
    bool accepted = false;
    const QString title = QInputDialog::getText(
        this, QStringLiteral("重命名自由笔记"), QStringLiteral("笔记标题"),
        QLineEdit::Normal, selected->title, &accepted).simplified();
    if (!accepted) return;
    QString error;
    if (!service_ || !service_->rename(selected->id, title, &error)) setStatus(error, true);
    else setStatus(QStringLiteral("笔记已重命名"));
    refresh();
}

void NotebookLibraryPage::recycleOrRestoreSelected()
{
    const auto selected = selectedRecord();
    if (!selected) return;
    if (!recycleView_ && QMessageBox::question(
            this, QStringLiteral("移入回收站"),
            QStringLiteral("将“%1”移入回收站？笔记文件会保留，可随时恢复。")
                .arg(selected->title)) != QMessageBox::Yes) {
        return;
    }
    QString error;
    const bool success = recycleView_
        ? (service_ && service_->restore(selected->id, &error))
        : (service_ && service_->recycle(selected->id, &error));
    setStatus(success
        ? (recycleView_ ? QStringLiteral("笔记已恢复") : QStringLiteral("笔记已移入回收站"))
        : error, !success);
    refresh();
}

void NotebookLibraryPage::permanentlyDeleteSelected()
{
    const auto selected = selectedRecord();
    if (!selected || !recycleView_) return;
    if (QMessageBox::warning(
            this, QStringLiteral("彻底删除自由笔记"),
            QStringLiteral("彻底删除“%1”？此操作无法恢复。可以先导出完整备份。")
                .arg(selected->title),
            QMessageBox::Yes | QMessageBox::Cancel,
            QMessageBox::Cancel) != QMessageBox::Yes) {
        return;
    }
    QString error;
    const bool success = service_ && service_->permanentlyDelete(selected->id, &error);
    setStatus(success ? QStringLiteral("笔记已彻底删除") : error, !success);
    refresh();
}

void NotebookLibraryPage::setRecycleView(bool enabled)
{
    recycleView_ = enabled;
    titleInput_->setVisible(!enabled);
    createButton_->setVisible(!enabled);
    recycleButton_->setText(enabled ? QStringLiteral("恢复") : QStringLiteral("移入回收站"));
    applyResponsiveLayout();
    refresh();
}

void NotebookLibraryPage::updateActions()
{
    const bool selected = selectedRecord().has_value();
    openButton_->setVisible(!recycleView_);
    renameButton_->setVisible(!recycleView_);
    permanentDeleteButton_->setVisible(recycleView_);
    openButton_->setEnabled(selected);
    renameButton_->setEnabled(selected);
    recycleButton_->setEnabled(selected);
    permanentDeleteButton_->setEnabled(selected);
}

void NotebookLibraryPage::applyResponsiveLayout()
{
    clearLayout(actionsLayout_);
    const bool phone = width() < 620;
    QVector<QPushButton *> buttons;
    if (!recycleView_) buttons = {openButton_, renameButton_, recycleButton_};
    else buttons = {recycleButton_, permanentDeleteButton_};
    const int columns = phone ? 2 : buttons.size();
    for (qsizetype index = 0; index < buttons.size(); ++index) {
        actionsLayout_->addWidget(
            buttons.at(index), static_cast<int>(index) / columns,
            static_cast<int>(index) % columns);
    }
    for (int column = 0; column < columns; ++column) actionsLayout_->setColumnStretch(column, 1);
}

std::optional<domain::NotebookRecord> NotebookLibraryPage::selectedRecord() const
{
    const QListWidgetItem *item = list_->currentItem();
    return item ? record(QUuid(item->data(Qt::UserRole).toString())) : std::nullopt;
}

void NotebookLibraryPage::setStatus(const QString &status, bool error)
{
    status_->setText(status);
    status_->setProperty("error", error);
    status_->style()->unpolish(status_);
    status_->style()->polish(status_);
}

} // namespace quizapp::ui
