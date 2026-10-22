/**
 * @file test_list_scroll_model.cpp
 * @brief Regression for the shared list selection/scroll model (slice-0062).
 *
 * Each test encodes a rule a hand-rolled list previously got wrong. The two headline bugs:
 *  - the wardriving off-screen selection (scroll window wider than the rendered rows, 158246b)
 *    -> test_down_on_last_visible_row_advances_window / test_up_scrolls_window_back_into_view;
 *  - the attack-config premature scroll (hardcoded visible-count below the rendered rows,
 *    a2417c8) -> test_count_fits_viewport_never_scrolls / test_shrinking_viewport_reclamps.
 * Plus the wrap policy and the scrollbar-thumb geometry the component now owns.
 */

#include <unity.h>
#include <algorithm>
#include <vector>
#include "ui/components/list_scroll_model.h"

using namespace adversary;

// A list of 10 items in a 4-row viewport — the canonical "more than fits" case.
static ListScrollModel makeList(int count, int viewport) {
    ListScrollModel model;
    model.setItemCount(count);
    model.setViewport(viewport);
    return model;
}

// A list whose `rejected` indices are non-selectable (separators / live-disabled rows), with
// the selection positioned on the first selectable row — the shape the category submenus take
// once routed through the model (slice-0105).
static ListScrollModel makeListWithRejected(int count, int viewport, std::vector<int> rejected) {
    ListScrollModel model = makeList(count, viewport);
    model.setSelectablePredicate([rejected](int i) {
        return std::find(rejected.begin(), rejected.end(), i) == rejected.end();
    });
    model.setSelectedIndex(0);
    return model;
}

void setUp() {}
void tearDown() {}

// --- scroll window follows the selection -------------------------------------------------

void test_down_on_last_visible_row_advances_window() {
    ListScrollModel m = makeList(10, 4);  // rows 0..3 visible, selection 0
    m.moveDown(); m.moveDown(); m.moveDown();  // selection 3, still the last visible row
    TEST_ASSERT_EQUAL_INT(3, m.selectedIndex());
    TEST_ASSERT_EQUAL_INT(0, m.scrollOffset());

    m.moveDown();  // selection 4 — would be off-screen; window must advance
    TEST_ASSERT_EQUAL_INT(4, m.selectedIndex());
    TEST_ASSERT_EQUAL_INT(1, m.scrollOffset());
    // The selection is inside [offset, offset+viewport).
    TEST_ASSERT_TRUE(m.selectedIndex() >= m.scrollOffset());
    TEST_ASSERT_TRUE(m.selectedIndex() < m.scrollOffset() + m.visibleRows());
}

void test_up_scrolls_window_back_into_view() {
    ListScrollModel m = makeList(10, 4);
    m.setSelectedIndex(6);  // forces window to 3..6 (offset 3)
    TEST_ASSERT_EQUAL_INT(3, m.scrollOffset());

    m.moveUp(); m.moveUp(); m.moveUp();  // selection 3 — still visible, no scroll yet
    TEST_ASSERT_EQUAL_INT(3, m.selectedIndex());
    TEST_ASSERT_EQUAL_INT(3, m.scrollOffset());

    m.moveUp();  // selection 2 — above the window, it must follow up
    TEST_ASSERT_EQUAL_INT(2, m.selectedIndex());
    TEST_ASSERT_EQUAL_INT(2, m.scrollOffset());
}

// --- a list that fits never scrolls ------------------------------------------------------

void test_count_fits_viewport_never_scrolls() {
    ListScrollModel m = makeList(4, 7);  // 4 items, 7 rows fit — nothing is off-screen
    TEST_ASSERT_FALSE(m.isScrollable());
    for (int i = 0; i < 6; ++i) m.moveDown();  // churn past the end a few times
    TEST_ASSERT_EQUAL_INT(0, m.scrollOffset());
    TEST_ASSERT_FALSE(m.isScrollable());
}

