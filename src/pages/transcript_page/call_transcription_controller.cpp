#include "call_transcription_controller.h"
#include "call_side_panel.h"
#include "failure_text.h"
#include "transcript_panel.h"

namespace pcm::transcriptionui {
using pcm::calltranscription::SessionState;
using pcm::calltranscription::TranscriptionSession;
CallTranscriptionController::CallTranscriptionController(
    std::shared_ptr<pcm::database::Database> db, TranscriptPanel *panel,
    CallSidePanel *sidePanel, ControllerHooks hooks, QObject *parent)
    : QObject(parent), mDb(std::move(db)), mPanel(panel), mSidePanel(sidePanel),
      mHooks(std::move(hooks)) {
  connect(panel, &TranscriptPanel::startRequested, this,
          &CallTranscriptionController::onTranscribeRequested);
  connect(panel, &TranscriptPanel::stopRequested, this, [this] {
    if (mSession)
      mSession->stop();
  });
  connect(panel, &TranscriptPanel::revokeRequested, this, [this] {
    if (mSession)
      mSession->revoke();
  });
  refreshAvailability();
}
CallTranscriptionController::~CallTranscriptionController() { shutdown(); }
void CallTranscriptionController::attachCall(pcm::video::VideoSession *call,
                                             std::optional<int64_t> event) {
  detachCall();
  if (mShutdown)
    return;
  mCall = call;
  mCallEvent = event;
  if (call) {
    mCallStateConnection =
        connect(call, &pcm::video::VideoSession::stateChanged, this,
                [this](pcm::video::VideoSessionState state) {
                  if (state == pcm::video::VideoSessionState::Leaving ||
                      state == pcm::video::VideoSessionState::Ended ||
                      state == pcm::video::VideoSessionState::Failed)
                    detachCall();
                });
    mCallDestroyedConnection =
        connect(call, &QObject::destroyed, this, [this] { detachCall(); });
  }
  refreshAvailability();
}
void CallTranscriptionController::detachCall() {
  ++mAttachmentGeneration;
  disconnect(mCallStateConnection);
  disconnect(mCallDestroyedConnection);
  mCall.clear();
  mCallEvent.reset();
  // VideoSession ending does not necessarily emit VideoProvider::left.
  if (mSession)
    mSession->stop();
  refreshAvailability();
}
void CallTranscriptionController::refreshAvailability() {
  const bool enabled =
      mHooks.transcriptionEnabled && mHooks.transcriptionEnabled();
  const bool models = mHooks.modelsAvailable && mHooks.modelsAvailable();
  QString reason;
  if (!enabled)
    reason = tr("Transcription is turned off in Settings");
  else if (!models)
    reason = tr("Speech models are not installed");
  else if (!mCall)
    reason = tr("Open a call to transcribe it");
  else if (mSession)
    reason = tr("Wait for the current transcription to finish");
  mStartAvailable = !mShutdown && reason.isEmpty();
  if (mPanel)
    mPanel->setStartAvailable(mStartAvailable, reason);
  if (mSidePanel)
    mSidePanel->setTranscriptionAvailable(enabled && models);
}
void CallTranscriptionController::releaseSession(
    TranscriptionSession *session) {
  if (session != mSession)
    return;
  disconnect(session, nullptr, this, nullptr);
  connect(session, &QObject::destroyed, this, [this, session] {
    if (mSession != session)
      return;
    mSession = nullptr;
    refreshAvailability();
  });
  // A terminal signal is emitted inside the session's own call stack.
  session->deleteLater();
  refreshAvailability();
  emit transcribeButtonState(false, tr("Transcribe call"));
}
void CallTranscriptionController::onTranscribeRequested() {
  if (mSession) {
    if (mPanel)
      mPanel->setNotice(tr("Wait for the current transcription to finish"));
    return;
  }
  refreshAvailability();
  if (!mStartAvailable || !mHooks.askConsent)
    return;
  const auto generation = mAttachmentGeneration;
  const auto original = mCallEvent;
  QPointer<CallTranscriptionController> self(this);
  const auto consentHook = mHooks.askConsent;
  if (!consentHook() || !self)
    return;
  // A modal hook may process call-ended events while consent is being asked.
  refreshAvailability();
  if (!mStartAvailable || generation != mAttachmentGeneration)
    return;
  const auto resolveHook = mHooks.resolveEvent;
  const auto event = original && resolveHook ? resolveHook(*original) : std::nullopt;
  if (!self || generation != mAttachmentGeneration || mShutdown)
    return;
  if (original && (!event || *event <= 0)) {
    if (mPanel)
      mPanel->setError(
          tr("Unable to open the calendar event for transcription"));
    return;
  }
  const auto materialisedHook = mHooks.eventMaterialised;
  if (original && *original < 0 && materialisedHook)
    materialisedHook(*event);
  if (!self || generation != mAttachmentGeneration || !mCall || mShutdown)
    return;
  const auto factoryHook = mHooks.engineFactory;
  const auto factory = factoryHook ? factoryHook()
                                  : pcm::calltranscription::EngineFactory{};
  if (!self || generation != mAttachmentGeneration || !mCall || mShutdown)
    return;
  auto *session = new TranscriptionSession(
      mDb, mCall->provider(), factory, this);
  mSession = session;
  mSessionHadFailure = false;
  if (mPanel) {
    mPanel->clearPhrases();
    mPanel->setError({});
    mPanel->setNotice({});
    mPanel->setDelayed(false);
    mPanel->setAudioGap(false);
  }
  connect(session, &TranscriptionSession::stateChanged, this,
          [this, session](SessionState state) {
            if (mSession == session && mPanel)
              mPanel->setState(mSessionHadFailure &&
                                       state == SessionState::Finished
                                   ? SessionState::Failed
                                   : state);
          });
  connect(session, &TranscriptionSession::phraseAdded, this,
          [this, session](DuckTranscriptPhrase phrase) {
            if (mSession == session && mPanel)
              mPanel->addPhrase(phrase);
          });
  connect(session, &TranscriptionSession::delayedChanged, this,
          [this, session](bool delayed) {
            if (mSession == session && mPanel)
              mPanel->setDelayed(delayed);
          });
  connect(session, &TranscriptionSession::audioGapChanged, this,
          [this, session](bool interrupted) {
            if (mSession == session && mPanel)
              mPanel->setAudioGap(interrupted);
          });
  connect(session, &TranscriptionSession::trackFailed, this,
          [this, session](const QString &) {
            if (mSession == session && mPanel)
              mPanel->setNotice(
                  tr("Some participant audio could not be transcribed"));
          });
  connect(session, &TranscriptionSession::failed, this,
          [this, session](const QString &raw) {
            if (mSession != session)
              return;
            mSessionHadFailure = true;
            if (mPanel) {
              mPanel->setError(userFacingFailure(raw));
              mPanel->setDelayed(false);
            }
            if (session->state() == SessionState::Failed) {
              releaseSession(session);
            } else {
              // Writer errors are nonterminal failed signals. Detach the sink
              // now, and retain the session until its engine and writer have
              // finished.
              session->stop();
            }
          });
  connect(session, &TranscriptionSession::finished, this,
          [this, session](qint64 id) {
            if (mSession != session)
              return;
            if (mPanel)
              mPanel->setDelayed(false);
            releaseSession(session);
            emit transcriptReady(id);
          });
  connect(session, &TranscriptionSession::revoked, this,
          [this, session](qint64 id) {
            if (mSession != session)
              return;
            QPointer<TranscriptionSession> guard(session);
            QPointer<CallTranscriptionController> self(this);
            mInRevokeCallback = true;
            // The session has joined its writer before this terminal signal.
            bool saved = false;
            try {
              saved = mDb->set_transcript_status(id, "draft");
            } catch (...) {
            }
            const auto choiceHook = mHooks.askRevokeChoice;
            const auto choice =
                choiceHook
                    ? choiceHook()
                    : ControllerHooks::RevokeChoice::KeepAsDraft;
            if (!self)
              return;
            mInRevokeCallback = false;
            if (!guard || mSession != session)
              return;
            if (choice == ControllerHooks::RevokeChoice::DeleteRecorded) {
              bool deleted = false;
              try {
                deleted = mDb->delete_transcript(id);
              } catch (...) {
              }
              if (deleted && mPanel)
                mPanel->clearPhrases();
              if (!deleted && mPanel) {
                mPanel->setError(tr("Unable to delete the transcript"));
                mPanel->setState(SessionState::Failed);
              }
            } else if (!saved && mPanel) {
              mPanel->setError(tr("Unable to save the transcript"));
              mPanel->setState(SessionState::Failed);
            }
            if (mPanel)
              mPanel->setDelayed(false);
            releaseSession(session);
          });
  if (!session->start(event, "live_local_v1")) {
    if (mPanel)
      mPanel->setError(tr("Unable to start transcription"));
    releaseSession(session);
    return;
  }
  refreshAvailability();
  emit requestOpenTranscriptTab();
  emit transcribeButtonState(true, tr("Transcription running"));
}
void CallTranscriptionController::shutdown() {
  if (mShutdown)
    return;
  mShutdown = true;
  disconnect(mCallStateConnection);
  disconnect(mCallDestroyedConnection);
  mCall.clear();
  mCallEvent.reset();
  if (mSession) {
    auto *session = mSession;
    mSession = nullptr;
    disconnect(session, nullptr, this, nullptr);
    if (mInRevokeCallback) {
      session->setParent(nullptr);
      session->deleteLater();
    } else
      delete session;
  }
  refreshAvailability();
}
} // namespace pcm::transcriptionui
