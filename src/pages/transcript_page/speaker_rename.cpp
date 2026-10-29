#include "speaker_rename.h"

#include <QCoreApplication>
#include <QInputDialog>
#include <QLabel>
#include <QMenu>

namespace pcm::transcriptionui {
namespace {
// Keeps names well inside the prompter's 200-character speaker limit.
constexpr int kMaxSpeakerName = 100;
}

std::optional<QString> askSpeakerName(QWidget *parent, const SpeakerNamePrompt &prompt,
                                      const QString &current) {
  std::optional<QString> answer;
  if (prompt) {
    answer = prompt(current);
  } else {
    bool ok = false;
    const auto value = QInputDialog::getText(parent, QCoreApplication::translate("SpeakerRename", "Rename speaker"),
        QCoreApplication::translate("SpeakerRename", "Name shown for this speaker in the whole transcript:"), QLineEdit::Normal, current, &ok);
    if (ok) answer = value;
  }
  if (!answer) return std::nullopt;
  const auto name = answer->simplified().left(kMaxSpeakerName);
  if (name.isEmpty() || name == current) return std::nullopt;
  return name;
}

void offerSpeakerRename(QLabel *speaker, std::function<void()> rename) {
  speaker->setContextMenuPolicy(Qt::CustomContextMenu);
  speaker->setToolTip(QCoreApplication::translate("SpeakerRename", "Right-click to rename this speaker"));
  QObject::connect(speaker, &QLabel::customContextMenuRequested, speaker,
                   [speaker, rename = std::move(rename)](const QPoint &position) {
    QMenu menu(speaker);
    menu.addAction(QCoreApplication::translate("SpeakerRename", "Rename speaker…"), speaker, rename);
    menu.exec(speaker->mapToGlobal(position));
  });
}
}
