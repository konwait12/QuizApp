#include "ui/ThemePalette.h"

namespace quizapp::ui {
namespace {

ThemePalette palette(
    const QString &id,
    const QString &name,
    const char *primary,
    const char *secondary,
    const char *success,
    const char *danger,
    const char *warning,
    const char *lightBackground,
    const char *lightSurface,
    const char *lightText,
    const char *lightMuted,
    const char *lightLine,
    const char *darkBackground,
    const char *darkSurface,
    const char *darkText,
    const char *darkMuted,
    const char *darkLine)
{
    return {
        id, name,
        QColor(QString::fromLatin1(primary)),
        QColor(QString::fromLatin1(secondary)),
        QColor(QString::fromLatin1(success)),
        QColor(QString::fromLatin1(danger)),
        QColor(QString::fromLatin1(warning)),
        QColor(QString::fromLatin1(lightBackground)),
        QColor(QString::fromLatin1(lightSurface)),
        QColor(QString::fromLatin1(lightText)),
        QColor(QString::fromLatin1(lightMuted)),
        QColor(QString::fromLatin1(lightLine)),
        QColor(QString::fromLatin1(darkBackground)),
        QColor(QString::fromLatin1(darkSurface)),
        QColor(QString::fromLatin1(darkText)),
        QColor(QString::fromLatin1(darkMuted)),
        QColor(QString::fromLatin1(darkLine)),
    };
}

const QVector<ThemePalette> kPresets{
    palette(
        QStringLiteral("classic"), QStringLiteral("清爽蓝"),
        "#4f6ef7", "#5b6472", "#22b07d", "#f24e5e", "#f5a623",
        "#f8f9fb", "#ffffff", "#1a1d28", "#8e94a3", "#edf0f4",
        "#12151c", "#1b202b", "#f4f6fb", "#9aa3b5", "#2b3240"),
    palette(
        QStringLiteral("forest"), QStringLiteral("森林绿"),
        "#1f8f62", "#5f6f62", "#22a06b", "#d94a5a", "#b98320",
        "#f3f7ee", "#ffffff", "#18211b", "#74816f", "#e2eadb",
        "#07120c", "#102119", "#eff9f1", "#9bb09d", "#223d2c"),
    palette(
        QStringLiteral("ink"), QStringLiteral("墨黑灰"),
        "#222831", "#6b7280", "#4b8f76", "#c7515d", "#9a741f",
        "#f4f4f2", "#ffffff", "#151719", "#717780", "#dedfdb",
        "#090a0c", "#17191d", "#f1f2f4", "#9da3ad", "#2b2f35"),
    palette(
        QStringLiteral("sunset"), QStringLiteral("日落橙"),
        "#d8662d", "#6b625d", "#2c9c72", "#d94b54", "#d49424",
        "#faf7f2", "#ffffff", "#241a14", "#8a7d72", "#eee6dd",
        "#19120f", "#241b17", "#fff4ed", "#b4a299", "#3a2b24"),
    palette(
        QStringLiteral("berry"), QStringLiteral("莓果红"),
        "#c23b68", "#665c68", "#259d7a", "#de4052", "#d59620",
        "#fbf7fa", "#ffffff", "#241923", "#897a88", "#efe5ee",
        "#181018", "#251824", "#fff1fb", "#b49aac", "#3b2a38"),
    palette(
        QStringLiteral("cyan"), QStringLiteral("湖青色"),
        "#0f8b8d", "#5b6870", "#1fa878", "#d84d5c", "#c98d20",
        "#f3f9f9", "#ffffff", "#142021", "#728485", "#e0eeee",
        "#0d1718", "#172426", "#eefafa", "#94aaad", "#263a3d"),
};

} // namespace

const QVector<ThemePalette> &ThemePalettes::legacyPresets()
{
    return kPresets;
}

const ThemePalette &ThemePalettes::find(const QString &id)
{
    for (const ThemePalette &preset : kPresets) {
        if (preset.id == id) {
            return preset;
        }
    }
    return kPresets.at(1);
}

bool ThemePalettes::isLegacyPreset(const QString &id)
{
    for (const ThemePalette &preset : kPresets) {
        if (preset.id == id) {
            return true;
        }
    }
    return false;
}

} // namespace quizapp::ui
