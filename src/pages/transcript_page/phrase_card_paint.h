#pragma once

#include <QPainter>
#include <QPalette>
#include <QWidget>

// Rounded card background shared by the transcript page and the in-call panel.
inline void paintPhraseCard(QWidget *card, bool selected) {
  QPainter painter(card);
  painter.setRenderHint(QPainter::Antialiasing);
  const auto &palette = card->palette();
  auto background = palette.color(QPalette::Window);
  background = background.lightness() < 128 ? background.lighter(115) : background.darker(103);
  auto border = palette.color(selected ? QPalette::Highlight : QPalette::Mid);
  if (!selected) border.setAlpha(100);
  painter.setPen(QPen(border, 1));
  painter.setBrush(background);
  painter.drawRoundedRect(QRectF(card->rect()).adjusted(0.5, 0.5, -0.5, -0.5), 8, 8);
}
