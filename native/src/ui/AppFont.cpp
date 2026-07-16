#include "ui/AppFont.h"

#include <QApplication>
#include <QDir>
#include <QFileInfo>
#include <QFont>
#include <QFontDatabase>
#include <QRawFont>
#include <QStringList>

namespace quizapp::ui {
namespace {

bool fontSupportsChinese(const QFont &font)
{
    const QRawFont rawFont = QRawFont::fromFont(font);
    return rawFont.isValid() && rawFont.supportsCharacter(0x9898U);
}

bool applyFirstCapableFamily(const QStringList &families)
{
    for (const QString &family : families) {
        QFont font = QApplication::font();
        font.setFamily(family);
        if (!fontSupportsChinese(font)) {
            continue;
        }
        QApplication::setFont(font);
        return true;
    }
    return false;
}

QStringList preferredFamilies()
{
    return {
        QStringLiteral("Microsoft YaHei UI"),
        QStringLiteral("Microsoft YaHei"),
        QStringLiteral("Noto Sans SC"),
        QStringLiteral("Noto Sans CJK SC"),
        QStringLiteral("PingFang SC"),
    };
}

} // namespace

bool configureApplicationFont()
{
    static bool configured = false;
    static bool successful = false;
    if (configured) {
        return successful;
    }
    configured = true;

    if (applyFirstCapableFamily(preferredFamilies())) {
        successful = true;
        return true;
    }

    const QString windowsDirectory = qEnvironmentVariable("WINDIR");
    if (!windowsDirectory.isEmpty()) {
        const QDir fontsDirectory(QDir(windowsDirectory).filePath(QStringLiteral("Fonts")));
        const QStringList candidates{
            QStringLiteral("Noto Sans SC (TrueType).otf"),
            QStringLiteral("msyh.ttc"),
            QStringLiteral("simhei.ttf"),
        };
        for (const QString &fileName : candidates) {
            const QString path = fontsDirectory.filePath(fileName);
            if (!QFileInfo::exists(path)) {
                continue;
            }
            const int fontId = QFontDatabase::addApplicationFont(path);
            if (fontId < 0) {
                continue;
            }
            if (applyFirstCapableFamily(QFontDatabase::applicationFontFamilies(fontId))) {
                successful = true;
                return true;
            }
        }
    }

    successful = fontSupportsChinese(QApplication::font());
    return successful;
}

} // namespace quizapp::ui
