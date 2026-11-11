#pragma once

#include <QDialog>

class QCheckBox;
class QPushButton;

// The terms for sending call text to an AI provider chosen by the user.
// Accepting is bound to the provider's address by the caller.
class CloudConsentDialog final : public QDialog {
  Q_OBJECT
public:
  explicit CloudConsentDialog(const QString &host, bool pseudonymize, QWidget *parent = nullptr);
  QPushButton *acceptButton() const { return mAccept; }
  QCheckBox *understoodBox() const { return mUnderstood; }
private:
  QCheckBox *mUnderstood;
  QPushButton *mAccept;
};
