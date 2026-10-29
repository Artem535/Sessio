#pragma once

#include <QJsonObject>
#include <QPushButton>
#include <QString>
#include <functional>

class CallNotesController;

// Source link for a note or key point: «0:04–0:05 · 1 phrase» (compact: «0:04»).
// Returns nullptr when the item has no valid evidence. Clicking calls onClick(phraseId).
QPushButton *makeSourceLink(CallNotesController *controller, const QJsonObject &item, bool compact,
                            QWidget *parent, std::function<void(qint64)> onClick);
// «whole meeting · N phrases» / «through 12:30 · N phrases» caption for the summary.
QString summaryCoverage(CallNotesController *controller);
