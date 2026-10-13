#include "prompter_worker.h"
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QProcessEnvironment>
#include <QStandardPaths>

PrompterWorker::PrompterWorker(QObject *parent) : QObject(parent) {
  mTimeout.setSingleShot(true); mTimeout.setInterval(90000);
  connect(&mTimeout, &QTimer::timeout, this, &PrompterWorker::fail);
  connect(&mProcess, &QProcess::readyReadStandardOutput, this, [this] {
    const auto bytes = mProcess.readAllStandardOutput();
    if (!mStopped) consumeOutput(bytes);
  });
  connect(&mProcess, &QProcess::readyReadStandardError, this, [this] {
    // Runtime diagnostics may contain prompts. Drain them without logging or storing them.
    mProcess.readAllStandardError();
  });
  connect(&mProcess, &QProcess::errorOccurred, this, [this](QProcess::ProcessError) { if (!mStopped) fail(); });
  connect(&mProcess, &QProcess::finished, this, [this](int, QProcess::ExitStatus) {
    if (!mRestart.isEmpty()) {
      const auto args = mRestart; mRestart.clear();
      const auto request = mPending;
      if (start(args[0], args[1], args[2], args[3]) && !request.isEmpty()) send(request);
    } else if (!mStopped) fail();
  });
}
PrompterWorker::~PrompterWorker() {
  stop();
  if (mProcess.state() != QProcess::NotRunning) mProcess.waitForFinished(1000);
}
QString PrompterWorker::bundledScript() {
  const auto app = QCoreApplication::applicationDirPath();
  const QStringList paths{QDir(app).filePath("call-prompter/worker.py"),
      QDir(app).filePath("../share/sessio/call-prompter/worker.py"),
      QDir(app).filePath("../Resources/call-prompter/worker.py")};
  for (const auto &path : paths) if (QFileInfo::exists(path)) return QFileInfo(path).absoluteFilePath();
  return {};
}
bool PrompterWorker::start(const QString &python, const QString &script, const QString &model, const QString &promptTemplate) {
  if (!QFileInfo(python).isExecutable() || !QFileInfo(script).isFile() || !QFileInfo(model).isFile() || !QFileInfo(promptTemplate).isFile()) {
    fail(); return false;
  }
  if (mProcess.state() != QProcess::NotRunning) {
    if (!mStopped) return true;
    mRestart = {python, script, model, promptTemplate};
    mTimeout.start(); return true;
  }
  mBuffer.clear(); mPending = {}; mReady = false; mStopped = false;
  mProcess.setProgram(python);
  mProcess.setArguments({"-u", script, "--model", model, "--template", promptTemplate, "--threads", "4"});
  auto environment = QProcessEnvironment::systemEnvironment();
  environment.insert("PYTHONUNBUFFERED", "1"); environment.insert("OMP_NUM_THREADS", "4");
  environment.insert("SESSIO_PROMPTER_CACHE_DIR", QDir(QStandardPaths::writableLocation(QStandardPaths::CacheLocation)).filePath("prompter"));
  mProcess.setProcessEnvironment(environment);
  mTimeout.start(); mProcess.start(); return true;
}
void PrompterWorker::stop() {
  mStopped = true; mReady = false; mTimeout.stop(); mPending = {}; mBuffer.clear();
  mRestart.clear(); mExpected = {};
  if (mProcess.state() != QProcess::NotRunning) mProcess.kill();
}
void PrompterWorker::fail() { stop(); emit failed(); }
void PrompterWorker::send(const QJsonObject &request) {
  if (mStopped && mRestart.isEmpty()) { emit failed(); return; }
  if (!mReady) {
    if (!mPending.isEmpty()) { fail(); return; }
    mPending = request; return;
  }
  const auto bytes = QJsonDocument(request).toJson(QJsonDocument::Compact) + '\n';
  if (bytes.size() > 256 * 1024 || mProcess.write(bytes) != bytes.size()) { fail(); return; }
  mExpected = request;
  mTimeout.start();
}
void PrompterWorker::consumeOutput(const QByteArray &bytes) {
  mBuffer += bytes;
  if (mBuffer.size() > 256 * 1024) { fail(); return; }
  while (mBuffer.contains('\n')) {
    const auto end = mBuffer.indexOf('\n');
    const auto line = mBuffer.left(end); mBuffer.remove(0, end + 1);
    QJsonParseError error;
    const auto doc = QJsonDocument::fromJson(line, &error);
    if (error.error != QJsonParseError::NoError || !doc.isObject() || doc.object()["protocol_version"].toInt() != 1) { fail(); return; }
    const auto result = doc.object();
    if (result["ready"].toBool()) {
      mReady = true; mTimeout.stop(); emit ready();
      if (!mPending.isEmpty()) { const auto request = mPending; mPending = {}; send(request); }
    } else {
      if (result.contains("error") && !result.contains("request_id")) { fail(); return; }
      if (mExpected.isEmpty() || result["request_id"] != mExpected["request_id"] ||
          result["session_generation"] != mExpected["session_generation"] ||
          result["ledger_revision"] != mExpected["ledger_revision"]) { fail(); return; }
      mExpected = {}; mTimeout.stop();
      emit resultReady(result);
    }
  }
}
