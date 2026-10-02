#include "role_selection_dialog.h"

#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

RoleSelectionDialog::RoleSelectionDialog(QWidget *parent) : QDialog(parent) {
  setWindowTitle(tr("Welcome to Sessio"));
  setModal(true);

  auto *layout = new QVBoxLayout(this);
  auto *prompt = new QLabel(tr("Who are you?"), this);
  layout->addWidget(prompt);

  auto *specialistButton = new QPushButton(tr("I'm a specialist"), this);
  specialistButton->setObjectName("specialistButton");
  auto *clientButton = new QPushButton(tr("I'm a client"), this);
  clientButton->setObjectName("clientButton");
  layout->addWidget(specialistButton);
  layout->addWidget(clientButton);

  connect(specialistButton, &QPushButton::clicked, this, [this]() {
    mSelectedRole = pcm::config::AppRole::Specialist;
    accept();
  });
  connect(clientButton, &QPushButton::clicked, this, [this]() {
    mSelectedRole = pcm::config::AppRole::Client;
    accept();
  });
}
