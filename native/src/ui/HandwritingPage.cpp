#include "ui/HandwritingPage.h"

#include "ui/MaterialIconProvider.h"

#include "core/Document.h"
#include "core/DocumentViewport.h"
#include "core/ToolType.h"

#include <QAbstractButton>
#include <QButtonGroup>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFrame>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeyEvent>
#include <QLabel>
#include <QSaveFile>
#include <QScrollArea>
#include <QResizeEvent>
#include <QStandardPaths>
#include <QTimer>
#include <QToolButton>
#include <QPushButton>
#include <QVBoxLayout>

#include <utility>

namespace quizapp::ui {
namespace {

constexpr qreal kDefaultZoom = 1.0;

void assignMaterialIcon(QAbstractButton *button, const QString &name)
{
    button->setProperty("quizappIcon", name);
    button->setIconSize(QSize(22, 22));
}

QToolButton *toolbarButton(
    QWidget *parent,
    const QString &objectName,
    const QString &iconName,
    const QString &toolTip)
{
    auto *button = new QToolButton(parent);
    button->setObjectName(objectName);
    button->setToolTip(toolTip);
    button->setAccessibleName(toolTip);
    button->setAutoRaise(true);
    button->setMinimumSize(44, 44);
    assignMaterialIcon(button, iconName);
    return button;
}

QToolButton *toolToggle(
    QWidget *parent,
    const QString &objectName,
    const QString &iconName,
    const QString &toolTip)
{
    QToolButton *button = toolbarButton(parent, objectName, iconName, toolTip);
    button->setCheckable(true);
    return button;
}

QJsonObject pointToJson(const QPointF &point)
{
    QJsonObject object;
    object.insert(QStringLiteral("x"), point.x());
    object.insert(QStringLiteral("y"), point.y());
    return object;
}

QPointF pointFromJson(const QJsonObject &object)
{
    return QPointF(
        object.value(QStringLiteral("x")).toDouble(),
        object.value(QStringLiteral("y")).toDouble());
}

} // namespace

HandwritingPage::HandwritingPage(QString dataRoot, QWidget *parent)
    : QWidget(parent)
    , dataRoot_(std::move(dataRoot))
{
    setObjectName(QStringLiteral("handwritingPage"));
    setFocusPolicy(Qt::StrongFocus);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    toolbar_ = new QWidget(this);
    toolbar_->setObjectName(QStringLiteral("handwritingToolbar"));
    toolbarLayout_ = new QVBoxLayout(toolbar_);
    toolbarLayout_->setContentsMargins(14, 8, 14, 8);
    toolbarLayout_->setSpacing(8);

    auto *topRow = new QWidget(toolbar_);
    topRow->setObjectName(QStringLiteral("handwritingTopRow"));
    auto *topRowLayout = new QHBoxLayout(topRow);
    topRowLayout->setContentsMargins(0, 0, 0, 0);
    topRowLayout->setSpacing(8);

    auto *back = toolbarButton(
        topRow,
        QStringLiteral("handwritingBackButton"),
        QStringLiteral("arrow_back"),
        QStringLiteral("返回刷题"));
    connect(back, &QToolButton::clicked, this, &HandwritingPage::saveAndReturn);
    topRowLayout->addWidget(back);

    titleLabel_ = new QLabel(QStringLiteral("题目笔记"), topRow);
    titleLabel_->setObjectName(QStringLiteral("handwritingTitle"));
    topRowLayout->addWidget(titleLabel_);
    topRowLayout->addStretch();
    desktopPageLabel_ = new QLabel(QStringLiteral("第 1 / 1 页"), topRow);
    desktopPageLabel_->setObjectName(QStringLiteral("handwritingPageLabel"));
    topRowLayout->addWidget(desktopPageLabel_);
    toolbarLayout_->addWidget(topRow);

    toolStrip_ = new QWidget(toolbar_);
    toolStrip_->setObjectName(QStringLiteral("handwritingToolStrip"));
    auto *toolLayout = new QHBoxLayout(toolStrip_);
    toolLayout->setContentsMargins(0, 0, 0, 0);
    toolLayout->setSpacing(8);

    toolScroller_ = new QScrollArea(toolbar_);
    toolScroller_->setObjectName(QStringLiteral("handwritingToolScroller"));
    toolScroller_->setFrameShape(QFrame::NoFrame);
    toolScroller_->setWidgetResizable(false);
    toolScroller_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    toolScroller_->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    toolScroller_->setFixedHeight(48);
    toolbarLayout_->addWidget(toolScroller_);

    auto *toolGroup = new QButtonGroup(this);
    toolGroup->setExclusive(true);

    auto *pen = toolToggle(
        toolStrip_,
        QStringLiteral("handwritingPenButton"),
        QStringLiteral("stylus"),
        QStringLiteral("钢笔"));
    toolGroup->addButton(pen);
    pen->setChecked(true);
    connect(pen, &QToolButton::clicked, this, [this] {
        if (viewport_) {
            viewport_->setCurrentTool(ToolType::Pen);
        }
    });
    toolLayout->addWidget(pen);

    auto *eraser = toolToggle(
        toolStrip_,
        QStringLiteral("handwritingEraserButton"),
        QStringLiteral("ink_eraser"),
        QStringLiteral("橡皮擦"));
    toolGroup->addButton(eraser);
    connect(eraser, &QToolButton::clicked, this, [this] {
        if (viewport_) {
            viewport_->setCurrentTool(ToolType::Eraser);
        }
    });
    toolLayout->addWidget(eraser);

    auto *pan = toolToggle(
        toolStrip_,
        QStringLiteral("handwritingPanButton"),
        QStringLiteral("pan_tool"),
        QStringLiteral("移动画布"));
    toolGroup->addButton(pan);
    connect(pan, &QToolButton::clicked, this, [this] {
        if (viewport_) {
            viewport_->setCurrentTool(ToolType::Pan);
        }
    });
    toolLayout->addWidget(pan);

    auto *undo = toolbarButton(
        toolStrip_,
        QStringLiteral("handwritingUndoButton"),
        QStringLiteral("undo"),
        QStringLiteral("撤销"));
    connect(undo, &QToolButton::clicked, this, [this] {
        if (viewport_) {
            viewport_->undo();
        }
    });
    toolLayout->addWidget(undo);

    auto *redo = toolbarButton(
        toolStrip_,
        QStringLiteral("handwritingRedoButton"),
        QStringLiteral("redo"),
        QStringLiteral("重做"));
    connect(redo, &QToolButton::clicked, this, [this] {
        if (viewport_) {
            viewport_->redo();
        }
    });
    toolLayout->addWidget(redo);

    auto *zoomOut = toolbarButton(
        toolStrip_,
        QStringLiteral("handwritingZoomOutButton"),
        QStringLiteral("zoom_out"),
        QStringLiteral("缩小"));
    connect(zoomOut, &QToolButton::clicked, this, [this] {
        if (viewport_) {
            viewport_->zoomOut();
        }
    });
    toolLayout->addWidget(zoomOut);

    zoomLabel_ = new QLabel(QStringLiteral("100%"), toolStrip_);
    zoomLabel_->setObjectName(QStringLiteral("handwritingZoomLabel"));
    zoomLabel_->setAlignment(Qt::AlignCenter);
    zoomLabel_->setFixedWidth(54);
    toolLayout->addWidget(zoomLabel_);

    auto *zoomIn = toolbarButton(
        toolStrip_,
        QStringLiteral("handwritingZoomInButton"),
        QStringLiteral("zoom_in"),
        QStringLiteral("放大"));
    connect(zoomIn, &QToolButton::clicked, this, [this] {
        if (viewport_) {
            viewport_->zoomIn();
        }
    });
    toolLayout->addWidget(zoomIn);

    auto *fit = toolbarButton(
        toolStrip_,
        QStringLiteral("handwritingFitButton"),
        QStringLiteral("fit_screen"),
        QStringLiteral("适应页面"));
    connect(fit, &QToolButton::clicked, this, [this] {
        if (viewport_) {
            viewport_->zoomToFit();
        }
    });
    toolLayout->addWidget(fit);
    toolLayout->addStretch();
    toolStrip_->adjustSize();
    toolScroller_->setWidget(toolStrip_);

    layout->addWidget(toolbar_);

    mobilePageBar_ = new QWidget(this);
    mobilePageBar_->setObjectName(QStringLiteral("handwritingMobilePageBar"));
    auto *mobilePageLayout = new QHBoxLayout(mobilePageBar_);
    mobilePageLayout->setContentsMargins(12, 6, 12, 6);
    mobilePageLayout->setSpacing(8);
    previousPageButton_ = toolbarButton(
        mobilePageBar_,
        QStringLiteral("handwritingPreviousPageButton"),
        QStringLiteral("chevron_left"),
        QStringLiteral("上一页"));
    connect(previousPageButton_, &QToolButton::clicked, this, [this] {
        goToRelativePage(-1);
    });
    mobilePageLayout->addWidget(previousPageButton_);
    mobilePageLabel_ = new QLabel(QStringLiteral("第 1 / 1 页"), mobilePageBar_);
    mobilePageLabel_->setObjectName(QStringLiteral("handwritingMobilePageLabel"));
    mobilePageLabel_->setAlignment(Qt::AlignCenter);
    mobilePageLayout->addWidget(mobilePageLabel_, 1);
    nextPageButton_ = toolbarButton(
        mobilePageBar_,
        QStringLiteral("handwritingNextPageButton"),
        QStringLiteral("chevron_right"),
        QStringLiteral("下一页"));
    connect(nextPageButton_, &QToolButton::clicked, this, [this] {
        goToRelativePage(1);
    });
    mobilePageLayout->addWidget(nextPageButton_);
    auto *mobileAddPage = toolbarButton(
        mobilePageBar_,
        QStringLiteral("handwritingMobileAddPageButton"),
        QStringLiteral("add"),
        QStringLiteral("新建页面"));
    connect(mobileAddPage, &QToolButton::clicked, this, &HandwritingPage::addPage);
    mobilePageLayout->addWidget(mobileAddPage);
    layout->addWidget(mobilePageBar_);

    auto *workspace = new QWidget(this);
    workspace->setObjectName(QStringLiteral("handwritingWorkspace"));
    auto *workspaceLayout = new QHBoxLayout(workspace);
    workspaceLayout->setContentsMargins(0, 0, 0, 0);
    workspaceLayout->setSpacing(0);

    pagePanel_ = new QFrame(workspace);
    pagePanel_->setObjectName(QStringLiteral("handwritingPagePanel"));
    pagePanel_->setFixedWidth(164);
    auto *pagePanelLayout = new QVBoxLayout(pagePanel_);
    pagePanelLayout->setContentsMargins(10, 12, 10, 12);
    pagePanelLayout->setSpacing(10);
    auto *pagePanelTitle = new QLabel(QStringLiteral("页面"), pagePanel_);
    pagePanelTitle->setObjectName(QStringLiteral("handwritingPagePanelTitle"));
    pagePanelLayout->addWidget(pagePanelTitle);
    auto *pageScroll = new QScrollArea(pagePanel_);
    pageScroll->setObjectName(QStringLiteral("handwritingPageScroller"));
    pageScroll->setWidgetResizable(true);
    pageScroll->setFrameShape(QFrame::NoFrame);
    pageScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    auto *pageContainer = new QWidget(pageScroll);
    pageContainer->setObjectName(QStringLiteral("handwritingPageList"));
    pageButtonsLayout_ = new QVBoxLayout(pageContainer);
    pageButtonsLayout_->setContentsMargins(0, 0, 0, 0);
    pageButtonsLayout_->setSpacing(8);
    pageButtonsLayout_->addStretch();
    pageScroll->setWidget(pageContainer);
    pagePanelLayout->addWidget(pageScroll, 1);
    addPageButton_ = new QPushButton(QStringLiteral("新建页面"), pagePanel_);
    addPageButton_->setObjectName(QStringLiteral("handwritingAddPageButton"));
    assignMaterialIcon(addPageButton_, QStringLiteral("add"));
    addPageButton_->setMinimumHeight(44);
    connect(addPageButton_, &QPushButton::clicked, this, &HandwritingPage::addPage);
    pagePanelLayout->addWidget(addPageButton_);
    workspaceLayout->addWidget(pagePanel_);

    viewport_ = new DocumentViewport(workspace);
    viewport_->setObjectName(QStringLiteral("questionDocumentViewport"));
    viewport_->setMinimumSize(0, 0);
    viewport_->setFocusPolicy(Qt::StrongFocus);
    viewport_->setTouchGestureMode(TouchGestureMode::Full);
    connect(viewport_, &DocumentViewport::zoomChanged,
            this, &HandwritingPage::updateZoomLabel);
    connect(viewport_, &DocumentViewport::currentPageChanged,
            this, &HandwritingPage::updatePageState);
    workspaceLayout->addWidget(viewport_, 1);
    layout->addWidget(workspace, 1);

    statusLabel_ = new QLabel(this);
    statusLabel_->setObjectName(QStringLiteral("handwritingStatus"));
    statusLabel_->setMinimumHeight(34);
    statusLabel_->setContentsMargins(16, 0, 16, 0);
    layout->addWidget(statusLabel_);
    applyResponsiveLayout();
}

HandwritingPage::~HandwritingPage() = default;

void HandwritingPage::openNotebook(const domain::NotebookLaunchContext &context)
{
    returning_ = false;
    freeNotebookOpen_ = false;
    freeNotebookId_ = QUuid();
    freeNotebookTitle_.clear();
    context_ = context;
    currentBundlePath_ = bundlePathForCurrentQuestion();

    std::unique_ptr<Document> loaded;
    const QString manifestPath = QDir(currentBundlePath_).filePath(QStringLiteral("document.json"));
    if (QFileInfo::exists(manifestPath)) {
        loaded = Document::loadBundle(currentBundlePath_);
    }
    if (!loaded) {
        loaded = Document::createNew(
            QStringLiteral("题目 %1 笔记").arg(context_.questionIndex + 1),
            Document::Mode::Paged);
    }

    document_ = std::move(loaded);
    viewport_->setDocument(document_.get());
    viewport_->setCurrentTool(ToolType::Pen);
    titleLabel_->setText(QStringLiteral("第 %1 题笔记").arg(context_.questionIndex + 1));
    setStatus(QStringLiteral("题目笔记仅保存在当前设备"));
    refreshPageControls();
    QTimer::singleShot(0, this, &HandwritingPage::restoreViewportState);
    viewport_->setFocus();
}

bool HandwritingPage::openFreeNotebook(const domain::NotebookRecord &record)
{
    returning_ = false;
    freeNotebookOpen_ = true;
    freeNotebookId_ = record.id;
    freeNotebookTitle_ = record.title;
    context_ = {};
    if (!setFreeNotebookPath(record.relativePath)) {
        freeNotebookOpen_ = false;
        setStatus(QStringLiteral("自由笔记路径无效"));
        return false;
    }
    std::unique_ptr<Document> loaded;
    const QString manifestPath = QDir(currentBundlePath_).filePath(QStringLiteral("document.json"));
    if (QFileInfo::exists(manifestPath)) loaded = Document::loadBundle(currentBundlePath_);
    if (!loaded) loaded = Document::createNew(record.title, Document::Mode::Paged);
    loaded->name = record.title;
    document_ = std::move(loaded);
    viewport_->setDocument(document_.get());
    viewport_->setCurrentTool(ToolType::Pen);
    titleLabel_->setText(record.title);
    setStatus(QStringLiteral("自由笔记仅保存在当前设备"));
    refreshPageControls();
    QTimer::singleShot(0, this, &HandwritingPage::restoreViewportState);
    viewport_->setFocus();
    return true;
}

bool HandwritingPage::hasNotebookOpen() const
{
    return document_ != nullptr;
}

domain::NotebookLaunchContext HandwritingPage::currentContext() const
{
    return context_;
}

QString HandwritingPage::currentBundlePath() const
{
    return currentBundlePath_;
}

DocumentViewport *HandwritingPage::viewport() const
{
    return viewport_;
}

void HandwritingPage::saveAndReturn()
{
    if (returning_) {
        return;
    }
    returning_ = true;
    QString error;
    if (!saveDocument(&error) || !saveViewportState(&error)) {
        setStatus(QStringLiteral("笔记保存失败：%1").arg(error));
        returning_ = false;
        return;
    }
    setStatus(QStringLiteral("笔记已保存"));
    if (freeNotebookOpen_) emit returnToNotebookLibrary(freeNotebookId_);
    else emit returnToPractice(context_);
}

void HandwritingPage::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Back || event->key() == Qt::Key_Escape) {
        saveAndReturn();
        event->accept();
        return;
    }
    QWidget::keyPressEvent(event);
}

