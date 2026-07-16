#include "ui/StudyTrendChart.h"

#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QToolTip>
#include <QVariantAnimation>

#include <algorithm>

namespace quizapp::ui {

StudyTrendChart::StudyTrendChart(QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("studyTrendChart"));
    setToolTip(QStringLiteral("点击数据点查看当天学习时长"));
    setAccessibleName(QStringLiteral("学习时长折线图"));
    setMinimumHeight(220);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    animation_ = new QVariantAnimation(this);
    animation_->setDuration(520);
    animation_->setStartValue(0.0);
    animation_->setEndValue(1.0);
    animation_->setEasingCurve(QEasingCurve::OutCubic);
    connect(animation_, &QVariantAnimation::valueChanged, this, [this](const QVariant &value) {
        progress_ = value.toReal();
        update();
    });
}

void StudyTrendChart::setData(
    const QVector<domain::DailyStudyTotal> &totals,
    bool animate)
{
    totals_ = totals;
    selectedIndex_ = -1;
    QToolTip::hideText();
    animation_->stop();
    if (animate && isVisible() && !totals_.isEmpty()) {
        progress_ = 0.0;
        animation_->start();
    } else {
        progress_ = 1.0;
        update();
    }
}

QColor StudyTrendChart::lineColor() const { return lineColor_; }
QColor StudyTrendChart::gridColor() const { return gridColor_; }
QColor StudyTrendChart::textColor() const { return textColor_; }
void StudyTrendChart::setLineColor(const QColor &color) { lineColor_ = color; update(); }
void StudyTrendChart::setGridColor(const QColor &color) { gridColor_ = color; update(); }
void StudyTrendChart::setTextColor(const QColor &color) { textColor_ = color; update(); }

void StudyTrendChart::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event)
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    const QRectF plot = rect().adjusted(44, 18, -16, -34);
    if (plot.width() <= 0 || plot.height() <= 0) {
        return;
    }
    painter.setPen(QPen(gridColor_, 1));
    for (int row = 0; row <= 3; ++row) {
        const qreal y = plot.top() + plot.height() * row / 3.0;
        painter.drawLine(QPointF(plot.left(), y), QPointF(plot.right(), y));
    }
    if (totals_.isEmpty()) {
        painter.setPen(textColor_);
        painter.drawText(plot, Qt::AlignCenter, QStringLiteral("暂无学习记录"));
        return;
    }

    const int maximum = std::max(60, std::max_element(
        totals_.cbegin(), totals_.cend(), [](const auto &left, const auto &right) {
            return left.durationSeconds < right.durationSeconds;
        })->durationSeconds);
    const QVector<QPointF> points = pointPositions();
    QPainterPath path;
    const int visiblePointCount = qBound(
        1, static_cast<int>(qCeil(points.size() * progress_)), points.size());
    path.moveTo(points.constFirst());
    for (int index = 1; index < visiblePointCount; ++index) {
        path.lineTo(points.at(index));
    }
    painter.setPen(QPen(lineColor_, 2.4, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter.drawPath(path);
    painter.setBrush(lineColor_);
    for (int index = 0; index < visiblePointCount; ++index) {
        const qreal radius = index == selectedIndex_ ? 5.0 : 3.5;
        painter.drawEllipse(points.at(index), radius, radius);
    }

    painter.setPen(textColor_);
    const QString maximumLabel = maximum >= 3600
        ? QStringLiteral("%1h").arg(maximum / 3600.0, 0, 'f', 1)
        : QStringLiteral("%1m").arg(maximum / 60);
    painter.drawText(QRectF(0, plot.top() - 8, 38, 20), Qt::AlignRight, maximumLabel);
    painter.drawText(QRectF(0, plot.bottom() - 10, 38, 20), Qt::AlignRight, QStringLiteral("0"));
    const int labelStep = totals_.size() > 14 ? 7 : qMax(1, totals_.size() - 1);
    for (int index = 0; index < totals_.size(); index += labelStep) {
        const QPointF point = points.at(index);
        painter.drawText(
            QRectF(point.x() - 30, plot.bottom() + 8, 60, 20),
            Qt::AlignHCenter,
            totals_.at(index).date.toString(QStringLiteral("M/d")));
    }
    if (totals_.size() > 1 && (totals_.size() - 1) % labelStep != 0) {
        painter.drawText(
            QRectF(points.constLast().x() - 30, plot.bottom() + 8, 60, 20),
            Qt::AlignHCenter,
            totals_.constLast().date.toString(QStringLiteral("M/d")));
    }
}

void StudyTrendChart::mousePressEvent(QMouseEvent *event)
{
    const QVector<QPointF> points = pointPositions();
    int nearest = -1;
    qreal nearestDistance = 18.0;
    for (int index = 0; index < points.size(); ++index) {
        const qreal distance = QLineF(event->position(), points.at(index)).length();
        if (distance < nearestDistance) {
            nearest = index;
            nearestDistance = distance;
        }
    }
    selectedIndex_ = nearest;
    if (nearest >= 0) {
        const auto &total = totals_.at(nearest);
        QToolTip::showText(
            event->globalPosition().toPoint(),
            QStringLiteral("%1\n%2")
                .arg(total.date.toString(QStringLiteral("yyyy年M月d日")),
                     durationText(total.durationSeconds)),
            this);
    } else {
        QToolTip::hideText();
    }
    update();
}

QVector<QPointF> StudyTrendChart::pointPositions() const
{
    QVector<QPointF> points;
    if (totals_.isEmpty()) {
        return points;
    }
    const QRectF plot = rect().adjusted(44, 18, -16, -34);
    const int maximum = std::max(60, std::max_element(
        totals_.cbegin(), totals_.cend(), [](const auto &left, const auto &right) {
            return left.durationSeconds < right.durationSeconds;
        })->durationSeconds);
    points.reserve(totals_.size());
    for (int index = 0; index < totals_.size(); ++index) {
        const qreal x = totals_.size() == 1
            ? plot.center().x()
            : plot.left() + plot.width() * index / (totals_.size() - 1.0);
        const qreal ratio = totals_.at(index).durationSeconds / static_cast<qreal>(maximum);
        points.append(QPointF(x, plot.bottom() - plot.height() * ratio));
    }
    return points;
}

QString StudyTrendChart::durationText(int seconds) const
{
    const int hours = seconds / 3600;
    const int minutes = (seconds % 3600) / 60;
    const int remainder = seconds % 60;
    if (hours > 0) {
        return QStringLiteral("%1小时%2分钟").arg(hours).arg(minutes);
    }
    if (minutes > 0) {
        return QStringLiteral("%1分钟%2秒").arg(minutes).arg(remainder);
    }
    return QStringLiteral("%1秒").arg(remainder);
}

} // namespace quizapp::ui
