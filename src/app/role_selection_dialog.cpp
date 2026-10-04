#include "role_selection_dialog.h"

#include "../widgets/constants.hpp"

#include <QFont>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

namespace {
constexpr int kDialogWidth = 420;
constexpr int kDialogHeight = 220;
constexpr int kButtonMinWidth = 200;
constexpr int kButtonMaxWidth = 260;
constexpr int kContentMargin = 24;
constexpr int kContentSpacing = pcm::widgets::constants::kPanelPadding;
} // namespace

RoleSelectionDialog::RoleSelectionDialog(QWidget *parent) : QDialog(parent) {
  setWindowTitle(tr("Welcome to Sessio"));
  setModal(true);
  setWindowFlag(Qt::WindowContextHelpButtonHint, false);
  setWindowFlag(Qt::WindowMaximizeButtonHint, false);
  setSizeGripEnabled(false);
  setFixedSize(kDialogWidth, kDialogHeight);

  auto *layout = new QVBoxLayout(this);
  layout->setContentsMargins(kContentMargin, kContentMargin, kContentMargin, kContentMargin);
  layout->setSpacing(kContentSpacing);

  auto *prompt = new QLabel(tr("Who are you?"), this);
  prompt->setObjectName("promptLabel");
  prompt->setAlignment(Qt::AlignCenter);
  auto promptFont = prompt->font();
  promptFont.setPointSizeF(promptFont.pointSizeF() * 1.3);
  promptFont.setBold(true);
  prompt->setFont(promptFont);

  auto *specialistButton = new QPushButton(tr("I'm a specialist"), this);
  specialistButton->setObjectName("specialistButton");
  specialistButton->setMaximumWidth(kButtonMaxWidth);
  specialistButton->setMinimumWidth(kButtonMinWidth);
  auto *clientButton = new QPushButton(tr("I'm a client"), this);
  clientButton->setObjectName("clientButton");
  clientButton->setMaximumWidth(kButtonMaxWidth);
  clientButton->setMinimumWidth(kButtonMinWidth);

  layout->addStretch(1);
  layout->addWidget(prompt);
  layout->addSpacing(kContentSpacing);
  layout->addWidget(specialistButton, 0, Qt::AlignHCenter);
  layout->addWidget(clientButton, 0, Qt::AlignHCenter);
  layout->addStretch(1);

  connect(specialistButton, &QPushButton::clicked, this, [this]() {
    mSelectedRole = pcm::config::AppRole::Specialist;
    accept();
  });
  connect(clientButton, &QPushButton::clicked, this, [this]() {
    mSelectedRole = pcm::config::AppRole::Client;
    accept();
  });
}
