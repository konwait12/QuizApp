#pragma once

#include <QWidget>

namespace quizapp::ui {

class ThemePreview final : public QWidget {
    Q_OBJECT

public:
    explicit ThemePreview(QWidget *parent = nullptr);

    QString themeId() const;
    void setThemeId(const QString &themeId);
    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    QString themeId_ = QStringLiteral("system");
};

} // namespace quizapp::ui
