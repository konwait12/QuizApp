#pragma once

#include <QColor>
#include <QWidget>

namespace quizapp::ui {

class ThemePreview final : public QWidget {
    Q_OBJECT

public:
    explicit ThemePreview(QWidget *parent = nullptr);

    QString themeId() const;
    QString paletteId() const;
    void setThemeId(const QString &themeId);
    void setPaletteId(const QString &paletteId);
    void setPrimaryColor(const QColor &color);
    void setCornerRadius(int radius);
    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    QString themeId_ = QStringLiteral("system");
    QString paletteId_ = QStringLiteral("forest");
    QColor primaryColor_;
    int cornerRadius_ = 7;
};

} // namespace quizapp::ui
