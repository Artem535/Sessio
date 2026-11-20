#pragma once

#include "app_role.h"

#include <QDialog>
#include <optional>

class RoleSelectionDialog final : public QDialog {
  Q_OBJECT

public:
  explicit RoleSelectionDialog(QWidget *parent = nullptr);

  [[nodiscard]] std::optional<pcm::config::AppRole> selectedRole() const { return mSelectedRole; }

private:
  std::optional<pcm::config::AppRole> mSelectedRole;
};
