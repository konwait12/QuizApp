#include "ui/AnswerTablePage.h"

#include <QAbstractTableModel>
#include <QFrame>
#include <QGridLayout>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QItemSelectionModel>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QResizeEvent>
#include <QScrollBar>
#include <QSortFilterProxyModel>
#include <QTableView>
#include <QVBoxLayout>

namespace quizapp::ui {
namespace {

constexpr int kQuestionIndexRole = Qt::UserRole + 1;

QString typeText(domain::QuestionType type)
{
    switch (type) {
    case domain::QuestionType::Single:
        return QStringLiteral("单选题");
    case domain::QuestionType::Multiple:
        return QStringLiteral("多选题");
    case domain::QuestionType::Boolean:
        return QStringLiteral("判断题");
    case domain::QuestionType::Subjective:
        return QStringLiteral("主观题");
    }
    return {};
}

void clearLayoutItems(QLayout *layout)
{
    while (layout && layout->count() > 0) {
        delete layout->takeAt(0);
    }
}

} // namespace

class AnswerTableModel final : public QAbstractTableModel {
public:
    explicit AnswerTableModel(QObject *parent = nullptr)
        : QAbstractTableModel(parent)
    {
    }

    void setContent(
        const QVector<domain::Question> &questions,
        const QVector<QUuid> &questionOrder)
    {
        beginResetModel();
        rows_.clear();
        QHash<QUuid, const domain::Question *> byId;
        for (const domain::Question &question : questions) {
            byId.insert(question.id, &question);
        }
        rows_.reserve(questionOrder.size());
        for (const QUuid &questionId : questionOrder) {
            const auto found = byId.constFind(questionId);
            if (found != byId.cend()) {
                rows_.append(**found);
            }
        }
        endResetModel();
    }

    int rowCount(const QModelIndex &parent = QModelIndex()) const override
    {
        return parent.isValid() ? 0 : rows_.size();
    }

    int columnCount(const QModelIndex &parent = QModelIndex()) const override
    {
        return parent.isValid() ? 0 : 4;
    }

    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override
    {
        if (!index.isValid() || index.row() < 0 || index.row() >= rows_.size()) {
            return {};
        }
        const domain::Question &question = rows_.at(index.row());
        if (role == kQuestionIndexRole) {
            return index.row();
        }
        if (role == Qt::ToolTipRole) {
            return question.path.join(QStringLiteral(" / "));
        }
        if (role != Qt::DisplayRole) {
            return {};
        }
        switch (index.column()) {
        case 0:
            return index.row() + 1;
        case 1:
            return typeText(question.type);
        case 2:
            return question.prompt;
        case 3:
            if (!question.correctAnswer.trimmed().isEmpty()) {
                return question.correctAnswer;
            }
            if (!question.builtinExplanation.text.trimmed().isEmpty()
                || !question.builtinExplanation.imageBlobIds.isEmpty()) {
                return QStringLiteral("见内置解析");
            }
            return QStringLiteral("暂无");
        default:
            return {};
        }
    }

    QVariant headerData(
        int section,
        Qt::Orientation orientation,
        int role = Qt::DisplayRole) const override
    {
        if (orientation != Qt::Horizontal || role != Qt::DisplayRole) {
            return {};
        }
        const QStringList headers{
            QStringLiteral("题号"),
            QStringLiteral("题型"),
            QStringLiteral("题目"),
            QStringLiteral("答案"),
        };
        return section >= 0 && section < headers.size() ? headers.at(section) : QVariant();
    }

private:
    QVector<domain::Question> rows_;
};

AnswerTablePage::AnswerTablePage(QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("answerTablePage"));
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(18, 16, 18, 18);
    root->setSpacing(12);

