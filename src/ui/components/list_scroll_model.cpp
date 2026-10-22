/**
 * @file list_scroll_model.cpp
 * @brief Implementation of the pure list selection/scroll model (slice-0062).
 */

#include "list_scroll_model.h"

namespace adversary {

namespace {
int clampInt(int value, int lo, int hi) {
    if (value < lo) return lo;
    if (value > hi) return hi;
    return value;
}
}  // namespace

void ListScrollModel::setItemCount(int count) {
    itemCount_ = count < 0 ? 0 : count;
    const int lastIndex = itemCount_ > 0 ? itemCount_ - 1 : 0;
    selectedIndex_ = clampInt(selectedIndex_, 0, lastIndex);
    snapSelectionToSelectable();  // a rebuild may leave the selection on a now-rejected row
    clampOffsetToSelection();
}

void ListScrollModel::setViewport(int visibleRows) {
    // A zero/negative viewport would make the window math meaningless; one row always fits.
    visibleRows_ = visibleRows < 1 ? 1 : visibleRows;
    clampOffsetToSelection();
}

void ListScrollModel::setSelectablePredicate(std::function<bool(int)> predicate) {
    selectable_ = std::move(predicate);
}

bool ListScrollModel::isSelectable(int i) const {
    if (i < 0 || i >= itemCount_) return false;
    return selectable_ ? selectable_(i) : true;
}

int ListScrollModel::seekSelectable(int from, int direction) const {
    if (itemCount_ == 0) return -1;
    int current = from;
    if (current < 0) current = itemCount_ - 1;
    if (current >= itemCount_) current = 0;
    for (int step = 0; step < itemCount_; ++step) {
        if (isSelectable(current)) return current;
        current += direction;
        if (current < 0) current = itemCount_ - 1;
        if (current >= itemCount_) current = 0;
    }
    return -1;  // nothing selectable: the caller leaves the selection where it was
}

void ListScrollModel::snapSelectionToSelectable() {
    const int sel = seekSelectable(selectedIndex_, 1);
    if (sel >= 0) selectedIndex_ = sel;
}

void ListScrollModel::moveUp() {
    if (itemCount_ == 0) return;
    const int next = seekSelectable(selectedIndex_ - 1, -1);
    if (next >= 0) selectedIndex_ = next;  // leave put when nothing is selectable
    clampOffsetToSelection();
}

void ListScrollModel::moveDown() {
    if (itemCount_ == 0) return;
    const int next = seekSelectable(selectedIndex_ + 1, 1);
    if (next >= 0) selectedIndex_ = next;
    clampOffsetToSelection();
}

void ListScrollModel::setSelectedIndex(int index) {
    if (itemCount_ == 0) return;
    selectedIndex_ = clampInt(index, 0, itemCount_ - 1);
    snapSelectionToSelectable();  // a jump onto a rejected row snaps to the nearest selectable
    clampOffsetToSelection();
}

void ListScrollModel::clampOffsetToSelection() {
    // Selection above the window: pull the window up to it.
    if (selectedIndex_ < scrollOffset_) {
        scrollOffset_ = selectedIndex_;
    }
    // Selection below the window: pull the window down so it is the last visible row.
    if (selectedIndex_ >= scrollOffset_ + visibleRows_) {
        scrollOffset_ = selectedIndex_ - visibleRows_ + 1;
    }
    // Never scroll past the point where the last item sits on the bottom row (and never below 0,
    // so a list that fits entirely stays pinned at the top with no empty lead-in).
    const int maxOffset = itemCount_ > visibleRows_ ? itemCount_ - visibleRows_ : 0;
    scrollOffset_ = clampInt(scrollOffset_, 0, maxOffset);
}

int ListScrollModel::thumbHeightPx(int trackPx) const {
    if (!isScrollable()) return trackPx;
    const int proportional = trackPx * visibleRows_ / itemCount_;
    const int clamped = proportional < MIN_THUMB_PX ? MIN_THUMB_PX : proportional;
    return clamped > trackPx ? trackPx : clamped;
}

int ListScrollModel::thumbTopPx(int trackPx) const {
    if (!isScrollable()) return 0;
    const int maxOffset = itemCount_ - visibleRows_;  // > 0 here, so the divide is safe
    const int travel = trackPx - thumbHeightPx(trackPx);
    const int top = travel * scrollOffset_ / maxOffset;
    return clampInt(top, 0, travel);
}

} // namespace adversary
