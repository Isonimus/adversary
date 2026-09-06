/**
 * @file test_ir_capability.cpp
 * @brief IR capability regression (slice-0004). Pure, no hardware.
 */

#include <unity.h>
#include "modules/ir/ir_capability.h"
#include "config/pins.h"

using adversary::ir::IrTxSource;
using adversary::ir::capHasIrRx;
using adversary::ir::irTxPin;
using adversary::hal::ExpansionCap;

void setUp(void) {}
void tearDown(void) {}

// IR receive is present iff the multi-radio cap is the resolved cap.
void test_capHasIrRx_tracks_cap(void) {
    TEST_ASSERT_TRUE(capHasIrRx(ExpansionCap::MultiRadio));
    TEST_ASSERT_FALSE(capHasIrRx(ExpansionCap::None));
}

// irTxPin selects the built-in emitter vs the cap array by the setting.
void test_irTxPin_selects_emitter(void) {
    TEST_ASSERT_EQUAL_INT(adversary::pins::IR_TX, irTxPin(IrTxSource::BuiltIn));
    TEST_ASSERT_EQUAL_INT(adversary::pins::CAP_IR_TX, irTxPin(IrTxSource::CapArray));
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_capHasIrRx_tracks_cap);
    RUN_TEST(test_irTxPin_selects_emitter);
    return UNITY_END();
}
