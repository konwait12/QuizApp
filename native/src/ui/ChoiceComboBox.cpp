#include "ui/ChoiceComboBox.h"

#include <QApplication>
#include <QListView>
#include <QPainter>
#include <QSizePolicy>
#include <QStyledItemDelegate>
#include <QStyleOptionViewItem>

#include <algorithm>

namespace quizapp::ui {
namespace {

class ChoiceItemDelegate final : public QStyledItemDelegate
{
public:
    explicit ChoiceItemDelegate(ChoiceComboBox *combo)
        : QStyledItemDelegate(combo)
        , combo_(combo)
    {
    }

    QSize sizeHint(
        const QStyleOptionViewItem &option,
        const QModelIndex &index) const override
    {
        QSize size = QStyledItemDelegate::sizeHint(option, index);
        size.setHeight(std::max(size.height(), 44));
        return size;
    }

    void paint(
        QPainter *painter,
        const QStyleOptionViewItem &option,
        const QModelIndex &index) const override
    {
        QStyleOptionViewItem item(option);
        initStyleOption(&item, index);
        const bool selected = item.state.testFlag(QStyle::State_Selected)
            || index.row() == combo_->currentIndex();
        const bool hovered = item.state.testFlag(QStyle::State_MouseOver);
        const bool enabled = item.state.testFlag(QStyle::State_Enabled);
        const auto appColor = [](const char *name, const QColor &fallback) {
            const QColor configured = qvariant_cast<QColor>(
                QApplication::instance()->property(name));
            return configured.isValid() ? configured : fallback;
        };
        const QColor accent = appColor(
            "quizappChoiceAccent", item.palette.color(QPalette::Highlight));
        const QColor accentSoft = appColor(
            "quizappChoiceAccentSoft", item.palette.color(QPalette::AlternateBase));
        const QColor normalText = appColor(
            "quizappChoiceText", item.palette.color(QPalette::Text));
        const QColor mutedText = appColor(
            "quizappChoiceMuted", item.palette.color(QPalette::Disabled, QPalette::Text));
        const QColor surface = appColor(
            "quizappChoiceSurface", item.palette.color(QPalette::Base));

        painter->save();
        painter->setRenderHint(QPainter::Antialiasing, true);
        painter->fillRect(item.rect, surface);
        if (selected || hovered) {
            QColor fill = accentSoft;
            if (hovered && !selected) {
                fill.setAlphaF(0.72);
            }
            painter->setPen(Qt::NoPen);
            painter->setBrush(fill);
            painter->drawRoundedRect(item.rect.adjusted(4, 3, -4, -3), 6, 6);
        }

        painter->setFont(item.font);
        painter->setPen(enabled ? selected ? accent : normalText : mutedText);
        QRect textRect = item.rect.adjusted(14, 0, selected ? -38 : -14, 0);
        const QString text = item.fontMetrics.elidedText(
            item.text, Qt::ElideRight, textRect.width());
        painter->drawText(
            textRect,
            Qt::AlignVCenter | Qt::AlignLeft | Qt::TextSingleLine,
            text);

        if (selected) {
            QPen pen(accent, 2.2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
            painter->setPen(pen);
            const QPoint center(item.rect.right() - 20, item.rect.center().y());
            painter->drawLine(center + QPoint(-5, 0), center + QPoint(-1, 4));
            painter->drawLine(center + QPoint(-1, 4), center + QPoint(6, -5));
        }
        painter->restore();
    }

private:
    ChoiceComboBox *combo_ = nullptr;
};

} // namespace

ChoiceComboBox::ChoiceComboBox(QWidget *parent)
    : QComboBox(parent)
{
    setMinimumWidth(0);
    setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    auto *popup = new QListView(this);
    popup->setObjectName(QStringLiteral("choiceComboPopup"));
    popup->setUniformItemSizes(true);
    popup->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    popup->setTextElideMode(Qt::ElideRight);
    setView(popup);
    setItemDelegate(new ChoiceItemDelegate(this));
}

} // namespace quizapp::ui
