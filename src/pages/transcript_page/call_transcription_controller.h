#pragma once
#include "transcription_session.h"
#include "video_session.h"
#include <optional>

class TranscriptPanel;
class CallSidePanel;
namespace pcm::transcriptionui {
struct ControllerHooks {
  std::function<std::optional<int64_t>(int64_t)> resolveEvent;
  std::function<bool()> askConsent;
  enum class RevokeChoice { DeleteRecorded, KeepAsDraft };
  std::function<RevokeChoice()> askRevokeChoice;
  std::function<pcm::calltranscription::EngineFactory()> engineFactory;
  std::function<bool()> modelsAvailable;
  std::function<bool()> transcriptionEnabled;
  std::function<void(int64_t)> eventMaterialised;
};
class CallTranscriptionController final : public QObject {
  Q_OBJECT
public:
  CallTranscriptionController(std::shared_ptr<pcm::database::Database> db,
                              TranscriptPanel *panel, CallSidePanel *sidePanel,
                              ControllerHooks hooks, QObject *parent = nullptr);
  ~CallTranscriptionController() override;
  void attachCall(pcm::video::VideoSession *session,
                  std::optional<int64_t> callEventId);
  void detachCall();
  void refreshAvailability();
  void shutdown();
  [[nodiscard]] bool startAvailable() const { return mStartAvailable; }
  [[nodiscard]] bool active() const { return mSession != nullptr; }
public slots:
  void onTranscribeRequested();
signals:
  void transcribeButtonState(bool active, QString tooltip);
  void requestOpenTranscriptTab();
  void transcriptReady(qint64 transcriptId);

private:
  void releaseSession(pcm::calltranscription::TranscriptionSession *session);
  std::shared_ptr<pcm::database::Database> mDb;
  QPointer<TranscriptPanel> mPanel;
  QPointer<CallSidePanel> mSidePanel;
  ControllerHooks mHooks;
  QPointer<pcm::video::VideoSession> mCall;
  std::optional<int64_t> mCallEvent;
  pcm::calltranscription::TranscriptionSession *mSession = nullptr;
  QMetaObject::Connection mCallStateConnection;
  QMetaObject::Connection mCallDestroyedConnection;
  bool mStartAvailable = false;
  bool mShutdown = false;
  bool mInRevokeCallback = false;
  bool mSessionHadFailure = false;
  quint64 mAttachmentGeneration = 0;
};
} // namespace pcm::transcriptionui
