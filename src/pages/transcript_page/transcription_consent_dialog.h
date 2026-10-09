#pragma once

#include <QDialog>

class QCheckBox;
class QPushButton;

class TranscriptionConsentDialog final : public QDialog {
  Q_OBJECT
public:
  explicit TranscriptionConsentDialog(QWidget *parent = nullptr);
  [[nodiscard]] bool consentChecked() const;
  [[nodiscard]] QPushButton *startButton() const;

public slots:
  void done(int result) override;

private:
  QCheckBox *mConsent;
  QPushButton *mStart;
};
