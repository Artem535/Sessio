#pragma once

class QComboBox;

namespace pcm::widgets {

// For combos listing devices, whose names ("Динамики (Realtek(R) Audio)") are
// often longer than the box. The box keeps a modest width and elides its
// label (the style does that); the drop-down list grows to the longest entry,
// up to the screen width, and every entry carries its full name as a tooltip.
// Call again after repopulating.
void fitComboToLongItems(QComboBox *combo);

} // namespace pcm::widgets
