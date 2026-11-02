#pragma once

#include <QColor>

namespace pcm::widgets::constants {
    constexpr auto kFontSize = 15;
    constexpr auto kPageContentMargin = 9;
    constexpr auto kPanelPadding = 10;
    constexpr auto kCardCornerRadius = 14;
    constexpr auto kCircleDiameter = 10;
    constexpr auto kCircleMargin = kCircleDiameter / 2;
    constexpr auto kNotesHeaderHorizontalPadding = 16;
    constexpr auto kNotesHeaderVerticalPadding = 14;
    constexpr auto kNotesFeedItemSpacing = 12;
    constexpr auto kNotesComposerSpacing = 10;
    constexpr auto kNotesBubbleMaxWidth = 980;
    constexpr auto kNotesBubbleHorizontalPadding = 14;
    constexpr auto kNotesBubbleVerticalPadding = 12;
    constexpr auto kNotesAttachmentPreviewMaxWidth = 320;
    constexpr auto kNotesAttachmentPreviewMaxHeight = 220;
    constexpr auto kNotesDocumentWidthInset = 32;
    constexpr auto kNotesBodyHeightExtra = 10;
    inline const QColor kSurfaceBorderColor = QColor(255, 255, 255, 31);
    inline const QColor kSurfaceBackgroundColor = QColor(255, 255, 255, 5);
    inline const QColor kCalendarCardBorderColor = QColor(255, 255, 255, 0);
    inline const QColor kCalendarCardBackgroundColor = QColor(255, 255, 255, 8);
} // namespace pcm::widgets::constants
