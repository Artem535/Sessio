#include "transcript_list_page.h"
#include "phrase_card_paint.h"
#include <QDateTime>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLocale>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPointer>
#include <QPushButton>
#include <QScrollArea>
#include <QVBoxLayout>
#include <algorithm>

namespace {
class Card final : public QFrame {
public:
  explicit Card(std::function<void()> open, QWidget *parent) : QFrame(parent), mOpen(std::move(open)) {
    setCursor(Qt::PointingHandCursor);
  }
protected:
  void paintEvent(QPaintEvent *) override { paintPhraseCard(this, mHovered); }
  void enterEvent(QEnterEvent *) override { mHovered = true; update(); }
  void leaveEvent(QEvent *) override { mHovered = false; update(); }
  void mouseReleaseEvent(QMouseEvent *event) override {
    if (event->button() == Qt::LeftButton && rect().contains(event->position().toPoint()) && mOpen) mOpen();
  }
private:
  std::function<void()> mOpen;
  bool mHovered = false;
};

// A small rounded label. The tint is translucent so it reads on light and dark themes.
QLabel *chip(const QString &text, const char *name, QWidget *parent, const QString &rgba = QStringLiteral("rgba(128,128,128,45)")) {
  auto *label = new QLabel(text, parent);
  label->setObjectName(QString::fromLatin1(name));
  label->setTextFormat(Qt::PlainText);
  label->setStyleSheet(QStringLiteral("QLabel { background: %1; border-radius: 10px; padding: 2px 10px; }").arg(rgba));
  return label;
}

QString duration(int64_t milliseconds) {
  const auto seconds = std::max<int64_t>(0, milliseconds) / 1000;
  const auto minutes = seconds / 60;
  const QString tail = QStringLiteral("%1:%2").arg(minutes % 60, 2, 10, QChar('0')).arg(seconds % 60, 2, 10, QChar('0'));
  return seconds >= 3600 ? QStringLiteral("%1:%2").arg(seconds / 3600).arg(tail) : tail;
}
}

TranscriptListPage::TranscriptListPage(std::shared_ptr<pcm::database::Database> db, QWidget *parent)
    : QWidget(parent), mDb(std::move(db)), mSummary(new QLabel(this)), mNotice(new QLabel(this)),
      mEmpty(new QLabel(this)), mList(new QWidget), mRows(new QVBoxLayout(mList)) {
  auto *outer = new QHBoxLayout(this);
  outer->setContentsMargins(24, 24, 24, 24);
  auto *content = new QWidget(this);
  content->setMaximumWidth(940);
  outer->addStretch(); outer->addWidget(content, 1); outer->addStretch();
  auto *layout = new QVBoxLayout(content);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(10);
  auto *header = new QHBoxLayout;
  auto *title = new QLabel(tr("Transcripts"), content);
  title->setObjectName("transcriptsTitle");
  auto font = title->font(); font.setPointSize(22); font.setBold(true); title->setFont(font);
  header->addWidget(title); header->addStretch();
  mSummary->setObjectName("transcriptsSummary");
  header->addWidget(mSummary);
  layout->addLayout(header);
  mNotice->setObjectName("transcriptsNotice");
  mNotice->setWordWrap(true);
  layout->addWidget(mNotice);
  mEmpty->setObjectName("transcriptsEmpty");
  mEmpty->setText(tr("No transcripts yet.\nThey appear here after a call with transcription turned on."));
  mEmpty->setAlignment(Qt::AlignCenter);
  layout->addWidget(mEmpty);
  auto *scroll = new QScrollArea(content);
  scroll->setWidgetResizable(true);
  scroll->setFrameShape(QFrame::NoFrame);
  scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  mList->setObjectName("transcriptList");
  mList->setAutoFillBackground(false);
  mRows->setContentsMargins(0, 8, 12, 8); mRows->setSpacing(10);
  mRows->setAlignment(Qt::AlignTop);
  scroll->setWidget(mList);
  layout->addWidget(scroll, 1);
  mConfirm = [this](const QString &text) {
    return QMessageBox::question(this, tr("Delete transcript"), text,
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No) == QMessageBox::Yes;
  };
  reload();
}
void TranscriptListPage::setConfirmHook(std::function<bool(const QString &)> hook) {
  if (hook) mConfirm = std::move(hook);
}
void TranscriptListPage::showEvent(QShowEvent *event) {
  reload();
  QWidget::showEvent(event);
}

