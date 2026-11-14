#pragma once

#include <QString>
#include <QStringList>

#include <functional>

class SingleInstanceGuard;

namespace pcm {

// Relaunches the running executable and ends the current process.
//
// Sequence (order matters, see restart()):
//   1. release the single-instance lock + IPC endpoint, so the new process can
//      become primary instead of forwarding its launch to us and exiting;
//   2. spawn the new process (same executable, same arguments);
//   3a. spawned  -> ask the event loop to quit;
//   3b. failed   -> take the single-instance lock back and keep running.
class ApplicationRestarter final {
public:
  using Spawn = std::function<bool(const QString &program, const QStringList &arguments)>;
  using Quit = std::function<void()>;

  // `guard` is not owned and may be null (nothing to release). Defaults:
  // QProcess::startDetached for `spawn`, QCoreApplication::quit for `quit`.
  ApplicationRestarter(SingleInstanceGuard *guard, QString program, QStringList arguments,
                       Spawn spawn = {}, Quit quit = {});

  // Restarter for the running process: applicationFilePath() plus the original
  // command-line arguments. A sessio:// launch URL is dropped so a restart does
  // not replay a stale join link.
  static ApplicationRestarter forRunningApplication(SingleInstanceGuard *guard, Spawn spawn = {},
                                                    Quit quit = {});

  // Returns true when the new process was started.
  bool restart() const;

  [[nodiscard]] static QStringList restartArguments(const QStringList &originalArguments);

private:
  SingleInstanceGuard *mGuard;
  QString mProgram;
  QStringList mArguments;
  Spawn mSpawn;
  Quit mQuit;
};

} // namespace pcm
