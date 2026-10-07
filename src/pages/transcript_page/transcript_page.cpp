#include "transcript_page.h"
#include <QComboBox>
#include <QDateTime>
#include <QLabel>
#include <QLocale>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPointer>
#include <QPushButton>
#include <QScrollArea>
#include <QVBoxLayout>
#include <algorithm>

namespace {
QLabel *label(const QString &text, const char *name, QWidget *parent) {
  auto *result = new QLabel(text, parent);
  result->setObjectName(QString::fromLatin1(name));
  result->setTextFormat(Qt::PlainText);
  result->setWordWrap(true);
  return result;
}
QPushButton *button(const QString &text, const QString &name, QWidget *parent) {
  auto *result = new QPushButton(text, parent);
  result->setObjectName(name);
  return result;
}
QString timestamp(int64_t milliseconds) {
  const auto seconds = milliseconds / 1000;
  if (seconds >= 3600)
    return QStringLiteral("%1:%2:%3").arg(seconds / 3600)
        .arg((seconds / 60) % 60, 2, 10, QLatin1Char('0'))
        .arg(seconds % 60, 2, 10, QLatin1Char('0'));
  return QStringLiteral("%1:%2").arg(seconds / 60, 2, 10, QLatin1Char('0'))
      .arg(seconds % 60, 2, 10, QLatin1Char('0'));
}
}

TranscriptPage::TranscriptPage(std::shared_ptr<pcm::database::Database> db,
                               int64_t eventId, const QString &title, QWidget *parent)
    : QWidget(parent), mDb(std::move(db)) {
  auto *layout = new QVBoxLayout(this);
  auto *back = button(tr("Back to event"), "backToEvent", this);
  layout->addWidget(back, 0, Qt::AlignLeft);
  connect(back, &QPushButton::clicked, this, &TranscriptPage::backRequested);
  mSelector = new QComboBox(this);
  mSelector->setObjectName("transcriptSelector");
  mSelector->setAccessibleName(tr("Transcript"));
  layout->addWidget(mSelector);
  mTitle = label({}, "transcriptTitle", this);
  auto font = mTitle->font(); font.setPointSize(18); font.setBold(true); mTitle->setFont(font);
  layout->addWidget(mTitle);
  mClient = label({}, "transcriptClient", this); layout->addWidget(mClient);
  mMetadata = label({}, "transcriptMetadata", this); layout->addWidget(mMetadata);
  mStatus = label({}, "transcriptStatus", this); layout->addWidget(mStatus);
  mNotice = label({}, "transcriptNotice", this); layout->addWidget(mNotice);
  auto *actions = new QHBoxLayout;
  mReview = button(tr("Mark reviewed"), "markReviewed", this);
  mDelete = button(tr("Delete transcript"), "deleteTranscript", this);
  actions->addWidget(mReview); actions->addWidget(mDelete); actions->addStretch();
  layout->addLayout(actions);
  auto *scroll = new QScrollArea(this); scroll->setWidgetResizable(true);
  auto *rows = new QWidget(scroll); mRows = new QVBoxLayout(rows);
  mRows->setAlignment(Qt::AlignTop); scroll->setWidget(rows); layout->addWidget(scroll, 1);
  mConfirm = [this](const QString &message) {
    return QMessageBox::question(this, tr("Delete transcript"), message,
                                 QMessageBox::Yes | QMessageBox::No, QMessageBox::No) == QMessageBox::Yes;
  };
  connect(mSelector, &QComboBox::currentIndexChanged, this, &TranscriptPage::showTranscript);
  connect(mReview, &QPushButton::clicked, this, [this] {
    const auto id = mSelector->currentData().toLongLong();
    try {
      if (!mayMutate(id)) return;
      if (!mDb->set_transcript_status(id, "reviewed")) { reportFailure(); return; }
      showTranscript(); emit transcriptsChanged();
    } catch (...) { reportFailure(); }
  });
  connect(mDelete, &QPushButton::clicked, this, [this] {
    const auto id = mSelector->currentData().toLongLong();
    QPointer<TranscriptPage> guard(this);
    try {
      if (!mayMutate(id)) return;
      // Copy the hook: a nested modal loop can destroy its owning page.
      const auto confirm = mConfirm;
      const bool accepted = confirm(tr("Delete this transcript and all its phrases?"));
      if (!guard || !accepted) return;
      // The modal confirmation may process a new recording or page navigation.
      if (!mayMutate(id)) return;
      if (!mDb->delete_transcript(id)) { reportFailure(); return; }
      reload(mEventId, mTitle->text()); emit transcriptsChanged();
    } catch (...) { if (guard) guard->reportFailure(); }
  });
  reload(eventId, title);
}

