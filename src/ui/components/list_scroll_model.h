/**
 * @file list_scroll_model.h
 * @brief Pure selection + scroll-window state for a vertically scrolling list (slice-0062).
 *
 * Display-free core shared by every selection list in the UI. The renderer owns the layout,
 * so it is the only thing that knows how many rows physically fit; it feeds that viewport
 * height in here each frame (setViewport), and this model keeps the selection inside the
 * window, wraps at the ends, and reports the scrollbar thumb geometry. No Canvas, no Arduino
 * — it compiles and is unit-tested on the host.
 *
 * Extracted from 8+ hand-rolled lists whose private copies of this logic drifted and shipped
 * two opposite bugs: a selection drawn off-screen because the scroll window was wider than the
 * rendered rows (commit 158246b), and a list that scrolled before anything was off-screen
 * because a hardcoded visible-count sat below the rendered rows (commit a2417c8). Making the
 * viewport a render-supplied input removes the second copy that could drift.
 */

#pragma once

#include <cstdint>
#include <functional>

namespace adversary {

class ListScrollModel {
public:
    /// Minimum scrollbar thumb height in pixels, so the thumb stays visible/grabbable even
    /// when the visible fraction is tiny. Lives here (not in the renderer) because it is part
    /// of the thumb geometry this model computes and unit-tests.
    static constexpr int MIN_THUMB_PX = 6;

    /// Set the number of items. Keeps the current selection where it still exists, otherwise
    /// clamps it into range; re-clamps the scroll offset. A count of 0 parks selection at 0.
    void setItemCount(int count);

    /// Set how many rows the viewport can show (the renderer's live row budget). Re-clamps the
    /// scroll offset so the selection stays visible even when the viewport shrank this frame.
    void setViewport(int visibleRows);

    /// Install a predicate deciding which indices the cursor may rest on; an index the predicate
    /// rejects is skipped by moveUp/moveDown and snapped off by setSelectedIndex/setItemCount
    /// (separators, live-disabled rows). An empty predicate (the default) means every index is
    /// selectable, so a list that sets none behaves exactly as before (slice-0105, ADR-0104).
    /// The predicate closes over the owning screen's item list; the screen owns both and must
    /// keep the predicate valid for this model's lifetime. setViewport never snaps — a row that
    /// becomes non-selectable while selected stays selected until the next nav or rebuild.
    void setSelectablePredicate(std::function<bool(int)> predicate);

    /// Move the selection one row up; wraps from the first item to the last.
    void moveUp();
    /// Move the selection one row down; wraps from the last item to the first.
    void moveDown();

    /// Jump to an explicit index (clamped into range) and scroll it into view.
    void setSelectedIndex(int index);

    int itemCount() const { return itemCount_; }
    int selectedIndex() const { return selectedIndex_; }
    int scrollOffset() const { return scrollOffset_; }
    int visibleRows() const { return visibleRows_; }

    /// True when there are more items than fit — i.e. a scrollbar should be drawn.
    bool isScrollable() const { return itemCount_ > visibleRows_; }

    /// Thumb height for a track `trackPx` tall: proportional to the visible fraction, clamped
    /// to MIN_THUMB_PX and never taller than the track. Returns the full track height when the
    /// list is not scrollable.
    int thumbHeightPx(int trackPx) const;

    /// Thumb top offset within a track `trackPx` tall: proportional to the scroll offset over
    /// the scrollable range, clamped so the thumb never runs past the track end. Returns 0 when
    /// the list is not scrollable.
    int thumbTopPx(int trackPx) const;

private:
    /// Pull the scroll offset so the current selection sits inside [offset, offset+visibleRows),
    /// then clamp it to the valid range. The single rule both bugs above violated a copy of.
    void clampOffsetToSelection();

    /// True when index `i` is in range and the selectable predicate accepts it (or none is set).
    bool isSelectable(int i) const;
    /// First selectable index at or after `from`, searching in `direction` (+1/-1) and wrapping,
    /// up to itemCount_ steps; -1 when no index is selectable. The loop-termination guarantee.
    int seekSelectable(int from, int direction) const;
    /// Snap the current selection to the nearest selectable index (forward, wrapping); leaves it
    /// put when nothing is selectable. Used after a jump or a list rebuild, never per-frame.
    void snapSelectionToSelectable();

    int itemCount_ = 0;
    int selectedIndex_ = 0;
    int scrollOffset_ = 0;
    int visibleRows_ = 1;  // at least 1; the renderer overwrites this every frame
    std::function<bool(int)> selectable_;  // empty = every index selectable
};

} // namespace adversary
