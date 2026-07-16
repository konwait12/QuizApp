#include "handwriting/SpeedyNoteStrokeAdapter.h"

#include "VectorStroke.h"

#include <QtGlobal>

#include <algorithm>
#include <cmath>

namespace quizapp::handwriting {
namespace {

bool isFinitePoint(const QPointF &point)
{
    return std::isfinite(point.x()) && std::isfinite(point.y());
}

VectorStroke toUpstream(const StrokeSnapshot &snapshot)
{
    VectorStroke stroke;
    stroke.id = snapshot.id.isEmpty()
        ? QUuid::createUuid().toString(QUuid::WithoutBraces)
        : snapshot.id;
    stroke.color = snapshot.color;
    stroke.baseThickness = snapshot.baseThickness;
    stroke.points.reserve(snapshot.samples.size());
    for (const PenSample &sample : snapshot.samples) {
        if (!isFinitePoint(sample.documentPosition)) {
            continue;
        }
        StrokePoint point;
        point.pos = sample.documentPosition;
        point.pressure = std::clamp(sample.pressure, qreal(0.0), qreal(1.0));
        point.timestamp = sample.timestamp;
        stroke.points.append(point);
    }
    stroke.updateBoundingBox();
    return stroke;
}

StrokeSnapshot fromUpstream(const VectorStroke &stroke)
{
    StrokeSnapshot snapshot;
    snapshot.id = stroke.id;
    snapshot.color = stroke.color;
    snapshot.baseThickness = stroke.baseThickness;
    snapshot.boundingBox = stroke.boundingBox;
    snapshot.samples.reserve(stroke.points.size());
    for (const StrokePoint &point : stroke.points) {
        snapshot.samples.append(PenSample{
            point.pos,
            std::clamp(point.pressure, qreal(0.0), qreal(1.0)),
            point.timestamp,
        });
    }
    return snapshot;
}

} // namespace

StrokeSnapshot SpeedyNoteStrokeAdapter::createStroke(
    const QVector<PenSample> &samples,
    const QColor &color,
    qreal baseThickness) const
{
    StrokeSnapshot snapshot;
    snapshot.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    snapshot.color = color.isValid() ? color : QColor(Qt::black);
    snapshot.baseThickness = std::isfinite(baseThickness) && baseThickness > 0.0
        ? baseThickness
        : 5.0;
    snapshot.samples = samples;
    return fromUpstream(toUpstream(snapshot));
}

bool SpeedyNoteStrokeAdapter::hitTest(
    const StrokeSnapshot &stroke,
    const QPointF &documentPosition,
    qreal tolerance) const
{
    if (!isFinitePoint(documentPosition) || !std::isfinite(tolerance) || tolerance < 0.0) {
        return false;
    }
    return toUpstream(stroke).containsPoint(documentPosition, tolerance);
}

QJsonObject SpeedyNoteStrokeAdapter::serialize(const StrokeSnapshot &stroke) const
{
    return toUpstream(stroke).toJson();
}

std::optional<StrokeSnapshot> SpeedyNoteStrokeAdapter::deserialize(
    const QJsonObject &object) const
{
    if (!object.value(QStringLiteral("points")).isArray()
        || !object.value(QStringLiteral("color")).isString()) {
        return std::nullopt;
    }
    const qreal thickness = object.value(QStringLiteral("thickness")).toDouble(-1.0);
    if (!std::isfinite(thickness) || thickness <= 0.0) {
        return std::nullopt;
    }
    const VectorStroke stroke = VectorStroke::fromJson(object);
    if (!stroke.color.isValid()) {
        return std::nullopt;
    }
    for (const StrokePoint &point : stroke.points) {
        if (!isFinitePoint(point.pos) || !std::isfinite(point.pressure)) {
            return std::nullopt;
        }
    }
    return fromUpstream(stroke);
}

} // namespace quizapp::handwriting

