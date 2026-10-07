#include "transcription_settings_panel.h"
#include <QCheckBox>
#include <QLabel>
#include <QPushButton>
#include <QSignalBlocker>
#include <QVBoxLayout>

TranscriptionSettingsPanel::TranscriptionSettingsPanel(QWidget *parent) : QWidget(parent) {
  auto *layout = new QVBoxLayout(this);
  mEnabled = new QCheckBox(tr("Enable call transcription"), this);
  mEnabled->setObjectName("transcriptionEnabled");
  layout->addWidget(mEnabled);
  mModel = new QLabel(this);
  mModel->setObjectName("transcriptionModelInfo");
  mModel->setTextFormat(Qt::PlainText);
  layout->addWidget(mModel);
  auto *note = new QLabel(tr("Transcription runs on this computer. Audio is never stored. Text is saved in the local database next to your other data and included in backups."), this);
  note->setWordWrap(true);
  layout->addWidget(note);
  mCount = new QLabel(this);
  layout->addWidget(mCount);
  mDelete = new QPushButton(tr("Delete all transcripts…"), this);
  mDelete->setObjectName("deleteAllTranscripts");
  layout->addWidget(mDelete);
  layout->addStretch();
  connect(mEnabled, &QCheckBox::toggled, this, &TranscriptionSettingsPanel::enabledToggled);
  connect(mDelete, &QPushButton::clicked, this, &TranscriptionSettingsPanel::deleteAllRequested);
  setTranscriptCount(0);
}
void TranscriptionSettingsPanel::setEnabledState(bool enabled) {
  const QSignalBlocker block(mEnabled);
  mEnabled->setChecked(enabled);
}
void TranscriptionSettingsPanel::setModelInfo(const QString &id, bool installed) {
  mModel->setText(tr("Model: %1 — %2").arg(id, installed ? tr("installed") : tr("not installed")));
}
void TranscriptionSettingsPanel::setTranscriptCount(int count) {
  mTranscriptCount = count;
  mCount->setText(tr("%n transcripts stored", nullptr, count));
  mDelete->setEnabled(mDeleteAllowed && count > 0);
}
void TranscriptionSettingsPanel::setDeleteAllAllowed(bool allowed, const QString &reason) {
  mDeleteAllowed = allowed;
  mDelete->setToolTip(reason);
  mDelete->setEnabled(allowed && mTranscriptCount > 0);
}
