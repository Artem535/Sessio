#include "application_restarter.h"
#include "single_instance_guard.h"

#include <QCoreApplication>
#include <QProcess>

#include <utility>

namespace pcm {

ApplicationRestarter::ApplicationRestarter(SingleInstanceGuard *guard, QString program,
                                           QStringList arguments, Spawn spawn, Quit quit)
    : mGuard(guard), mProgram(std::move(program)), mArguments(std::move(arguments)),
      mSpawn(std::move(spawn)), mQuit(std::move(quit)) {
  if (!mSpawn) {
    mSpawn = [](const QString &program, const QStringList &args) {
      return QProcess::startDetached(program, args);
    };
  }
  if (!mQuit) {
    mQuit = []() { QCoreApplication::quit(); };
  }
}

QStringList ApplicationRestarter::restartArguments(const QStringList &originalArguments) {
  QStringList result;
  for (const auto &argument : originalArguments) {
    if (!argument.startsWith(QStringLiteral("sessio:"), Qt::CaseInsensitive)) {
      result.append(argument);
    }
  }
  return result;
}

ApplicationRestarter ApplicationRestarter::forRunningApplication(SingleInstanceGuard *guard,
                                                             Spawn spawn, Quit quit) {
  return ApplicationRestarter(guard, QCoreApplication::applicationFilePath(),
                              restartArguments(QCoreApplication::arguments().mid(1)),
                              std::move(spawn), std::move(quit));
}

bool ApplicationRestarter::restart() const {
  if (mGuard != nullptr) {
    mGuard->release();
  }
  const bool started = mSpawn(mProgram, mArguments);
  if (!started) {
    if (mGuard != nullptr) {
      mGuard->reacquire();
    }
    return false;
  }
  mQuit();
  return true;
}

} // namespace pcm
