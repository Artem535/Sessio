#include "transcript_page.h"

#include <QComboBox>
#include <QDateTime>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QPointer>
#include <QQmlContext>
#include <QQuickWidget>
#include <QScrollArea>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QMenu>
#include <QToolButton>
#include <QQuickItem>

void TranscriptPage::enableQuickReader() {
  if (mQuickReader) return;
  Q_INIT_RESOURCE(notes_workspace_qml);
  mQuickReader = new QQuickWidget(mTranscriptHost);
  mQuickReader->setObjectName("transcriptQuickReader");
  mQuickReader->setResizeMode(QQuickWidget::SizeRootObjectToView);
  mQuickReader->setClearColor(palette().color(QPalette::Base));
  mQuickReader->rootContext()->setContextProperty("readerBridge", this);
  mTranscriptHost->layout()->addWidget(mQuickReader);
  mQuickReader->setSource(QUrl("qrc:/sessio/notes/TranscriptReader.qml"));
  auto *returnToReading = new QPushButton(tr("Read transcript"), this);
  returnToReading->setObjectName("returnToQuickReader");
  auto *content = qobject_cast<QVBoxLayout *>(layout())->itemAt(0)->widget();
  content->layout()->addWidget(returnToReading);
  connect(returnToReading, &QPushButton::clicked, this, [this] { setReaderMode(true); });
  connect(mNotice, &QLabel::linkActivated, this, [this] { emit readerChanged(); });
  setReaderMode(true);
}

void TranscriptPage::setReaderMode(bool reading) {
  if (!mQuickReader || (reading && mEditing)) return;
  auto *outer = qobject_cast<QVBoxLayout *>(layout());
  outer->setContentsMargins(reading ? 0 : 16, reading ? 0 : 16, reading ? 0 : 16, reading ? 0 : 16);
  outer->itemAt(0)->widget()->setVisible(!reading);
  mSearch->setVisible(!reading);
  mTranscriptHost->findChild<QScrollArea *>()->setVisible(!reading);
  mQuickReader->setVisible(reading);
  if (!reading) setNotesVisible(false);
}

QWidget *TranscriptPage::headerControls() {
  if (mExternalHeader) return mExternalHeader;
  enableQuickReader();
  mExternalHeader = new QWidget(this);
  mExternalHeader->setObjectName("transcriptReaderHeaderControls");
  auto *row = new QHBoxLayout(mExternalHeader);
  row->setContentsMargins(0, 0, 0, 0);
  row->setSpacing(12);
  auto *back = new QPushButton(tr("Back to transcripts"), mExternalHeader);
  row->addWidget(back);
  connect(back, &QPushButton::clicked, this, [this] { readerAction("back"); });
  auto *title = new QLabel(tr("Transcript"), mExternalHeader);
  auto heading = title->font(); heading.setPointSize(16); heading.setBold(true); title->setFont(heading);
  row->addWidget(title);
  row->addStretch();
  auto *history = new QPushButton(tr("History"), mExternalHeader);
  history->setObjectName("readerHeaderHistory");
  row->addWidget(history);
  connect(history, &QPushButton::clicked, this, [this] {
    if (auto *root = mQuickReader->rootObject()) root->setProperty("historyVisible", !root->property("historyVisible").toBool());
  });
  auto *notes = new QPushButton(tr("Call notes"), mExternalHeader);
  notes->setCheckable(true);
  row->addWidget(notes);
  connect(notes, &QPushButton::clicked, this, [this] { readerAction("notes"); });
  auto *more = new QToolButton(mExternalHeader);
  more->setText(QStringLiteral("⋯"));
  more->setAccessibleName(tr("More actions"));
  more->setPopupMode(QToolButton::InstantPopup);
  auto *menu = new QMenu(more);
  const auto action = [this, menu](const QString &text, const QString &id) {
    auto *item = menu->addAction(text);
    connect(item, &QAction::triggered, this, [this, id] { readerAction(id); });
    return item;
  };
  auto *exportAction = action(tr("Export text"), "export");
  auto *attach = action(tr("Attach clients"), "attach");
  auto *review = action(tr("Mark reviewed"), "review");
  auto *edit = action(tr("Edit transcript"), "advanced");
  auto *remove = action(tr("Delete transcript"), "delete");
  more->setMenu(menu); row->addWidget(more);
  const auto sync = [this, back, notes, exportAction, attach, review, edit, remove] {
    back->setText(findChild<QPushButton *>("backToEvent")->text());
    back->setEnabled(!mEditing);
    notes->setEnabled(mNotesView != nullptr && !mEditing);
    notes->setChecked(mNotesVisible);
    exportAction->setEnabled(mSelector->count() > 0);
    attach->setEnabled(mAttach->isEnabled());
    review->setEnabled(mReview->isEnabled() && !mReview->isHidden());
    edit->setEnabled(mDelete->isEnabled() && !mDelete->isHidden());
    remove->setEnabled(edit->isEnabled());
  };
  connect(this, &TranscriptPage::readerChanged, mExternalHeader, sync);
  sync();
  if (mQuickReader->rootObject()) mQuickReader->rootObject()->setProperty("externalHeader", true);
  return mExternalHeader;
}