void test_shrinking_viewport_reclamps_offset() {
    ListScrollModel m = makeList(10, 7);
    m.setSelectedIndex(9);            // bottom; offset becomes 3 (3..9)
    TEST_ASSERT_EQUAL_INT(3, m.scrollOffset());

    m.setViewport(4);                 // layout now only draws 4 rows
    // Selection 9 must still be visible: offset re-clamps to 6 (6..9).
    TEST_ASSERT_EQUAL_INT(6, m.scrollOffset());
    TEST_ASSERT_TRUE(m.selectedIndex() < m.scrollOffset() + m.visibleRows());
}

// --- wrap at the ends --------------------------------------------------------------------

void test_up_on_first_wraps_to_last_and_shows_it() {
    ListScrollModel m = makeList(10, 4);  // selection 0, offset 0
    m.moveUp();                            // wrap to item 9
    TEST_ASSERT_EQUAL_INT(9, m.selectedIndex());
    // The wrapped selection must be on screen: offset 6 shows rows 6..9.
    TEST_ASSERT_EQUAL_INT(6, m.scrollOffset());
    TEST_ASSERT_TRUE(m.selectedIndex() < m.scrollOffset() + m.visibleRows());
}

void test_down_on_last_wraps_to_first() {
    ListScrollModel m = makeList(10, 4);
    m.setSelectedIndex(9);                 // bottom
    TEST_ASSERT_EQUAL_INT(6, m.scrollOffset());
    m.moveDown();                          // wrap to item 0
    TEST_ASSERT_EQUAL_INT(0, m.selectedIndex());
    TEST_ASSERT_EQUAL_INT(0, m.scrollOffset());
}

// --- scrollbar thumb geometry ------------------------------------------------------------

void test_thumb_height_is_proportional_with_minimum() {
    ListScrollModel big = makeList(100, 5);  // visible fraction 5/100 -> 5px of a 100px track
    // Proportional would be 5px, below the 6px floor, so it clamps up to the minimum.
    TEST_ASSERT_EQUAL_INT(ListScrollModel::MIN_THUMB_PX, big.thumbHeightPx(100));

    ListScrollModel half = makeList(10, 5);  // half the list fits -> half the track
    TEST_ASSERT_EQUAL_INT(50, half.thumbHeightPx(100));

    // A list that fits gets the full track (renderer won't draw it, but the math is defined).
    ListScrollModel fits = makeList(3, 5);
    TEST_ASSERT_EQUAL_INT(100, fits.thumbHeightPx(100));
}

void test_thumb_top_tracks_offset() {
    ListScrollModel m = makeList(10, 5);  // maxOffset 5, thumb height 50 on a 100px track
    TEST_ASSERT_EQUAL_INT(0, m.thumbTopPx(100));        // top of list -> top of track

    m.setSelectedIndex(9);                               // bottom -> offset 5 (max)
    TEST_ASSERT_EQUAL_INT(5, m.scrollOffset());
    // travel = 100 - 50 = 50, at max offset the thumb sits flush with the track bottom.
    TEST_ASSERT_EQUAL_INT(50, m.thumbTopPx(100));
    TEST_ASSERT_TRUE(m.thumbTopPx(100) + m.thumbHeightPx(100) <= 100);
}

// --- selectable-row predicate (slice-0105, ADR-0104) -------------------------------------

void test_predicate_skips_rejected_rows_on_nav() {
    ListScrollModel m = makeListWithRejected(5, 5, {1});  // index 1 is a separator
    TEST_ASSERT_EQUAL_INT(0, m.selectedIndex());

    m.moveDown();  // 1 is rejected, so the cursor lands on 2
    TEST_ASSERT_EQUAL_INT(2, m.selectedIndex());

    m.moveUp();    // back over 1 to 0
    TEST_ASSERT_EQUAL_INT(0, m.selectedIndex());
}

