#include "transcript_panel.h"
#include "phrase_card_paint.h"
#include <QHBoxLayout>
#include <QFrame>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QVBoxLayout>
#include <algorithm>

namespace {
class PhraseCard final : public QFrame {
public:
  using QFrame::QFrame;
protected:
  void paintEvent(QPaintEvent *) override { paintPhraseCard(this, false); }
};
QLabel *label(const QString &text, const char *name, QWidget *parent) {
  auto *result = new QLabel(text, parent);
  result->setObjectName(QString::fromLatin1(name));
  result->setTextFormat(Qt::PlainText);
  result->setWordWrap(true);
  return result;
}
void message(QLabel *target, const QString &text) {
  target->setText(text);
  target->setVisible(!text.isEmpty());
}
QString timestamp(std::int64_t milliseconds) {
  const auto seconds = std::max<std::int64_t>(0, milliseconds) / 1000;
  const auto minutes = seconds / 60;
  const QString tail = QStringLiteral("%1:%2").arg(minutes % 60, 2, 10, QChar('0')).arg(seconds % 60, 2, 10, QChar('0'));
  return seconds >= 3600 ? QStringLiteral("%1:%2").arg(seconds / 3600).arg(tail) : tail;
}
}

TranscriptPanel::TranscriptPanel(QWidget *parent) : QWidget(parent) {
  auto *layout = new QVBoxLayout(this);
  auto *header = new QHBoxLayout;
  header->addWidget(label(tr("Transcript"), "transcriptTitle", this));
  auto *badge = label(tr("Local"), "transcriptLocal", this);
  badge->setFrameStyle(QFrame::StyledPanel); header->addStretch(); header->addWidget(badge);
  layout->addLayout(header);
  mStatus = label({}, "transcriptStatus", this); layout->addWidget(mStatus);
  mReason = label({}, "transcriptStartReason", this); layout->addWidget(mReason); mReason->hide();
  mError = label({}, "transcriptError", this); mError->setFrameStyle(QFrame::StyledPanel); layout->addWidget(mError); mError->hide();
  mNotice = label({}, "transcriptNotice", this); layout->addWidget(mNotice); mNotice->hide();
  mAudioGap = label(tr("Microphone audio interrupted; other participants are still being transcribed"), "transcriptAudioGap", this); layout->addWidget(mAudioGap); mAudioGap->hide();
  mDelayed = label(tr("Transcription is falling behind"), "transcriptDelayed", this); layout->addWidget(mDelayed); mDelayed->hide();
  mScroll = new QScrollArea(this); mScroll->setWidgetResizable(true); mScroll->setObjectName("transcriptScroll");
  auto *content = new QWidget(mScroll); mRows = new QVBoxLayout(content); mRows->setAlignment(Qt::AlignTop); mRows->setContentsMargins(0, 8, 12, 8); mRows->setSpacing(10); mScroll->setFrameShape(QFrame::NoFrame); mScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  mListening = label(tr("Listening…"), "transcriptListening", content); mRows->addWidget(mListening); mListening->hide();
  mScroll->setWidget(content); layout->addWidget(mScroll, 1);
  auto *bar = mScroll->verticalScrollBar();
  connect(bar, &QScrollBar::valueChanged, this, [this, bar](int value) { mFollowing = value >= bar->maximum() - 1; });
  connect(bar, &QScrollBar::rangeChanged, this, [this, bar](int, int maximum) { if (mFollowing) bar->setValue(maximum); });
  layout->addWidget(label(tr("Audio is not stored"), "transcriptNoAudio", this));
  auto *buttons = new QHBoxLayout;
  auto button = [this, buttons](const QString &text, const char *name) {
    auto *result = new QPushButton(text, this); result->setObjectName(name); result->setAccessibleName(text); buttons->addWidget(result); return result;
  };
  mStart = button(tr("Start"), "transcriptStart"); mStop = button(tr("Stop"), "transcriptStop"); mRevoke = button(tr("Revoke consent"), "transcriptRevoke");
  layout->addLayout(buttons);
  connect(mStart, &QPushButton::clicked, this, &TranscriptPanel::startRequested);
  connect(mStop, &QPushButton::clicked, this, &TranscriptPanel::stopRequested);
  connect(mRevoke, &QPushButton::clicked, this, &TranscriptPanel::revokeRequested);
  setStartAvailable(false, {}); setState(pcm::calltranscription::SessionState::Idle);
}
void TranscriptPanel::setState(pcm::calltranscription::SessionState state) {
  using pcm::calltranscription::SessionState;
  QString status;
  switch (state) {
  case SessionState::Idle: status = tr("Transcription is off"); break;
  case SessionState::Loading: status = tr("Loading speech model…"); break;
  case SessionState::Recording: status = tr("Transcription running"); break;
  case SessionState::Stopping: status = tr("Finishing…"); break;
  case SessionState::Finished: status = tr("Transcript saved as a draft"); break;
  case SessionState::Failed: status = tr("Transcription stopped"); break;
  }
  mStatus->setText(status);
  const bool canStop = state == SessionState::Loading || state == SessionState::Recording;
  mStart->setVisible(state == SessionState::Idle || state == SessionState::Finished || state == SessionState::Failed);
  mStop->setVisible(canStop); mRevoke->setVisible(canStop || state == SessionState::Stopping);
  mListening->setVisible(state == SessionState::Recording);
  if (!canStop) mAudioGap->hide();
}
void TranscriptPanel::setStartAvailable(bool available, const QString &reason) {
  mStart->setEnabled(available); message(mReason, available ? QString{} : reason);
}
void TranscriptPanel::addPhrase(const DuckTranscriptPhrase &phrase) {
  auto *row = new PhraseCard(mScroll->widget());
  row->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Maximum);
  auto *layout = new QVBoxLayout(row);
  layout->setContentsMargins(18, 12, 18, 14); layout->setSpacing(8);
  const bool practitioner = phrase.track_role == "practitioner";
  const QString name = phrase.speaker_name && !phrase.speaker_name->empty() ? QString::fromStdString(*phrase.speaker_name) : (practitioner ? tr("You") : tr("Participant"));
  row->setProperty("practitioner", practitioner);
  auto *meta = new QHBoxLayout;
  auto *dot = label(QStringLiteral("●"), "speakerMarker", row);
  auto markerPalette = dot->palette();
  markerPalette.setColor(QPalette::WindowText, practitioner ? palette().color(QPalette::Highlight) : QColor("#65bda9"));
  dot->setPalette(markerPalette);
  meta->addWidget(dot);
  auto *speaker = label(name, "phraseSpeaker", row);
  QFont font = speaker->font(); font.setBold(true); font.setPointSize(11); speaker->setFont(font);
  meta->addWidget(speaker); meta->addStretch();
  const auto range = phrase.end_ms > phrase.start_ms ? timestamp(phrase.start_ms) + QStringLiteral(" – ") + timestamp(phrase.end_ms) : timestamp(phrase.start_ms);
  meta->addWidget(label(range, "phraseTime", row));
  layout->addLayout(meta);
  auto *text = label(QString::fromStdString(phrase.text), "phraseText", row);
  auto textFont = text->font(); textFont.setPointSize(12); text->setFont(textFont);
  text->setTextInteractionFlags(Qt::TextSelectableByMouse); layout->addWidget(text);
  mRows->insertWidget(mRows->count() - 1, row); ++mPhraseCount;
}
void TranscriptPanel::setDelayed(bool delayed) { mDelayed->setVisible(delayed); }
void TranscriptPanel::setNotice(const QString &text) { message(mNotice, text); }
void TranscriptPanel::setAudioGap(bool interrupted) { mAudioGap->setVisible(interrupted); }
void TranscriptPanel::setError(const QString &text) { message(mError, text); }
void TranscriptPanel::clearPhrases() {
  while (mRows->count() > 1) { auto *item = mRows->takeAt(0); delete item->widget(); delete item; }
  mPhraseCount = 0; mFollowing = true;
}
int TranscriptPanel::phraseCount() const { return mPhraseCount; }
