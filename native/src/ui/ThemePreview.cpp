#include "ui/ThemePreview.h"
#include "ui/ThemePalette.h"

#include <QGuiApplication>
#include <QPainter>
#include <QPainterPath>
#include <QPaintEvent>
#include <QStyleHints>

namespace quizapp::ui {
namespace {

struct PreviewPalette {
    QColor background;
    QColor surface;
    QColor surfaceMuted;
    QColor line;
    QColor text;
    QColor accent;
    int radius = 6;
};

PreviewPalette paletteForTheme(
    const QString &themeId,
    const QString &paletteId,
    int cornerRadius)
{
    QString resolved = themeId;
    if (resolved == QStringLiteral("system")) {
        resolved = QGuiApplication::styleHints()->colorScheme() == Qt::ColorScheme::Dark
            ? QStringLiteral("dark") : QStringLiteral("light");
    }
    if (paletteId == QStringLiteral("endfield")) {
        return {
            QColor(QStringLiteral("#101113")), QColor(QStringLiteral("#18171a")),
            QColor(QStringLiteral("#25262b")), QColor(QStringLiteral("#414248")),
            QColor(QStringLiteral("#f4f4f2")), QColor(QStringLiteral("#fdfc00")),
            qBound(0, cornerRadius, 18)};
    }
    const ThemePalette &preset = ThemePalettes::find(paletteId);
    const bool dark = resolved == QStringLiteral("dark");
    return {
        dark ? preset.darkBackground : preset.lightBackground,
        dark ? preset.darkSurface : preset.lightSurface,
        dark ? preset.darkLine : preset.lightBackground,
        dark ? preset.darkLine : preset.lightLine,
        dark ? preset.darkText : preset.lightText,
        preset.primary,
        qBound(0, cornerRadius, 18)};
}

} // namespace

ThemePreview::ThemePreview(QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("themePreview"));
    setAccessibleName(QStringLiteral("主题预览"));
    setMinimumHeight(132);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
}

QString ThemePreview::themeId() const
{
    return themeId_;
}

QString ThemePreview::paletteId() const
{
    return paletteId_;
}

void ThemePreview::setThemeId(const QString &themeId)
{
    if (themeId_ == themeId) {
        return;
    }
    themeId_ = themeId;
    update();
}

void ThemePreview::setPaletteId(const QString &paletteId)
{
    if (paletteId_ == paletteId) {
        return;
    }
    paletteId_ = paletteId;
    update();
}

void ThemePreview::setCornerRadius(int radius)
{
    const int bounded = qBound(0, radius, 18);
    if (cornerRadius_ == bounded) {
        return;
    }
    cornerRadius_ = bounded;
    update();
}

QSize ThemePreview::sizeHint() const
{
    return QSize(560, 132);
}

void ThemePreview::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event)
    const PreviewPalette colors = paletteForTheme(themeId_, paletteId_, cornerRadius_);
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    const QRectF frame = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
    painter.setPen(QPen(colors.line, 1));
    painter.setBrush(colors.background);
    painter.drawRoundedRect(frame, colors.radius, colors.radius);

    const qreal railWidth = qMin<qreal>(56.0, frame.width() * 0.16);
    const QRectF rail(frame.left(), frame.top(), railWidth, frame.height());
    painter.save();
    painter.setClipPath([&] {
        QPainterPath path;
        path.addRoundedRect(frame, colors.radius, colors.radius);
        return path;
    }());
    painter.fillRect(rail, colors.surface);
    painter.fillRect(QRectF(
        frame.left() + railWidth, frame.top(), frame.width() - railWidth, 27), colors.surface);
    painter.restore();

    painter.setPen(Qt::NoPen);
    painter.setBrush(colors.accent);
    painter.drawRoundedRect(
        QRectF(frame.left() + 14, frame.top() + 10, railWidth - 28, 18),
        qMin(3, colors.radius), qMin(3, colors.radius));
    for (int index = 0; index < 3; ++index) {
        painter.setBrush(index == 0 ? colors.accent : colors.line);
        painter.drawRoundedRect(
            QRectF(frame.left() + 18, frame.top() + 48 + index * 22, railWidth - 36, 4),
            1, 1);
    }

    const qreal contentLeft = frame.left() + railWidth + 18;
    const qreal contentWidth = frame.right() - contentLeft - 16;
    painter.setBrush(colors.text);
    painter.drawRoundedRect(QRectF(contentLeft, frame.top() + 45, contentWidth * 0.32, 7), 2, 2);
    painter.setBrush(colors.line);
    painter.drawRoundedRect(QRectF(contentLeft, frame.top() + 58, contentWidth * 0.48, 4), 1, 1);

    const qreal cardGap = 8;
    const qreal cardWidth = (contentWidth - cardGap * 2) / 3;
    for (int index = 0; index < 3; ++index) {
        const QRectF card(
            contentLeft + index * (cardWidth + cardGap), frame.top() + 76, cardWidth, 38);
        painter.setPen(QPen(colors.line, 1));
        painter.setBrush(index == 0 ? colors.surfaceMuted : colors.surface);
        painter.drawRoundedRect(card, colors.radius, colors.radius);
        painter.setPen(Qt::NoPen);
        painter.setBrush(index == 0 ? colors.accent : colors.text);
        painter.drawRoundedRect(card.adjusted(9, 10, -card.width() * 0.54, -21), 1, 1);
        painter.setBrush(colors.line);
        painter.drawRoundedRect(card.adjusted(9, 24, -card.width() * 0.32, -10), 1, 1);
    }
}

} // namespace quizapp::ui
