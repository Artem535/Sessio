#pragma once
#include "database.h"
#include <QDialog>

class QListWidget;
class TranscriptClientDialog final : public QDialog {
  Q_OBJECT
public:
  TranscriptClientDialog(std::shared_ptr<pcm::database::Database> db, int64_t transcriptId,
                         QWidget *parent = nullptr);
  std::vector<int64_t> selectedClientIds() const;
private:
  QListWidget *mClients;
};