void TranscriptListPage::deleteTranscript(qint64 id) {
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
}

void TranscriptListPage::addCard(const DuckTranscript &row) {
  const auto id = static_cast<qint64>(row.id);
  const auto idText = QString::number(id);
  auto *card = new Card([this, id] { emit openTranscriptRequested(id); }, mList);
  card->setObjectName("transcriptCard_" + idText);
  card->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Maximum);
  auto *layout = new QHBoxLayout(card);
  layout->setContentsMargins(18, 14, 18, 14); layout->setSpacing(12);

  auto *info = new QVBoxLayout;
  info->setSpacing(8);
  auto *top = new QHBoxLayout;
  auto *date = new QLabel(QLocale().toString(QDateTime::fromMSecsSinceEpoch(row.created_at), QLocale::ShortFormat), card);
  date->setObjectName("transcriptDate");
  auto dateFont = date->font(); dateFont.setPointSize(13); dateFont.setBold(true); date->setFont(dateFont);
  top->addWidget(date);
  const bool recording = row.status == "recording", reviewed = row.status == "reviewed";
  top->addWidget(chip(recording ? tr("Recording") : reviewed ? tr("Reviewed") : tr("Draft"), "transcriptStatus", card,
                      recording ? QStringLiteral("rgba(220,70,70,70)")
                                : reviewed ? QStringLiteral("rgba(80,180,120,70)")
                                           : QStringLiteral("rgba(220,170,60,70)")));
  top->addStretch();
  info->addLayout(top);

  auto *clients = new QHBoxLayout;
  clients->setSpacing(6);
  int clientCount = 0;
  for (const auto clientId : mDb->get_transcript_client_ids(row.id)) {
    const auto client = mDb->get_client(clientId);
    if (!client) continue;
    const auto name = (QString::fromStdString(client->name.value_or("")) + " " +
                       QString::fromStdString(client->last_name.value_or(""))).trimmed();
    clients->addWidget(chip(name, "transcriptClient", card, QStringLiteral("rgba(117,82,163,70)")));
    ++clientCount;
  }
  if (clientCount == 0) clients->addWidget(chip(tr("No clients attached"), "transcriptNoClient", card));
  clients->addStretch();
  info->addLayout(clients);

  const auto phrases = mDb->get_transcript_phrases(row.id);
  int64_t endMs = 0;
  for (const auto &phrase : phrases) endMs = std::max<int64_t>(endMs, phrase.end_ms);
  auto *meta = new QHBoxLayout;
  meta->setSpacing(6);
  meta->addWidget(chip(tr("%n phrases", nullptr, static_cast<int>(phrases.size())), "transcriptPhrases", card));
  if (endMs > 0) meta->addWidget(chip(duration(endMs), "transcriptDuration", card));
  if (row.model_id && !row.model_id->empty())
    meta->addWidget(chip(QString::fromStdString(*row.model_id), "transcriptModel", card));
  meta->addStretch();
  info->addLayout(meta);
  layout->addLayout(info, 1);

  auto *actions = new QVBoxLayout;
  actions->setSpacing(6);
  auto *open = new QPushButton(tr("Open"), card);
  open->setObjectName("openTranscript_" + idText);
  auto *remove = new QPushButton(tr("Delete"), card);
  remove->setObjectName("deleteTranscript_" + idText);
  remove->setEnabled(!recording);
  actions->addStretch(); actions->addWidget(open); actions->addWidget(remove); actions->addStretch();
  layout->addLayout(actions);
  connect(open, &QPushButton::clicked, this, [this, id] { emit openTranscriptRequested(id); });
  connect(remove, &QPushButton::clicked, this, [this, id] { deleteTranscript(id); });
  mRows->addWidget(card);
}

void TranscriptListPage::reload() {
  while (auto *item = mRows->takeAt(0)) { delete item->widget(); delete item; }
  mNotice->clear(); mCount = 0;
  try {
    for (const auto &row : mDb->get_transcripts()) { addCard(row); ++mCount; }
  } catch (...) {
    while (auto *item = mRows->takeAt(0)) { delete item->widget(); delete item; }
    mCount = 0;
    mNotice->setText(tr("Could not load transcripts. Please try again."));
  }
  mEmpty->setVisible(mCount == 0 && mNotice->text().isEmpty());
  mSummary->setText(tr("%n transcripts", nullptr, mCount));
  mSummary->setVisible(mCount > 0);
}