void test_predicate_wraps_over_rejected_rows_at_ends() {
    ListScrollModel m = makeListWithRejected(5, 5, {0, 4});  // both ends are separators
    TEST_ASSERT_EQUAL_INT(1, m.selectedIndex());  // setSelectedIndex(0) snapped forward to 1

    m.moveUp();    // 0 rejected, wrap past 4 (rejected) to the last selectable, 3
    TEST_ASSERT_EQUAL_INT(3, m.selectedIndex());

    m.moveDown();  // 4 rejected, wrap past 0 (rejected) to the first selectable, 1
    TEST_ASSERT_EQUAL_INT(1, m.selectedIndex());
}

void test_predicate_all_rejected_does_not_move_or_loop() {
    ListScrollModel m = makeList(3, 5);
    m.setSelectablePredicate([](int) { return false; });  // nothing is selectable

    m.moveDown();  // must return without moving and without spinning
    TEST_ASSERT_EQUAL_INT(0, m.selectedIndex());
    m.moveUp();
    TEST_ASSERT_EQUAL_INT(0, m.selectedIndex());
}

void test_set_selected_index_snaps_off_rejected_row() {
    ListScrollModel m = makeListWithRejected(5, 5, {2});
    m.setSelectedIndex(2);  // 2 is rejected: snap forward to 3
    TEST_ASSERT_EQUAL_INT(3, m.selectedIndex());
}

void test_set_item_count_snaps_selection_off_rejected_row() {
    ListScrollModel m = makeListWithRejected(5, 5, {3});
    m.setSelectedIndex(4);  // selectable
    TEST_ASSERT_EQUAL_INT(4, m.selectedIndex());

    m.setItemCount(4);  // index 4 gone; clamps to 3, which is rejected, so snaps (wraps) to 0
    TEST_ASSERT_EQUAL_INT(0, m.selectedIndex());
}

void test_no_predicate_is_all_selectable() {
    ListScrollModel m = makeList(5, 5);  // no predicate set
    m.moveDown();
    TEST_ASSERT_EQUAL_INT(1, m.selectedIndex());  // nothing skipped, exactly as before
    m.moveUp();
    TEST_ASSERT_EQUAL_INT(0, m.selectedIndex());
}

void test_predicate_capture_fits_std_function_small_buffer() {
    int items = 0;
    auto pred = [&items](int i) { return items == i; };  // captures one reference (pointer-sized)
    // libstdc++/libc++ store a callable inline (no heap) when it fits their small-object buffer,
    // which is two pointers wide; a single-pointer capture qualifies on both the 64-bit host and
    // the 32-bit xtensa target, so installing it allocates nothing (ADR-0104).
    static_assert(sizeof(pred) <= 2 * sizeof(void*),
                  "selectable predicate capture must fit the std::function small buffer");
    ListScrollModel m = makeList(3, 5);
    m.setSelectablePredicate(pred);
    TEST_PASS();
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_down_on_last_visible_row_advances_window);
    RUN_TEST(test_up_scrolls_window_back_into_view);
    RUN_TEST(test_count_fits_viewport_never_scrolls);
    RUN_TEST(test_shrinking_viewport_reclamps_offset);
    RUN_TEST(test_up_on_first_wraps_to_last_and_shows_it);
    RUN_TEST(test_down_on_last_wraps_to_first);
    RUN_TEST(test_thumb_height_is_proportional_with_minimum);
    RUN_TEST(test_thumb_top_tracks_offset);
    RUN_TEST(test_predicate_skips_rejected_rows_on_nav);
    RUN_TEST(test_predicate_wraps_over_rejected_rows_at_ends);
    RUN_TEST(test_predicate_all_rejected_does_not_move_or_loop);
    RUN_TEST(test_set_selected_index_snaps_off_rejected_row);
    RUN_TEST(test_set_item_count_snaps_selection_off_rejected_row);
    RUN_TEST(test_no_predicate_is_all_selectable);
    RUN_TEST(test_predicate_capture_fits_std_function_small_buffer);
    return UNITY_END();
}