void HandwritingPage::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    applyResponsiveLayout();
}

void HandwritingPage::applyResponsiveLayout()
{
    const bool tablet = width() >= 760;
    if (pagePanel_) {
        pagePanel_->setVisible(tablet);
    }
    if (mobilePageBar_) {
        mobilePageBar_->setVisible(!tablet);
    }
    if (desktopPageLabel_) {
        desktopPageLabel_->setVisible(tablet);
    }
    if (toolbarLayout_) {
        toolbarLayout_->setContentsMargins(tablet ? 20 : 12, 8, tablet ? 20 : 12, 8);
    }
}

void HandwritingPage::refreshPageControls()
{
    if (!pageButtonsLayout_) {
        return;
    }
    while (pageButtonsLayout_->count() > 0) {
        QLayoutItem *item = pageButtonsLayout_->takeAt(0);
        delete item->widget();
        delete item;
    }
    pageButtons_.clear();

    const int count = document_ ? document_->pageCount() : 0;
    QWidget *container = pageButtonsLayout_->parentWidget();
    for (int index = 0; index < count; ++index) {
        auto *button = new QPushButton(
            QStringLiteral("第 %1 页").arg(index + 1), container);
        button->setObjectName(QStringLiteral("handwritingPageButton%1").arg(index));
        button->setProperty("pageIndex", index);
        button->setCheckable(true);
        button->setMinimumHeight(46);
        connect(button, &QPushButton::clicked, this, [this, index] {
            if (viewport_) {
                viewport_->scrollToPage(index);
                viewport_->setFocus();
            }
        });
        pageButtonsLayout_->addWidget(button);
        pageButtons_.append(button);
    }
    pageButtonsLayout_->addStretch();
    updatePageState(viewport_ ? viewport_->currentPageIndex() : 0);
}

