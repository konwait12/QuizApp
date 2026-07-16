#include "ui/MaterialIconProvider.h"

#include <QPainter>
#include <QPixmap>
#include <QSvgRenderer>

static void ensureQuizAppResources()
{
    Q_INIT_RESOURCE(quizapp_native);
}

namespace quizapp::ui {
namespace {

QPixmap tintedPixmap(const QString &resourcePath, const QColor &color, int size)
{
    QSvgRenderer renderer(resourcePath);
    if (!renderer.isValid()) {
        return {};
    }

    QPixmap pixmap(size, size);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    renderer.render(&painter, QRectF(0, 0, size, size));
    painter.setCompositionMode(QPainter::CompositionMode_SourceIn);
    painter.fillRect(pixmap.rect(), color);
    return pixmap;
}

} // namespace

QIcon MaterialIconProvider::icon(
    const QString &name,
    const QColor &normalColor,
    const QColor &emphasisColor)
{
    ensureQuizAppResources();
    const QString path = QStringLiteral(":/quizapp/icons/%1.svg").arg(name);
    QIcon icon;
    for (const int size : {24, 48}) {
        const QPixmap normal = tintedPixmap(path, normalColor, size);
        const QPixmap emphasis = tintedPixmap(path, emphasisColor, size);
        icon.addPixmap(normal, QIcon::Normal, QIcon::Off);
        icon.addPixmap(emphasis, QIcon::Normal, QIcon::On);
        icon.addPixmap(emphasis, QIcon::Active, QIcon::Off);
        icon.addPixmap(emphasis, QIcon::Active, QIcon::On);
        icon.addPixmap(emphasis, QIcon::Selected, QIcon::Off);
        icon.addPixmap(emphasis, QIcon::Selected, QIcon::On);
    }
    return icon;
}

} // namespace quizapp::ui
