#pragma once

#include <QMainWindow>

class ClientModeWindow final : public QMainWindow {
  Q_OBJECT

public:
  explicit ClientModeWindow(QWidget *parent = nullptr);
};
