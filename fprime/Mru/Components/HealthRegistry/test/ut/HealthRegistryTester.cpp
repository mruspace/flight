// ======================================================================
// \title  HealthRegistryTester.cpp
// \brief  Unit tests of HealthRegistry
// ======================================================================

#include "HealthRegistryTester.hpp"

namespace Mru {

HealthRegistryTester ::HealthRegistryTester()
    : HealthRegistryGTestBase("HealthRegistryTester", HealthRegistryTester::MAX_HISTORY_SIZE),
      component("HealthRegistry") {
    this->initComponents();
    this->connectPorts();
}

HealthRegistryTester ::~HealthRegistryTester() {
    this->component.deinit();
}

bool HealthRegistryTester ::strike(FwIndexType replica, U64 cycle) {
    this->clearHistory();
    return this->invoke_to_strikeIn(0, replica, cycle);
}

void HealthRegistryTester ::clear(FwIndexType replica) {
    this->clearHistory();
    this->invoke_to_clearIn(0, replica);
}

void HealthRegistryTester ::assertStrikes(U32 r0, U32 r1, U32 r2) {
    ASSERT_TLM_Strikes_SIZE(1);
    const U32 expected[Replicas] = {r0, r1, r2};
    ASSERT_TLM_Strikes(0, StrikeCounts(expected));
}

void HealthRegistryTester ::threeStrikesInWindow() {
    ASSERT_FALSE(this->strike(1, 100));
    ASSERT_FALSE(this->strike(1, 150));
    ASSERT_TRUE(this->strike(1, 300));  // 100..300 is the 200-cycle window
}

void HealthRegistryTester ::strikesOutsideWindow() {
    ASSERT_FALSE(this->strike(0, 1000));
    ASSERT_FALSE(this->strike(0, 1100));
    ASSERT_FALSE(this->strike(0, 1201));  // 1000..1201 is wider than the window
}

void HealthRegistryTester ::clearForgets() {
    ASSERT_FALSE(this->strike(2, 10));
    ASSERT_FALSE(this->strike(2, 20));
    this->clear(2);
    this->assertStrikes(0, 0, 0);
    ASSERT_FALSE(this->strike(2, 30));
    ASSERT_FALSE(this->strike(2, 40));
    ASSERT_TRUE(this->strike(2, 50));
}

void HealthRegistryTester ::reportsStrikes() {
    this->strike(0, 1);
    this->assertStrikes(1, 0, 0);
    this->strike(2, 2);
    this->assertStrikes(1, 0, 1);
    this->strike(2, 3);
    this->strike(2, 4);
    this->assertStrikes(1, 0, 3);
    this->strike(2, 5);
    ASSERT_TLM_Strikes_SIZE(0);  // capped at 3: no change, so no update
}

void HealthRegistryTester ::outOfRange() {
    ASSERT_FALSE(this->strike(3, 1));
    ASSERT_FALSE(this->strike(-1, 1));
    this->clear(-1);
    this->clear(3);
    ASSERT_FALSE(this->strike(0, 2));
    this->assertStrikes(1, 0, 0);
}

}  // namespace Mru
