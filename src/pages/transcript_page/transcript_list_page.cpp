#include "transcript_list_page.h"
#include <QDateTime>
#include <QHeaderView>
#include <QLabel>
#include <QLocale>
#include <QMessageBox>
#include <QPointer>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

TranscriptListPage::TranscriptListPage(std::shared_ptr<pcm::database::Database> db, QWidget *parent)
    : QWidget(parent), mDb(std::move(db)), mTable(new QTableWidget(this)), mNotice(new QLabel(this)) {
  auto *layout = new QVBoxLayout(this);
  layout->addWidget(new QLabel(tr("Transcripts"), this));
  mTable->setObjectName("transcriptList");
  mTable->setColumnCount(3);
  mTable->setHorizontalHeaderLabels({tr("Date"), tr("Status"), tr("Clients")});
  mTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
  mTable->setSelectionBehavior(QAbstractItemView::SelectRows);
  mTable->setSelectionMode(QAbstractItemView::SingleSelection);
  mTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
  layout->addWidget(mTable, 1);
  mNotice->setWordWrap(true);
  layout->addWidget(mNotice);
  auto *actions = new QHBoxLayout;
  auto *open = new QPushButton(tr("Open transcript"), this);
  open->setObjectName("openTranscript");
  auto *remove = new QPushButton(tr("Delete transcript"), this);
  remove->setObjectName("deleteTranscript");
  actions->addWidget(open); actions->addWidget(remove); actions->addStretch();
  layout->addLayout(actions);
  mConfirm = [this](const QString &text) {
    return QMessageBox::question(this, tr("Delete transcript"), text,
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No) == QMessageBox::Yes;
  };
  auto openSelected = [this] {
    if (mTable->currentRow() >= 0)
      emit openTranscriptRequested(mTable->item(mTable->currentRow(), 0)->data(Qt::UserRole).toLongLong());
  };
  connect(open, &QPushButton::clicked, this, openSelected);
  connect(mTable, &QTableWidget::cellDoubleClicked, this, [openSelected](int, int) { openSelected(); });
  connect(remove, &QPushButton::clicked, this, [this] {
    if (mTable->currentRow() < 0) return;
    const auto id = mTable->item(mTable->currentRow(), 0)->data(Qt::UserRole).toLongLong();
    QPointer<TranscriptListPage> guard(this);
    try {
      auto row = mDb->get_transcript(id);
      if (!row || row->status == "recording") {
        mNotice->setText(tr("This transcript is still being recorded. Editing is unavailable."));
        return;
      }
      const auto confirm = mConfirm;
      if (!confirm(tr("Delete this transcript and all its phrases?")) || !guard) return;
      row = mDb->get_transcript(id);
      if (!row || row->status == "recording") return;
      if (!mDb->delete_transcript(id)) throw std::runtime_error("delete failed");
      reload(); emit transcriptsChanged();
    } catch (...) { if (guard) mNotice->setText(tr("Could not update the transcript. Please try again.")); }
  });
  reload();
}
void TranscriptListPage::setConfirmHook(std::function<bool(const QString &)> hook) {
  if (hook) mConfirm = std::move(hook);
}
void TranscriptListPage::showEvent(QShowEvent *event) {
  reload();
  QWidget::showEvent(event);
}
void TranscriptListPage::reload() {
  mTable->setRowCount(0); mNotice->clear();
  try {
    for (const auto &row : mDb->get_transcripts()) {
      const auto index = mTable->rowCount(); mTable->insertRow(index);
      auto *date = new QTableWidgetItem(QLocale().toString(QDateTime::fromMSecsSinceEpoch(row.created_at), QLocale::ShortFormat));
      date->setData(Qt::UserRole, QVariant::fromValue<qlonglong>(row.id));
      mTable->setItem(index, 0, date);
      mTable->setItem(index, 1, new QTableWidgetItem(row.status == "recording" ? tr("Recording") :
          row.status == "reviewed" ? tr("Reviewed") : tr("Draft")));
      QStringList names;
      for (const auto id : mDb->get_transcript_client_ids(row.id)) {
        const auto client = mDb->get_client(id);
        if (client) names << (QString::fromStdString(client->name.value_or("")) + " " +
                              QString::fromStdString(client->last_name.value_or(""))).trimmed();
      }
      mTable->setItem(index, 2, new QTableWidgetItem(names.isEmpty() ? tr("No clients attached") : names.join(", ")));
    }
    if (mTable->rowCount() == 0) mNotice->setText(tr("No transcripts yet."));
  } catch (...) { mNotice->setText(tr("Could not load transcripts. Please try again.")); }
}
