#pragma once

#include <QColor>
#include <QIcon>
#include <QString>

namespace quizapp::ui {

class MaterialIconProvider final {
public:
    static QIcon icon(
        const QString &name,
        const QColor &normalColor,
        const QColor &emphasisColor);
};

} // namespace quizapp::ui