void HandwritingPage::updatePageState(int pageIndex)
{
    const int count = document_ ? document_->pageCount() : 0;
    const int safeIndex = count > 0 ? qBound(0, pageIndex, count - 1) : 0;
    const QString pageText = QStringLiteral("第 %1 / %2 页")
                                 .arg(count > 0 ? safeIndex + 1 : 0)
                                 .arg(count);
    if (desktopPageLabel_) {
        desktopPageLabel_->setText(pageText);
    }
    if (mobilePageLabel_) {
        mobilePageLabel_->setText(pageText);
    }
    for (qsizetype index = 0; index < pageButtons_.size(); ++index) {
        pageButtons_.at(index)->setChecked(index == safeIndex);
    }
    if (previousPageButton_) {
        previousPageButton_->setEnabled(count > 0 && safeIndex > 0);
    }
    if (nextPageButton_) {
        nextPageButton_->setEnabled(count > 0 && safeIndex + 1 < count);
    }
}

void HandwritingPage::updateZoomLabel(qreal zoom)
{
    if (zoomLabel_) {
        zoomLabel_->setText(QStringLiteral("%1%").arg(qRound(zoom * 100.0)));
    }
}

void HandwritingPage::addPage()
{
    if (!document_ || !viewport_) {
        return;
    }
    document_->addPage();
    viewport_->notifyDocumentStructureChanged();
    refreshPageControls();
    const int index = document_->pageCount() - 1;
    viewport_->scrollToPage(index);
    setStatus(QStringLiteral("已新建第 %1 页").arg(index + 1));
    viewport_->setFocus();
}