void TranscriptPage::setConfirmHook(std::function<bool(const QString &)> hook) {
  if (hook) mConfirm = std::move(hook);
}
int TranscriptPage::transcriptCount() const { return mSelector->count(); }
void TranscriptPage::reportFailure() {
  mNotice->setText(tr("Could not update the transcript. Please try again."));
}
bool TranscriptPage::mayMutate(int64_t transcriptId) {
  const auto row = mDb->get_transcript(transcriptId);
  if (!row || row->event_id != mEventId) return false;
  if (row->status == "recording") {
    mNotice->setText(tr("This transcript is still being recorded. Editing is unavailable."));
    return false;
  }
  return true;
}
void TranscriptPage::reload(int64_t eventId, const QString &title) {
  if (mEditing) {
    mNotice->setText(tr("Save or cancel the current edit first."));
    return;
  }
  mEventId = eventId; mTitle->setText(title);
  mClient->setText(tr("Client unavailable"));
  try {
    const auto client = mDb->get_client_by_event(eventId);
    const auto name = QString::fromStdString(client.name.value_or("")) + " " +
                      QString::fromStdString(client.last_name.value_or(""));
    if (client.id > 0 && !name.trimmed().isEmpty()) mClient->setText(tr("Client: %1").arg(name.trimmed()));
  } catch (...) { /* Missing client is valid for an unlinked event. */ }
  mSelector->blockSignals(true); mSelector->clear();
  try {
    auto transcripts = mDb->get_transcripts_for_event(eventId);
    std::sort(transcripts.begin(), transcripts.end(), [](const auto &left, const auto &right) {
      return left.created_at != right.created_at ? left.created_at > right.created_at : left.id > right.id;
    });
    for (const auto &row : transcripts)
      mSelector->addItem(QLocale().toString(QDateTime::fromMSecsSinceEpoch(row.created_at), QLocale::ShortFormat),
                         QVariant::fromValue<qlonglong>(row.id));
  } catch (...) {
    mSelector->blockSignals(false);
    showTranscript();
    reportFailure();
    return;
  }
  mSelector->blockSignals(false); mSelector->setVisible(mSelector->count() > 1);
  showTranscript();
}
void TranscriptPage::showTranscript() {
  mEditing = false;
  mSelector->setEnabled(true);
  findChild<QPushButton *>("backToEvent")->setEnabled(true);
  while (auto *item = mRows->takeAt(0)) { delete item->widget(); delete item; }
  mNotice->clear(); mMetadata->clear(); mStatus->clear();
  mReview->hide(); mDelete->hide();
  if (!mSelector->count()) { mNotice->setText(tr("No transcripts for this event.")); return; }
  try {
    const auto row = mDb->get_transcript(mSelector->currentData().toLongLong());
    if (!row) { mNotice->setText(tr("Transcript unavailable.")); return; }
    const auto phrases = mDb->get_transcript_phrases(row->id);
    const bool recording = row->status == "recording";
    mStatus->setText(recording ? tr("Recording") : row->status == "reviewed" ? tr("Reviewed") : tr("Draft"));
    mMetadata->setText(tr("%n phrases", nullptr, static_cast<int>(phrases.size())) +
                      tr(" · Model: %1").arg(QString::fromStdString(row->model_id.value_or(""))));
    mReview->setVisible(row->status != "reviewed"); mReview->setEnabled(!recording);
    mDelete->show(); mDelete->setEnabled(!recording);
    if (recording) mNotice->setText(tr("This transcript is still being recorded. Editing is unavailable."));
    for (const auto &phrase : phrases) {
      auto *widget = new QWidget(this); auto *layout = new QVBoxLayout(widget);
      const auto speaker = phrase.speaker_name ? QString::fromStdString(*phrase.speaker_name) :
          phrase.track_role == "practitioner" ? tr("Practitioner") : tr("Participant");
      layout->addWidget(label(timestamp(phrase.start_ms) + " – " + timestamp(phrase.end_ms) + " · " + speaker,
                              "phraseSpeaker", widget));
      auto *text = label(QString::fromStdString(phrase.text), "phraseText", widget); layout->addWidget(text);
      if (phrase.edited) layout->addWidget(label(tr("Edited"), "phraseEdited", widget));
      auto *actions = new QHBoxLayout;
      auto *edit = button(tr("Edit"), "editPhrase_" + QString::number(phrase.id), widget);
      auto *remove = button(tr("Delete"), "deletePhrase_" + QString::number(phrase.id), widget);
      actions->addWidget(edit); actions->addWidget(remove); actions->addStretch(); layout->addLayout(actions);
      edit->setEnabled(!recording); remove->setEnabled(!recording);
      connect(remove, &QPushButton::clicked, this, [this, phrase] {
        try {
          if (!mayMutate(phrase.transcript_id)) return;
          if (!mDb->delete_transcript_phrase(phrase.id)) { reportFailure(); return; }
          showTranscript(); emit transcriptsChanged();
        } catch (...) { reportFailure(); }
      });
      connect(edit, &QPushButton::clicked, this, [this, phrase, widget, layout, text, edit] {
        if (mEditing) return;
        try { if (!mayMutate(phrase.transcript_id)) return; }
        catch (...) { reportFailure(); return; }
        mEditing = true;
        mSelector->setEnabled(false);
        for (auto *action : findChildren<QPushButton *>()) action->setEnabled(false);
        auto *editor = new QPlainTextEdit(text->text(), widget); editor->setObjectName("phraseEditor");
        auto *save = button(tr("Save"), "savePhrase", widget);
        auto *cancel = button(tr("Cancel"), "cancelPhrase", widget);
        text->hide(); edit->hide(); layout->addWidget(editor); layout->addWidget(save); layout->addWidget(cancel);
        connect(cancel, &QPushButton::clicked, this, &TranscriptPage::showTranscript);
        connect(save, &QPushButton::clicked, this, [this, phrase, editor] {
          const auto value = editor->toPlainText().trimmed();
          if (value.isEmpty()) { mNotice->setText(tr("Phrase text cannot be empty.")); return; }
          try {
            if (!mayMutate(phrase.transcript_id)) return;
            if (!mDb->update_transcript_phrase_text(phrase.id, value.toStdString())) { reportFailure(); return; }
            showTranscript(); emit transcriptsChanged();
          } catch (...) { reportFailure(); }
        });
      });
      mRows->addWidget(widget);
    }
  } catch (...) { reportFailure(); }
}
