#pragma once

#include "database.h"
#include <QObject>
#include <QPointer>
#include <QVariantList>
#include <QVariantMap>
#include <QJsonArray>
#include <QJsonObject>
#include <functional>
#include <memory>
#include <map>

class CallNotesController;
class PrompterPanel;
class QQuickTextDocument;
class QTextDocument;
class QWidget;

// One adapter per workspace; every viewed material owns its controller and
// document. Background AI updates never replace an in-progress entry edit.
class NotesWorkspaceBridge final : public QObject {
  Q_OBJECT
  Q_PROPERTY(QVariantMap state READ state NOTIFY stateChanged)
  Q_PROPERTY(QVariantList history READ history NOTIFY stateChanged)
  Q_PROPERTY(QVariantList notes READ notes NOTIFY stateChanged)
  Q_PROPERTY(QVariantList phrases READ phrases NOTIFY stateChanged)
  Q_PROPERTY(QString saveStatus READ saveStatus NOTIFY editorStatusChanged)
  Q_PROPERTY(bool editing READ editing NOTIFY stateChanged)
  Q_PROPERTY(QString currentMaterialId READ currentMaterialId NOTIFY stateChanged)
  Q_PROPERTY(bool editorDirty READ editorDirty NOTIFY editorStatusChanged)
  Q_PROPERTY(QString editorNoteId READ editorNoteId NOTIFY stateChanged)
public:
  NotesWorkspaceBridge(std::shared_ptr<pcm::database::Database> db, CallNotesController *controller,
                       QWidget *dialogParent, QObject *parent = nullptr);
  ~NotesWorkspaceBridge() override;
  QVariantMap state() const;
  QVariantList history() const;
  QVariantList notes() const;
  QVariantList phrases() const;
  QString saveStatus() const { return mSaveStatus; }
  bool editing() const;
  QString currentMaterialId() const;
  bool editorDirty() const;
  QString editorNoteId() const { return mSessionEntry["id"].toString(); }
  void beginEditorSession();
  Q_INVOKABLE bool selectEditorNote(const QString &id);
  Q_INVOKABLE bool saveEditorSession();
  void setClient(const std::optional<DuckClient> &client);
  void setMeeting(const QString &details, const QString &status);
  void setAgreements(const QJsonArray &agreements);
  void setPrompterPanel(PrompterPanel *panel);
  void setControllerFactory(std::function<CallNotesController *(qint64, QObject *)> factory) { mFactory = std::move(factory); }
  Q_INVOKABLE void attachDocument(QQuickTextDocument *document);
  Q_INVOKABLE void selectMaterial(const QString &id);
  Q_INVOKABLE void editNote(const QString &id);
  Q_INVOKABLE void cancelEdit();
  Q_INVOKABLE bool saveNote();
  Q_INVOKABLE void format(int start, int end, const QString &kind);
  Q_INVOKABLE void attachFiles();
  Q_INVOKABLE void removeAttachment(int index);
  Q_INVOKABLE void openAttachment(const QString &entry, const QString &attachment);
  Q_INVOKABLE void appendSummary();
  Q_INVOKABLE void showSource(const QString &id);
  Q_INVOKABLE void retrySummary();
  Q_INVOKABLE void exportDocument();
  Q_INVOKABLE void startPrompter();
  Q_INVOKABLE void pausePrompter();
  Q_INVOKABLE void dismissPrompterHint();
  Q_INVOKABLE void prompterCardAction(const QString &cardId, const QString &action);
  Q_INVOKABLE void showPrompterSource(const QString &id);
signals:
  void stateChanged();
  void editorStatusChanged();
  void documentActivated();
  void sourceRequested(qint64 phraseId);
  void retryRequested();
private:
  struct Context;
  Context *context() const;
  Context *ensureContext(CallNotesController *controller);
  void refresh();
  void activateDocument();
  QTextDocument *activeDocument() const;
  void documentEdited(Context *context);
  std::shared_ptr<pcm::database::Database> mDb;
  QPointer<CallNotesController> mLive;
  QPointer<PrompterPanel> mPrompter;
  QPointer<QWidget> mDialogParent;
  QPointer<QQuickTextDocument> mQuickDocument;
  std::map<qint64, std::unique_ptr<Context>> mContexts;
  qint64 mSelected = 0;
  std::optional<DuckClient> mClient;
  QString mMeeting, mMeetingStatus, mSaveStatus;
  QJsonArray mAgreements;
  bool mLoading = false, mMutating = false;
  QTextDocument *mSessionDocument = nullptr;
  bool mSessionReady = false;
  QJsonObject mSessionEntry;
  QString mSessionOriginal, mSessionDraft;
  QStringList mSessionFiles, mSessionOriginalFiles;
  std::function<CallNotesController *(qint64, QObject *)> mFactory;
};
