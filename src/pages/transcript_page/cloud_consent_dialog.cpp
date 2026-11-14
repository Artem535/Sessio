#include "cloud_consent_dialog.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

CloudConsentDialog::CloudConsentDialog(const QString &host, bool pseudonymize, QWidget *parent) : QDialog(parent) {
  setWindowTitle(tr("Use a cloud AI model"));
  setModal(true);
  auto *layout = new QVBoxLayout(this);
  const auto add = [this, layout](const char *name, const QString &text) {
    auto *label = new QLabel(text, this);
    label->setObjectName(QString::fromLatin1(name));
    label->setWordWrap(true);
    label->setTextFormat(Qt::PlainText);
    layout->addWidget(label);
  };
  add("cloudConsentHost", tr("The text will be sent to: %1").arg(host));
  add("cloudConsentWhat", tr("What is sent: phrases of the call transcript, the agreements found so far and the notes used for key points and the meeting summary. Audio and video are never sent."));
  add("cloudConsentPersonal", tr("These texts can contain personal data, including health information. They may be processed in another country, under the provider's terms and not under Sessio's control."));
  add("cloudConsentPseudonyms", pseudonymize
      ? tr("Names, phone numbers and e-mail addresses of the client and of you are replaced by labels before sending. Other details said aloud, such as third parties or places, are not hidden.")
      : tr("Hiding names and contact details is turned off: the text is sent as it is."));
  add("cloudConsentResponsibility", tr("You choose the provider and are responsible for having the right to send this data to it. Consent applies to this address only: a different address asks again. You can withdraw consent at any time."));
  mUnderstood = new QCheckBox(tr("I understand and accept"), this);
  mUnderstood->setObjectName("cloudConsentUnderstood");
  layout->addWidget(mUnderstood);
  auto *buttons = new QDialogButtonBox(this);
  mAccept = buttons->addButton(tr("Enable cloud models"), QDialogButtonBox::AcceptRole);
  mAccept->setObjectName("cloudConsentAccept");
  mAccept->setEnabled(false);
  buttons->addButton(QDialogButtonBox::Cancel)->setText(tr("Cancel"));
  layout->addWidget(buttons);
  connect(mUnderstood, &QCheckBox::toggled, mAccept, &QPushButton::setEnabled);
  connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
  connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
}
