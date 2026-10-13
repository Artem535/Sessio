#include "transcript_client_dialog.h"
#include <QDialogButtonBox>
#include <QLabel>
#include <QListWidget>
#include <QVBoxLayout>
#include <algorithm>

TranscriptClientDialog::TranscriptClientDialog(std::shared_ptr<pcm::database::Database> db,
                                               int64_t transcriptId, QWidget *parent)
    : QDialog(parent), mClients(new QListWidget(this)) {
  setWindowTitle(tr("Attach clients"));
  resize(420, 400);
  auto *layout = new QVBoxLayout(this);
  auto *description = new QLabel(tr("Select existing clients. Guest names are never matched automatically."), this);
  description->setWordWrap(true);
  layout->addWidget(description);
  mClients->setObjectName("transcriptClientSelection");
  layout->addWidget(mClients);
  const auto selected = db->get_transcript_client_ids(transcriptId);
  for (const auto &client : db->get_clients()) {
    const auto name = QString::fromStdString(client->name.value_or("")) + " " +
                      QString::fromStdString(client->last_name.value_or(""));
    auto *item = new QListWidgetItem(name.trimmed(), mClients);
    item->setData(Qt::UserRole, QVariant::fromValue<qlonglong>(client->id));
    item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
    item->setCheckState(std::find(selected.begin(), selected.end(), client->id) != selected.end()
                            ? Qt::Checked : Qt::Unchecked);
  }
  auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
  layout->addWidget(buttons);
  connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
  connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
}
std::vector<int64_t> TranscriptClientDialog::selectedClientIds() const {
  std::vector<int64_t> ids;
  for (int i = 0; i < mClients->count(); ++i)
    if (mClients->item(i)->checkState() == Qt::Checked)
      ids.push_back(mClients->item(i)->data(Qt::UserRole).toLongLong());
  return ids;
}