QVariantMap TranscriptPage::reader() const {
  return {{"title", mTitle->text()}, {"client", mClient->text()}, {"status", mStatus->text()},
    {"metadata", mMetadata->text()}, {"notice", mNotice->text()},
    {"id", mSelector->currentData().toString()}, {"canReview", mReview->isEnabled() && !mReview->isHidden()},
    {"canDelete", mDelete->isEnabled() && !mDelete->isHidden()}, {"canAttach", mAttach->isEnabled()},
    {"hasNotes", mNotesView != nullptr}, {"notesVisible", mNotesVisible}};
}
QVariantList TranscriptPage::readerHistory() const {
  QVariantList result;
  try {
    for (const auto &row : mDb->get_transcripts()) {
      QStringList names;
      for (const auto id : mDb->get_transcript_client_ids(row.id))
        if (const auto client = mDb->get_client(id))
          names.append(QString::fromStdString(client->name.value_or("") + " " + client->last_name.value_or("")).trimmed());
      result.append(QVariantMap{{"id", QString::number(row.id)}, {"title", names.isEmpty() ? tr("No clients attached") : names.join(", ")},
        {"date", QDateTime::fromMSecsSinceEpoch(row.created_at).toLocalTime().toString("dd.MM.yyyy · HH:mm")}});
    }
  } catch (...) {}
  return result;
}
QVariantList TranscriptPage::readerPhrases() const {
  QVariantList result;
  if (mSelector->currentIndex() < 0) return result;
  try {
    for (const auto &phrase : mDb->get_transcript_phrases(mSelector->currentData().toLongLong())) {
      const auto seconds = phrase.start_ms / 1000;
      const auto time = seconds >= 3600 ? QString("%1:%2:%3").arg(seconds / 3600).arg(seconds / 60 % 60, 2, 10, QChar('0')).arg(seconds % 60, 2, 10, QChar('0'))
        : QString("%1:%2").arg(seconds / 60).arg(seconds % 60, 2, 10, QChar('0'));
      result.append(QVariantMap{{"id", QString::number(phrase.id)}, {"time", time},
        {"speaker", phrase.speaker_name ? QString::fromStdString(*phrase.speaker_name) : phrase.track_role == "practitioner" ? tr("Practitioner") : tr("Participant")},
        {"practitioner", phrase.track_role == "practitioner"}, {"text", QString::fromStdString(phrase.text)}});
    }
  } catch (...) {}
  return result;
}
void TranscriptPage::selectReaderTranscript(const QString &id) {
  if (mEditing) return;
  bool ok = false;
  const auto value = id.toLongLong(&ok);
  try {
    if (ok && mDb->get_transcript(value)) { emit readerTranscriptSelected(); reloadTranscript(value, tr("Transcript")); }
  } catch (...) { reportFailure(); emit readerChanged(); }
}
void TranscriptPage::readerAction(const QString &action) {
  if (mEditing) return;
  if (action == "back") { emit backRequested(); return; }
  if (action == "advanced") { setReaderMode(false); return; }
  if (action == "notes") {
    mSourceToggle->setChecked(true);
    setNotesVisible(!mNotesVisible);
    emit readerChanged();
    return;
  }
  const auto name = action == "review" ? "markReviewed" : action == "delete" ? "deleteTranscript" :
    action == "attach" ? "transcriptAttachClients" : action == "export" ? "transcriptExport" : "";
  if (!*name) return;
  QPointer<TranscriptPage> guard(this);
  if (auto *button = findChild<QPushButton *>(name); button && button->isEnabled()) button->click();
  if (guard) emit readerChanged();
}
