#include "file_log.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QMutex>
#include <QMutexLocker>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <dbghelp.h>
#endif

namespace pcm::app {
namespace {
struct LogState {
  QMutex mutex;
  QFile file;
  QString path;
  qint64 maxBytes = 0;
  QtMessageHandler previous = nullptr;
};

LogState &state() {
  static LogState value;
  return value;
}

const char *levelName(QtMsgType type) {
  switch (type) {
  case QtDebugMsg: return "debug";
  case QtInfoMsg: return "info";
  case QtWarningMsg: return "warning";
  case QtCriticalMsg: return "critical";
  case QtFatalMsg: return "fatal";
  }
  return "?";
}

void rotateIfNeeded(LogState &s) {
  if (s.file.size() < s.maxBytes) return;
  s.file.close();
  const QString old = s.path + QStringLiteral(".1");
  QFile::remove(old);
  QFile::rename(s.path, old);
  s.file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text);
}

void handler(QtMsgType type, const QMessageLogContext &context, const QString &message) {
  auto &s = state();
  {
    QMutexLocker lock(&s.mutex);
    if (s.file.isOpen()) {
      rotateIfNeeded(s);
      const QByteArray line =
          QDateTime::currentDateTime().toString(Qt::ISODateWithMs).toUtf8() + ' ' + levelName(type) + ' ' +
          (context.category ? context.category : "default") + ": " + message.toUtf8() + '\n';
      s.file.write(line);
      s.file.flush(); // the next line may never come if the process is about to die
    }
  }
  if (s.previous) s.previous(type, context, message);
}

#ifdef Q_OS_WIN
wchar_t gDumpDir[MAX_PATH] = {};

LONG WINAPI writeDump(EXCEPTION_POINTERS *info) {
  SYSTEMTIME now;
  GetLocalTime(&now);
  wchar_t path[MAX_PATH];
  swprintf_s(path, L"%s\\sessio-crash-%04u%02u%02u-%02u%02u%02u.dmp", gDumpDir, now.wYear, now.wMonth,
             now.wDay, now.wHour, now.wMinute, now.wSecond);
  const HANDLE file = CreateFileW(path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (file != INVALID_HANDLE_VALUE) {
    MINIDUMP_EXCEPTION_INFORMATION exception{GetCurrentThreadId(), info, FALSE};
    // Thread stacks and module list only: enough to find the faulting call,
    // without the heap (and so without call audio, video or names).
    MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), file, MiniDumpNormal, &exception, nullptr,
                      nullptr);
    CloseHandle(file);
  }
  return EXCEPTION_CONTINUE_SEARCH;
}
#endif
} // namespace

void installFileLog(const QString &dir, qint64 maxBytes) {
  auto &s = state();
  QMutexLocker lock(&s.mutex);
  if (s.file.isOpen() || !QDir().mkpath(dir)) return;
  s.path = QDir(dir).filePath(QStringLiteral("sessio.log"));
  s.maxBytes = maxBytes;
  s.file.setFileName(s.path);
  if (!s.file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
    s.path.clear();
    return;
  }
  s.previous = qInstallMessageHandler(handler);
}

QString fileLogPath() {
  auto &s = state();
  QMutexLocker lock(&s.mutex);
  return s.path;
}

void installCrashDumpHandler(const QString &dir) {
#ifdef Q_OS_WIN
  if (!QDir().mkpath(dir)) return;
  const auto native = QDir::toNativeSeparators(dir).toStdWString();
  wcsncpy_s(gDumpDir, native.c_str(), _TRUNCATE);
  SetUnhandledExceptionFilter(writeDump);
#else
  Q_UNUSED(dir);
#endif
}

} // namespace pcm::app
