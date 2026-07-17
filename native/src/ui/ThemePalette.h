#pragma once

#include <QColor>
#include <QString>
#include <QVector>

namespace quizapp::ui {

struct ThemePalette {
    QString id;
    QString name;
    QColor primary;
    QColor secondary;
    QColor success;
    QColor danger;
    QColor warning;
    QColor lightBackground;
    QColor lightSurface;
    QColor lightText;
    QColor lightMuted;
    QColor lightLine;
    QColor darkBackground;
    QColor darkSurface;
    QColor darkText;
    QColor darkMuted;
    QColor darkLine;
};

class ThemePalettes final {
public:
    static const QVector<ThemePalette> &legacyPresets();
    static const ThemePalette &find(const QString &id);
    static bool isLegacyPreset(const QString &id);
};

} // namespace quizapp::ui
