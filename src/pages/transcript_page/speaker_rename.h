#pragma once

#include <QString>
#include <functional>
#include <optional>

class QLabel;
class QWidget;

namespace pcm::transcriptionui {
// Asks for a speaker name given the current label; nullopt when cancelled.
using SpeakerNamePrompt = std::function<std::optional<QString>(const QString &current)>;
// Trimmed and bounded new name, or nullopt when cancelled, blank or unchanged.
// Uses prompt when set, otherwise a modal input dialog over parent.
std::optional<QString> askSpeakerName(QWidget *parent, const SpeakerNamePrompt &prompt,
                                      const QString &current);
// Gives the speaker label a "Rename speaker…" context menu that runs rename.
void offerSpeakerRename(QLabel *speaker, std::function<void()> rename);
}
