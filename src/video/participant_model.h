#pragma once

#include <QAbstractListModel>
#include <QString>
#include <QVector>
#include <optional>

namespace pcm::video {

struct Participant {
  QString id;
  QString displayName;
  QString role;
  bool isLocal{false};
  bool microphoneEnabled{true};
  bool cameraEnabled{true};
  bool screenSharing{false};
  bool operator==(const Participant &) const = default;
};

// Mutations occur on the Qt owner thread. Providers marshal SDK values here.
class ParticipantModel final : public QAbstractListModel {
  Q_OBJECT
public:
  enum Role {
    IdRole = Qt::UserRole + 1,
    DisplayNameRole,
    ParticipantRole,
    IsLocalRole,
    MicrophoneEnabledRole,
    CameraEnabledRole,
    ScreenSharingRole,
  };
  Q_ENUM(Role)
  explicit ParticipantModel(QObject *parent = nullptr);
  int rowCount(const QModelIndex &parent = {}) const override;
  QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
  QHash<int, QByteArray> roleNames() const override;
  void upsert(const Participant &participant);
  void remove(const QString &id);
  void clear();
  [[nodiscard]] std::optional<Participant> participant(const QString &id) const;
  [[nodiscard]] int remoteCount() const { return mRemoteCount; }

signals:
  void remoteCountChanged(int count);

private:
  QVector<Participant> mParticipants;
  int mRemoteCount{0};
  void updateRemoteCount(int previous);
};

} // namespace pcm::video
