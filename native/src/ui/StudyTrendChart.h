#pragma once

#include "domain/StudyEvent.h"

#include <QColor>
#include <QVector>
#include <QWidget>

class QMouseEvent;
class QPaintEvent;
class QVariantAnimation;

namespace quizapp::ui {

class StudyTrendChart final : public QWidget {
    Q_OBJECT
    Q_PROPERTY(QColor lineColor READ lineColor WRITE setLineColor)
    Q_PROPERTY(QColor gridColor READ gridColor WRITE setGridColor)
    Q_PROPERTY(QColor textColor READ textColor WRITE setTextColor)

public:
    explicit StudyTrendChart(QWidget *parent = nullptr);

    void setData(const QVector<domain::DailyStudyTotal> &totals, bool animate = true);
    QColor lineColor() const;
    QColor gridColor() const;
    QColor textColor() const;
    void setLineColor(const QColor &color);
    void setGridColor(const QColor &color);
    void setTextColor(const QColor &color);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;

private:
    QVector<QPointF> pointPositions() const;
    QString durationText(int seconds) const;

    QVector<domain::DailyStudyTotal> totals_;
    QVariantAnimation *animation_ = nullptr;
    qreal progress_ = 1.0;
    int selectedIndex_ = -1;
    QColor lineColor_ = QColor(QStringLiteral("#1d9367"));
    QColor gridColor_ = QColor(QStringLiteral("#d9e1d8"));
    QColor textColor_ = QColor(QStringLiteral("#657168"));
};

} // namespace quizapp::ui