    auto *header = new QFrame(this);
    header->setObjectName(QStringLiteral("answerTableHeader"));
    headerLayout_ = new QGridLayout(header);
    headerLayout_->setContentsMargins(0, 0, 0, 0);
    headerLayout_->setHorizontalSpacing(10);
    headerLayout_->setVerticalSpacing(8);
    backButton_ = new QPushButton(QStringLiteral("返回题库"), header);
    backButton_->setObjectName(QStringLiteral("answerTableBackButton"));
    backButton_->setProperty("quizappIcon", QStringLiteral("arrow_back"));
    backButton_->setMinimumHeight(42);
    connect(backButton_, &QPushButton::clicked, this, &AnswerTablePage::backRequested);

    titleContainer_ = new QWidget(header);
    titleContainer_->setObjectName(QStringLiteral("answerTableTitleContainer"));
    auto *titles = new QVBoxLayout(titleContainer_);
    titles->setContentsMargins(0, 0, 0, 0);
    titles->setSpacing(2);
    title_ = new QLabel(QStringLiteral("答案表"), titleContainer_);
    title_->setObjectName(QStringLiteral("answerTableTitle"));
    title_->setWordWrap(true);
    summary_ = new QLabel(titleContainer_);
    summary_->setObjectName(QStringLiteral("answerTableSummary"));
    titles->addWidget(title_);
    titles->addWidget(summary_);
    detailButton_ = new QPushButton(QStringLiteral("查看详情"), header);
    detailButton_->setObjectName(QStringLiteral("answerTableDetailButton"));
    detailButton_->setProperty("quizappIcon", QStringLiteral("grid_view"));
    detailButton_->setMinimumHeight(42);
    connect(detailButton_, &QPushButton::clicked, this, [this] {
        emitForCurrentSelection(false);
    });
    handwritingButton_ = new QPushButton(QStringLiteral("手写"), header);
    handwritingButton_->setObjectName(QStringLiteral("answerTableHandwritingButton"));
    handwritingButton_->setProperty("quizappIcon", QStringLiteral("stylus"));
    handwritingButton_->setMinimumHeight(42);
    connect(handwritingButton_, &QPushButton::clicked, this, [this] {
        emitForCurrentSelection(true);
    });
    root->addWidget(header);

    search_ = new QLineEdit(this);
    search_->setObjectName(QStringLiteral("answerTableSearch"));
    search_->setPlaceholderText(QStringLiteral("搜索题目、题型或答案"));
    search_->setClearButtonEnabled(true);
    search_->setMinimumHeight(42);
    root->addWidget(search_);

    model_ = new AnswerTableModel(this);
    proxyModel_ = new QSortFilterProxyModel(this);
    proxyModel_->setSourceModel(model_);
    proxyModel_->setFilterCaseSensitivity(Qt::CaseInsensitive);
    proxyModel_->setFilterKeyColumn(-1);
    connect(search_, &QLineEdit::textChanged,
            proxyModel_, &QSortFilterProxyModel::setFilterFixedString);

    table_ = new QTableView(this);
    table_->setObjectName(QStringLiteral("answerTableView"));
    table_->setModel(proxyModel_);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setSelectionMode(QAbstractItemView::SingleSelection);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->setAlternatingRowColors(false);
    table_->setShowGrid(false);
    table_->setWordWrap(false);
    table_->verticalHeader()->hide();
    table_->verticalHeader()->setDefaultSectionSize(48);
    table_->horizontalHeader()->setStretchLastSection(false);
    table_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    connect(table_->selectionModel(), &QItemSelectionModel::currentRowChanged,
            this, [this](const QModelIndex &current) {
                updateSelectionActions();
                if (current.isValid()) {
                    const QModelIndex source = proxyModel_->mapToSource(current);
                    emit currentQuestionChanged(source.row());
                }
            });
    connect(table_, &QTableView::doubleClicked, this, [this](const QModelIndex &) {
        emitForCurrentSelection(false);
    });
    root->addWidget(table_, 1);
    updateSelectionActions();
    updateResponsiveColumns();
}

