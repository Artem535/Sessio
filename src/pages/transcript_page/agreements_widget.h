#pragma once

#include "notes_widget_frame.h"
#include <QJsonArray>

// «Agreements» card fed with prompter records.
class AgreementsWidget final : public NotesWidgetFrame {
  Q_OBJECT
public:
  explicit AgreementsWidget(QWidget *parent = nullptr);
  // Prompter records: {"id","text","active"}; inactive ones are skipped.
  void setRecords(const QJsonArray &records);
  bool hasContent() const;

private:
  QVBoxLayout *mRows;
  int mCount = 0;
};