void HandwritingPage::goToRelativePage(int delta)
{
    if (!document_ || !viewport_ || document_->pageCount() <= 0) {
        return;
    }
    const int next = qBound(
        0, viewport_->currentPageIndex() + delta, document_->pageCount() - 1);
    viewport_->scrollToPage(next);
    viewport_->setFocus();
}

QString HandwritingPage::effectiveDataRoot() const
{
    if (!dataRoot_.isEmpty()) {
        return dataRoot_;
    }
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
}

QString HandwritingPage::questionKey() const
{
    if (!context_.questionId.isNull()) {
        return context_.questionId.toString(QUuid::WithoutBraces);
    }
    if (!context_.sessionId.isNull()) {
        return QStringLiteral("session-%1").arg(
            context_.sessionId.toString(QUuid::WithoutBraces));
    }
    return QStringLiteral("unbound");
}

QString HandwritingPage::questionNotesDirectory() const
{
    if (freeNotebookOpen_) return QFileInfo(currentBundlePath_).absolutePath();
    return QDir(effectiveDataRoot()).filePath(QStringLiteral("notes/questions"));
}

QString HandwritingPage::bundlePathForCurrentQuestion() const
{
    return QDir(questionNotesDirectory()).filePath(QStringLiteral("%1.snb").arg(questionKey()));
}

