#include "transcription_consent_dialog.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

TranscriptionConsentDialog::TranscriptionConsentDialog(QWidget *parent) : QDialog(parent) {
  setWindowTitle(tr("Transcribe this session"));
  setModal(true);
  auto *layout = new QVBoxLayout(this);
  auto addNote = [this, layout](const char *name, const QString &text) {
    auto *label = new QLabel(text, this);
    label->setObjectName(QString::fromLatin1(name));
    label->setWordWrap(true);
    layout->addWidget(label);
  };
  addNote("consentLocalNote", tr("The session is converted to text on this computer."));
  addNote("consentNoAudioNote", tr("Audio is not stored and is not sent anywhere."));
  addNote("consentRevokeNote", tr("The client can withdraw consent at any moment, and transcription stops immediately."));
  addNote("consentEditNote", tr("The text is saved as a draft and can be edited and deleted."));
  mConsent = new QCheckBox(tr("The client has given consent to transcribing this session"), this);
  mConsent->setObjectName(QStringLiteral("consentCheckBox"));
  layout->addWidget(mConsent);
  auto *buttons = new QDialogButtonBox(this);
  mStart = buttons->addButton(tr("Start transcription"), QDialogButtonBox::AcceptRole);
  mStart->setObjectName(QStringLiteral("consentStartButton"));
  mStart->setEnabled(false);
  auto *cancel = buttons->addButton(QDialogButtonBox::Cancel);
  cancel->setText(tr("Cancel"));
  cancel->setObjectName(QStringLiteral("consentCancelButton"));
  layout->addWidget(buttons);
  connect(mConsent, &QCheckBox::toggled, mStart, &QPushButton::setEnabled);
  connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
  connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

bool TranscriptionConsentDialog::consentChecked() const { return mConsent->isChecked(); }
QPushButton *TranscriptionConsentDialog::startButton() const { return mStart; }

void TranscriptionConsentDialog::done(int result) {
  if (result == QDialog::Accepted && !consentChecked()) return;
  QDialog::done(result);
}
