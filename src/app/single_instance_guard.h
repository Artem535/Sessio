#pragma once

#include <QLocalServer>
#include <QObject>

class SingleInstanceGuard final : public QObject {
  Q_OBJECT

public:
  explicit SingleInstanceGuard(QObject *parent = nullptr);

  [[nodiscard]] bool isPrimaryInstance() const { return mIsPrimary; }
  void forwardToPrimaryInstance(const QString &url);

signals:
  void urlReceivedFromSecondaryInstance(QString url);

private:
  static constexpr auto kServerName = "Sessio-single-instance";

  QLocalServer mServer;
  bool mIsPrimary = false;
};