QString HandwritingPage::viewportStatePathForCurrentQuestion() const
{
    if (freeNotebookOpen_) {
        return QDir(questionNotesDirectory()).filePath(
            QStringLiteral("%1.viewport.json").arg(
                freeNotebookId_.toString(QUuid::WithoutBraces)));
    }
    return QDir(questionNotesDirectory()).filePath(
        QStringLiteral("%1.viewport.json").arg(questionKey()));
}

bool HandwritingPage::setFreeNotebookPath(const QString &relativePath)
{
    if (relativePath.isEmpty() || QDir::isAbsolutePath(relativePath)
        || relativePath.contains(u'\\')) {
        return false;
    }
    const QString clean = QDir::cleanPath(relativePath);
    if (clean != relativePath || clean.startsWith(QStringLiteral("../"))
        || clean.contains(QStringLiteral("/../"))) {
        return false;
    }
    const QString root = QDir::fromNativeSeparators(
        QDir::cleanPath(QFileInfo(effectiveDataRoot()).absoluteFilePath()));
    const QString candidate = QDir::fromNativeSeparators(
        QDir::cleanPath(QDir(root).filePath(clean)));
    if (candidate != root
        && !candidate.startsWith(root + u'/', Qt::CaseInsensitive)) {
        return false;
    }
    currentBundlePath_ = candidate;
    return true;
}

