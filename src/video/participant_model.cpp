#include "participant_model.h"
#include <QThread>
#include <algorithm>

namespace pcm::video {

ParticipantModel::ParticipantModel(QObject *parent) : QAbstractListModel(parent) {}

int ParticipantModel::rowCount(const QModelIndex &parent) const {
  return parent.isValid() ? 0 : static_cast<int>(mParticipants.size());
}

QVariant ParticipantModel::data(const QModelIndex &index, int role) const {
  if (!index.isValid() || index.model() != this || index.column() != 0 ||
      index.row() < 0 || index.row() >= mParticipants.size())
    return {};
  const auto &p = mParticipants.at(index.row());
  switch (role) {
  case IdRole: return p.id;
  case Qt::DisplayRole:
  case DisplayNameRole: return p.displayName;
  case ParticipantRole: return p.role;
  case IsLocalRole: return p.isLocal;
  case MicrophoneEnabledRole: return p.microphoneEnabled;
  case CameraEnabledRole: return p.cameraEnabled;
  case ScreenSharingRole: return p.screenSharing;
  default: return {};
  }
}

QHash<int, QByteArray> ParticipantModel::roleNames() const {
  return {{IdRole, "id"}, {DisplayNameRole, "displayName"}, {ParticipantRole, "role"},
          {IsLocalRole, "isLocal"}, {MicrophoneEnabledRole, "microphoneEnabled"},
          {CameraEnabledRole, "cameraEnabled"}, {ScreenSharingRole, "screenSharing"}};
}

void ParticipantModel::updateRemoteCount(int previous) {
  mRemoteCount = static_cast<int>(std::count_if(mParticipants.cbegin(), mParticipants.cend(),
                                             [](const Participant &p) { return !p.isLocal; }));
  if (previous != mRemoteCount)
    emit remoteCountChanged(mRemoteCount);
}

void ParticipantModel::upsert(const Participant &participant) {
  Q_ASSERT(QThread::currentThread() == thread());
  const int previous = mRemoteCount;
  for (int row = 0; row < mParticipants.size(); ++row) {
    if (mParticipants[row].id != participant.id)
      continue;
    if (mParticipants[row] == participant)
      return;
    mParticipants[row] = participant;
    updateRemoteCount(previous);
    emit dataChanged(index(row), index(row), {DisplayNameRole, ParticipantRole, IsLocalRole,
                                            MicrophoneEnabledRole, CameraEnabledRole, ScreenSharingRole,
                                            Qt::DisplayRole});
    return;
  }
  const int row = rowCount();
  beginInsertRows({}, row, row);
  mParticipants.append(participant);
  mRemoteCount += !participant.isLocal;
  endInsertRows();
  updateRemoteCount(previous);
}

void ParticipantModel::remove(const QString &id) {
  Q_ASSERT(QThread::currentThread() == thread());
  for (int row = 0; row < mParticipants.size(); ++row) {
    if (mParticipants[row].id != id)
      continue;
    const int previous = mRemoteCount;
    beginRemoveRows({}, row, row);
    mRemoteCount -= !mParticipants[row].isLocal;
    mParticipants.removeAt(row);
    endRemoveRows();
    updateRemoteCount(previous);
    return;
  }
}

void ParticipantModel::clear() {
  Q_ASSERT(QThread::currentThread() == thread());
  if (mParticipants.isEmpty())
    return;
  const int previous = mRemoteCount;
  beginResetModel();
  mParticipants.clear();
  mRemoteCount = 0;
  endResetModel();
  updateRemoteCount(previous);
}

std::optional<Participant> ParticipantModel::participant(const QString &id) const {
  for (const auto &p : mParticipants) {
    if (p.id == id)
      return p;
  }
  return std::nullopt;
}

} // namespace pcm::video
