#pragma once

#include <QColor>
#include <QJsonObject>
#include <QPointF>
#include <QRectF>
#include <QString>
#include <QVector>

#include <optional>

namespace quizapp::handwriting {

struct PenSample {
    QPointF documentPosition;
    qreal pressure = 1.0;
    qint64 timestamp = 0;
};

struct StrokeSnapshot {
    QString id;
    QColor color;
    qreal baseThickness = 5.0;
    QVector<PenSample> samples;
    QRectF boundingBox;
};

class SpeedyNoteStrokeAdapter final {
public:
    StrokeSnapshot createStroke(
        const QVector<PenSample> &samples,
        const QColor &color,
        qreal baseThickness) const;

    bool hitTest(
        const StrokeSnapshot &stroke,
        const QPointF &documentPosition,
        qreal tolerance) const;

    QJsonObject serialize(const StrokeSnapshot &stroke) const;
    std::optional<StrokeSnapshot> deserialize(const QJsonObject &object) const;
};

} // namespace quizapp::handwriting

