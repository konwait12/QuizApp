#pragma once

#include <QColor>
#include <QString>

namespace quizapp::ui {

class EndfieldTheme final {
public:
    static QString styleSheet();
    static QColor iconNormal();
    static QColor iconEmphasis();
    static QColor iconOnPrimary();
};

} // namespace quizapp::ui
