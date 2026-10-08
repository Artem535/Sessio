#include "transcript_page.h"
#include "transcript_client_dialog.h"
#include <QFileDialog>
#include <QSaveFile>
#include <QComboBox>
#include <QApplication>
#include <QFrame>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QDateTime>
#include <QLabel>
#include <QLocale>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPointer>
#include <QPushButton>
#include <QScrollArea>
#include <QVBoxLayout>
#include <QToolButton>
#include <QWidgetAction>
#include <algorithm>

namespace {
class PhraseCard final : public QFrame {
public:
  explicit PhraseCard(QWidget *parent) : QFrame(parent) {
    setFocusPolicy(Qt::StrongFocus);
    connect(qApp, &QApplication::focusChanged, this, [this] { refreshActions(); });
  }
  void setActions(QWidget *actions) {
    mActions = actions;
    auto policy = actions->sizePolicy();
    policy.setRetainSizeWhenHidden(true);
    actions->setSizePolicy(policy);
    refreshActions();
  }
  void pinActions(bool pinned) { mPinned = pinned; refreshActions(); }
protected:
  void enterEvent(QEnterEvent *event) override {
    mHovered = true; refreshActions(); QFrame::enterEvent(event);
  }
  void leaveEvent(QEvent *event) override {
    mHovered = false; refreshActions(); QFrame::leaveEvent(event);
  }
  void mousePressEvent(QMouseEvent *event) override {
    if (event->button() == Qt::LeftButton) setFocus(Qt::MouseFocusReason);
    QFrame::mousePressEvent(event);
  }
  void paintEvent(QPaintEvent *) override {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    auto background = palette().color(QPalette::Window);
    background = background.lightness() < 128 ? background.lighter(115) : background.darker(103);
    auto border = palette().color(mSelected ? QPalette::Highlight : QPalette::Mid);
    if (!mSelected) border.setAlpha(100);
    painter.setPen(QPen(border, 1));
    painter.setBrush(background);
    painter.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), 8, 8);
  }