void HandwritingPage::restoreViewportState()
{
    if (!viewport_) {
        return;
    }

    QFile file(viewportStatePathForCurrentQuestion());
    if (!file.open(QIODevice::ReadOnly)) {
        viewport_->zoomToFit();
        updatePageState(viewport_->currentPageIndex());
        return;
    }

    const QJsonObject state = QJsonDocument::fromJson(file.readAll()).object();
    const int page = state.value(QStringLiteral("page")).toInt(0);
    const qreal zoom = state.value(QStringLiteral("zoom")).toDouble(kDefaultZoom);
    const QPointF pan = pointFromJson(state.value(QStringLiteral("pan")).toObject());

    viewport_->scrollToPage(page);
    viewport_->setZoomLevel(zoom > 0 ? zoom : kDefaultZoom);
    viewport_->setPanOffset(pan);
    updatePageState(viewport_->currentPageIndex());
    updateZoomLabel(viewport_->zoomLevel());
}

bool HandwritingPage::saveDocument(QString *errorMessage)
{
    if (!document_) {
        return true;
    }
    if (currentBundlePath_.isEmpty()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("缺少笔记路径");
        }
        return false;
    }
    if (!QDir().mkpath(currentBundlePath_)) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("无法创建笔记目录");
        }
        return false;
    }
    if (!document_->saveBundle(currentBundlePath_)) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("SpeedyNote 保存失败");
        }
        return false;
    }
    return true;
}

bool HandwritingPage::saveViewportState(QString *errorMessage) const
{
    if (!viewport_) {
        return true;
    }
    if (!QDir().mkpath(questionNotesDirectory())) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("无法创建视口状态目录");
        }
        return false;
    }

    QJsonObject state;
    state.insert(QStringLiteral("questionId"), questionKey());
    if (freeNotebookOpen_) {
        state.insert(QStringLiteral("notebookId"),
                     freeNotebookId_.toString(QUuid::WithoutBraces));
    }
    state.insert(QStringLiteral("sessionId"), context_.sessionId.toString(QUuid::WithoutBraces));
    state.insert(QStringLiteral("page"), viewport_->currentPageIndex());
    state.insert(QStringLiteral("zoom"), viewport_->zoomLevel());
    state.insert(QStringLiteral("pan"), pointToJson(viewport_->panOffset()));
    state.insert(QStringLiteral("savedAt"), QDateTime::currentDateTimeUtc().toString(Qt::ISODate));

    QSaveFile file(viewportStatePathForCurrentQuestion());
    if (!file.open(QIODevice::WriteOnly)) {
        if (errorMessage) {
            *errorMessage = file.errorString();
        }
        return false;
    }
    file.write(QJsonDocument(state).toJson(QJsonDocument::Indented));
    if (!file.commit()) {
        if (errorMessage) {
            *errorMessage = file.errorString();
        }
        return false;
    }
    return true;
}

void HandwritingPage::setStatus(const QString &text)
{
    if (statusLabel_) {
        statusLabel_->setText(text);
    }
}

} // namespace quizapp::ui