void AnswerTablePage::start(
    const domain::InstalledBankSummary &scope,
    const QVector<domain::Question> &questions,
    const domain::PracticeSession &session)
{
    search_->clear();
    model_->setContent(questions, session.questionOrder);
    title_->setText(scope.path.isEmpty()
        ? QStringLiteral("全部题库 · 答案表")
        : QStringLiteral("%1 · 答案表").arg(scope.path.join(QStringLiteral(" / "))));
    summary_->setText(QStringLiteral("%1 道题").arg(model_->rowCount()));
    setCurrentQuestionIndex(session.currentIndex);
    updateResponsiveColumns();
}

qsizetype AnswerTablePage::currentQuestionIndex() const
{
    if (!table_->currentIndex().isValid()) {
        return -1;
    }
    return proxyModel_->mapToSource(table_->currentIndex()).row();
}

void AnswerTablePage::setCurrentQuestionIndex(qsizetype questionIndex)
{
    if (questionIndex < 0 || questionIndex >= model_->rowCount()) {
        table_->clearSelection();
        table_->setCurrentIndex({});
        updateSelectionActions();
        return;
    }
    const QModelIndex source = model_->index(questionIndex, 0);
    const QModelIndex proxy = proxyModel_->mapFromSource(source);
    if (!proxy.isValid()) {
        return;
    }
    table_->setCurrentIndex(proxy);
    table_->selectRow(proxy.row());
    table_->scrollTo(proxy, QAbstractItemView::PositionAtCenter);
    updateSelectionActions();
}

bool AnswerTablePage::hasContent() const
{
    return model_->rowCount() > 0;
}

void AnswerTablePage::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    updateResponsiveColumns();
}

void AnswerTablePage::updateResponsiveColumns()
{
    const bool compact = width() < 620;
    clearLayoutItems(headerLayout_);
    if (compact) {
        backButton_->setText(QStringLiteral("返回"));
        detailButton_->setText(QStringLiteral("详情"));
        headerLayout_->addWidget(backButton_, 0, 0);
        headerLayout_->addWidget(titleContainer_, 0, 1);
        headerLayout_->addWidget(detailButton_, 1, 0);
        headerLayout_->addWidget(handwritingButton_, 1, 1);
        headerLayout_->setColumnStretch(0, 1);
        headerLayout_->setColumnStretch(1, 1);
        headerLayout_->setColumnStretch(2, 0);
        headerLayout_->setColumnStretch(3, 0);
    } else {
        backButton_->setText(QStringLiteral("返回题库"));
        detailButton_->setText(QStringLiteral("查看详情"));
        headerLayout_->addWidget(backButton_, 0, 0);
        headerLayout_->addWidget(titleContainer_, 0, 1);
        headerLayout_->addWidget(detailButton_, 0, 2);
        headerLayout_->addWidget(handwritingButton_, 0, 3);
        headerLayout_->setColumnStretch(0, 0);
        headerLayout_->setColumnStretch(1, 1);
        headerLayout_->setColumnStretch(2, 0);
        headerLayout_->setColumnStretch(3, 0);
    }
    table_->setColumnHidden(1, compact);
    table_->setColumnWidth(0, compact ? 54 : 70);
    table_->setColumnWidth(1, 88);
    table_->setColumnWidth(3, compact ? 94 : 130);
    table_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
}

void AnswerTablePage::updateSelectionActions()
{
    const bool selected = table_->currentIndex().isValid();
    detailButton_->setEnabled(selected);
    handwritingButton_->setEnabled(selected);
}

void AnswerTablePage::emitForCurrentSelection(bool handwriting)
{
    const qsizetype questionIndex = currentQuestionIndex();
    if (questionIndex < 0) {
        return;
    }
    if (handwriting) {
        emit handwritingRequested(questionIndex);
    } else {
        emit detailRequested(questionIndex);
    }
}

} // namespace quizapp::ui