private:
  void refreshActions() {
    const auto *focus = QApplication::focusWidget();
    mSelected = focus && (focus == this || isAncestorOf(focus));
    if (mActions) mActions->setVisible(mHovered || mSelected || mPinned);
    update();
  }
  QWidget *mActions = nullptr;
  bool mHovered = false;
  bool mSelected = false;
  bool mPinned = false;
};
QMenu *actionMenu(QToolButton *trigger, QPushButton *action) {
  auto *menu = new QMenu(trigger);
  auto *item = new QWidgetAction(menu);
  item->setDefaultWidget(action);
  menu->addAction(item);
  trigger->setMenu(menu);
  trigger->setPopupMode(QToolButton::InstantPopup);
  trigger->setText(QStringLiteral("⋯"));
  trigger->setAutoRaise(true);
  QObject::connect(action, &QPushButton::clicked, menu, &QMenu::close);
  return menu;
}
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
  auto *outer = new QHBoxLayout(this);
  outer->setContentsMargins(24, 24, 24, 24);
  auto *content = new QWidget(this);
  content->setMaximumWidth(940);
  outer->addStretch(); outer->addWidget(content, 1); outer->addStretch();
  auto *layout = new QVBoxLayout(content);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(10);
  auto *back = button(tr("Back to event"), "backToEvent", this);
  auto *navigation = new QHBoxLayout;
  navigation->addWidget(back); navigation->addStretch();
  mAttach = button(tr("Attach clients"), "transcriptAttachClients", this);
  navigation->addWidget(mAttach);
  auto *exportButton = button(tr("Export text"), "transcriptExport", this);
  navigation->addWidget(exportButton);
  connect(exportButton, &QPushButton::clicked, this, [this] {
    QPointer<TranscriptPage> guard(this);
    try {
      const auto bytes = exportText().toUtf8();
      const auto path = QFileDialog::getSaveFileName(this, tr("Export transcript"), {}, tr("Text files (*.txt)"));
      if (!guard || path.isEmpty()) return;
      QSaveFile file(path);
      if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit()) reportFailure();
    } catch (...) { if (guard) reportFailure(); }
  });
  connect(mAttach, &QPushButton::clicked, this, [this] {
    const auto id = mSelector->currentData().toLongLong();
    QPointer<TranscriptPage> guard(this);
    try {
      if (!mayMutate(id)) return;
      QPointer<TranscriptClientDialog> dialog = new TranscriptClientDialog(mDb, id, this);
      const auto result = dialog->exec();
      if (!guard || !dialog) return;
      const auto clients = dialog->selectedClientIds();
      dialog->deleteLater();
      if (result != QDialog::Accepted) return;
      if (!mayMutate(id)) return;
      if (!mDb->set_transcript_clients(id, clients)) { reportFailure(); return; }
      showTranscript(); emit transcriptsChanged();
    } catch (...) { if (guard) reportFailure(); }
  });
  auto *more = new QToolButton(this);
  more->setAccessibleName(tr("More actions"));
  more->setObjectName("transcriptMoreActions");
  mDelete = button(tr("Delete transcript"), "deleteTranscript", this);
  actionMenu(more, mDelete);
  navigation->addWidget(more);
  layout->addLayout(navigation);
  connect(back, &QPushButton::clicked, this, &TranscriptPage::backRequested);
  mSelector = new QComboBox(this);
  mSelector->setObjectName("transcriptSelector");
  mSelector->setAccessibleName(tr("Transcript"));
  layout->addWidget(mSelector);
  mTitle = label({}, "transcriptTitle", this);
  auto font = mTitle->font(); font.setPointSize(22); font.setBold(true); mTitle->setFont(font);
  auto *heading = new QHBoxLayout;
  heading->addWidget(mTitle, 1);
  mReview = button(tr("Mark reviewed"), "markReviewed", this);
  heading->addWidget(mReview);
  layout->addLayout(heading);
  mClient = label({}, "transcriptClient", this); layout->addWidget(mClient);
  mMetadata = label({}, "transcriptMetadata", this);
  mStatus = label({}, "transcriptStatus", this);
  auto *metadata = new QHBoxLayout;
  metadata->addWidget(mStatus); metadata->addWidget(mMetadata); metadata->addStretch();
  layout->addLayout(metadata);
  mNotice = label({}, "transcriptNotice", this); layout->addWidget(mNotice);
  auto *scroll = new QScrollArea(this); scroll->setWidgetResizable(true);
  scroll->setFrameShape(QFrame::NoFrame);
  scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  auto *rows = new QWidget(scroll); mRows = new QVBoxLayout(rows);
  rows->setAutoFillBackground(false);
  mRows->setContentsMargins(0, 8, 12, 8); mRows->setSpacing(10);
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
      if (mTranscriptId) reloadTranscript(*mTranscriptId, mTitle->text());
      else reload(mEventId, mTitle->text());
      emit transcriptsChanged();
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
  if (!row || (mTranscriptId ? row->id != *mTranscriptId : row->event_id != mEventId)) return false;
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
  mTranscriptId.reset(); mEventId = eventId; mTitle->setText(title);
  findChild<QPushButton *>("backToEvent")->setText(tr("Back to event"));
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
void TranscriptPage::reloadTranscript(int64_t transcriptId, const QString &title) {
  if (mEditing) { mNotice->setText(tr("Save or cancel the current edit first.")); return; }
  mTranscriptId = transcriptId;
  mEventId = 0;
  mTitle->setText(title);
  findChild<QPushButton *>("backToEvent")->setText(tr("Back to transcripts"));
  mSelector->blockSignals(true); mSelector->clear();
  try {
    if (const auto row = mDb->get_transcript(transcriptId))
      mSelector->addItem(QLocale().toString(QDateTime::fromMSecsSinceEpoch(row->created_at), QLocale::ShortFormat),
                         QVariant::fromValue<qlonglong>(row->id));
  } catch (...) { reportFailure(); }
  mSelector->blockSignals(false); mSelector->hide();
  showTranscript();
}
QString TranscriptPage::exportText() const {
  QStringList lines;
  if (mSelector->currentIndex() < 0) return {};
  for (const auto &phrase : mDb->get_transcript_phrases(mSelector->currentData().toLongLong())) {
    const auto speaker = phrase.speaker_name ? QString::fromStdString(*phrase.speaker_name) :
        phrase.track_role == "practitioner" ? tr("Practitioner") : tr("Participant");
    lines << timestamp(phrase.start_ms) + " " + speaker + ": " + QString::fromStdString(phrase.text);
  }
  return lines.join("\n") + "\n";
}
void TranscriptPage::showTranscript() {
  mEditing = false;
  mSelector->setEnabled(true);
  findChild<QPushButton *>("backToEvent")->setEnabled(true);
  findChild<QPushButton *>("transcriptExport")->setEnabled(mSelector->count() > 0);
  while (auto *item = mRows->takeAt(0)) { delete item->widget(); delete item; }
  mNotice->clear(); mMetadata->clear(); mStatus->clear();
  mReview->hide(); mDelete->hide();
  mAttach->setEnabled(false);
  mClient->setText(tr("No clients attached"));
  if (!mSelector->count()) {
    mNotice->setText(mTranscriptId ? tr("Transcript unavailable.") : tr("No transcripts for this event."));
    return;
  }
  try {
    const auto row = mDb->get_transcript(mSelector->currentData().toLongLong());
    if (!row) { mNotice->setText(tr("Transcript unavailable.")); return; }
    const auto phrases = mDb->get_transcript_phrases(row->id);
    const bool recording = row->status == "recording";
    QStringList names;
    for (const auto id : mDb->get_transcript_client_ids(row->id)) {
      if (const auto client = mDb->get_client(id))
        names << (QString::fromStdString(client->name.value_or("")) + " " +
                   QString::fromStdString(client->last_name.value_or(""))).trimmed();
    }
    if (!names.isEmpty()) mClient->setText(tr("Clients: %1").arg(names.join(", ")));
    mAttach->setEnabled(!recording);
    mStatus->setText(recording ? tr("Recording") : row->status == "reviewed" ? tr("Reviewed") : tr("Draft"));
    auto metadata = tr("%n phrases", nullptr, static_cast<int>(phrases.size()));
    if (row->model_id && !row->model_id->empty())
      metadata += tr(" · Model: %1").arg(QString::fromStdString(*row->model_id));
    mMetadata->setText(metadata);
    mReview->setVisible(row->status != "reviewed"); mReview->setEnabled(!recording);
    mDelete->show(); mDelete->setEnabled(!recording);
    if (recording) mNotice->setText(tr("This transcript is still being recorded. Editing is unavailable."));
    for (const auto &phrase : phrases) {
      auto *widget = new PhraseCard(this);
      widget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Maximum);
      widget->setObjectName("phraseCard_" + QString::number(phrase.id));
      auto *layout = new QVBoxLayout(widget);
      layout->setContentsMargins(18, 12, 18, 14); layout->setSpacing(8);
      const auto speaker = phrase.speaker_name ? QString::fromStdString(*phrase.speaker_name) :
          phrase.track_role == "practitioner" ? tr("Practitioner") : tr("Participant");
      auto *header = new QHBoxLayout;
      auto *dot = label(QStringLiteral("●"), "speakerMarker", widget);
      auto markerPalette = dot->palette();
      markerPalette.setColor(QPalette::WindowText, phrase.track_role == "practitioner"
          ? palette().color(QPalette::Highlight) : QColor("#65bda9"));
      dot->setPalette(markerPalette);
      header->addWidget(dot);
      auto *speakerLabel = label(speaker, "phraseSpeaker", widget);
      auto speakerFont = speakerLabel->font(); speakerFont.setBold(true); speakerFont.setPointSize(11);
      speakerLabel->setFont(speakerFont);
      header->addWidget(speakerLabel); header->addStretch();
      header->addWidget(label(timestamp(phrase.start_ms) + " – " + timestamp(phrase.end_ms), "phraseTime", widget));
      auto *text = label(QString::fromStdString(phrase.text), "phraseText", widget);
      auto textFont = text->font(); textFont.setPointSize(12); text->setFont(textFont);
      auto *actionsWidget = new QWidget(widget); actionsWidget->setObjectName("phraseActions");
      auto *actions = new QHBoxLayout(actionsWidget);
      actions->setContentsMargins(0, 0, 0, 0); actions->setSpacing(6);
      auto *edit = button(tr("Edit"), "editPhrase_" + QString::number(phrase.id), widget);
      auto *remove = button(tr("Delete"), "deletePhrase_" + QString::number(phrase.id), widget);
      auto *more = new QToolButton(widget);
      more->setAccessibleName(tr("More actions"));
      auto *menu = actionMenu(more, remove);
      connect(menu, &QMenu::aboutToShow, widget, [widget] { widget->pinActions(true); });
      connect(menu, &QMenu::aboutToHide, widget, [widget] { widget->pinActions(false); });
      actions->addWidget(edit); actions->addWidget(more);
      header->addWidget(actionsWidget); layout->addLayout(header);
      layout->addWidget(text);
      if (phrase.edited) layout->addWidget(label(tr("Edited"), "phraseEdited", widget));
      widget->setActions(actionsWidget);
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
        editor->setFont(text->font()); editor->setMinimumHeight(110);
        text->hide(); edit->hide(); layout->addWidget(editor);
        auto *editingActions = new QHBoxLayout;
        editingActions->addStretch(); editingActions->addWidget(cancel); editingActions->addWidget(save);
        layout->addLayout(editingActions); editor->setFocus();
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
