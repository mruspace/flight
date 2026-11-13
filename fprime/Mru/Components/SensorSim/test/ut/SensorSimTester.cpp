// ======================================================================
// \title  SensorSimTester.cpp
// \brief  Unit tests of SensorSim
// ======================================================================

#include "SensorSimTester.hpp"

namespace Mru {

SensorSimTester ::SensorSimTester()
    : SensorSimGTestBase("SensorSimTester", SensorSimTester::MAX_HISTORY_SIZE), component("SensorSim") {
    this->initComponents();
    this->connectPorts();
}

SensorSimTester ::~SensorSimTester() {
    this->component.deinit();
}

void SensorSimTester ::cycle() {
    this->clearHistory();
    this->invoke_to_schedIn(0, 0);
}

void SensorSimTester ::on(U32 noise, U32 accuracy) {
    this->clearHistory();
    this->sendCmd_SENSORS_ON(TEST_INSTANCE_ID, 0, noise, accuracy, 1);
}

void SensorSimTester ::fault(SensorFault::T kind, U8 sensor, U64 cycle, U32 cycles, I32 magnitude) {
    this->clearHistory();
    this->sendCmd_SENSOR_FAULT(TEST_INSTANCE_ID, 0, kind, sensor, cycle, cycles, magnitude);
}

const SensorSimTester::FromPortEntry_readingsOut& SensorSimTester ::last() const {
    return this->fromPortHistory_readingsOut->at(this->fromPortHistory_readingsOut->size() - 1);
}

void SensorSimTester ::offUntilOn() {
    this->cycle();
    ASSERT_from_readingsOut_SIZE(0);
    this->on(50, 50);
    ASSERT_EVENTS_SensorsOn(0, 50, 50, 1);
    for (U64 c = 1; c <= 50; c++) {
        this->cycle();
        ASSERT_from_readingsOut_SIZE(1);
        ASSERT_EQ(this->last().cycle, c);
        for (FwIndexType i = 0; i < Sensors; i++) {
            const I32 d = this->last().values[i] - SensorSim::truth(c);
            ASSERT_TRUE(d >= -50 && d <= 50);
            ASSERT_TRUE(this->last().present[i]);
        }
    }
    // the truth: a triangle, 20,000 to 22,000 and back every 400 cycles
    ASSERT_EQ(SensorSim::truth(0), 20000);
    ASSERT_EQ(SensorSim::truth(200), 22000);
    ASSERT_EQ(SensorSim::truth(300), 21000);
    ASSERT_EQ(SensorSim::truth(400), 20000);
}

void SensorSimTester ::faults() {
    this->on(0, 0);
    this->fault(SensorFault::SPIKE, 0, 2, 2, 5000);
    ASSERT_CMD_RESPONSE(0, SensorSimComponentBase::OPCODE_SENSOR_FAULT, 0, Fw::CmdResponse::OK);
    this->fault(SensorFault::DRIFT, 1, 3, 4, 400);
    this->fault(SensorFault::STUCK, 2, 4, 0, 0);
    this->fault(SensorFault::DROP, 0, 6, 1, 0);
    this->fault(SensorFault::SPIKE, 3, 1, 1, 1);
    ASSERT_EVENTS_SensorFaultRefused(0, 3);
    const I32 expected[8][Sensors] = {
        {20010, 20010, 20010},  // 1
        {25020, 20020, 20020},  // 2 spike
        {25030, 20130, 20030},  // 3 spike, drift 1/4
        {20040, 20240, 20040},  // 4 drift 2/4, stuck from here
        {20050, 20350, 20040},  // 5 drift 3/4
        {20060, 20460, 20040},  // 6 drift 4/4, sensor 0 dropped
        {20070, 20470, 20040},  // 7 drift holds
        {20080, 20480, 20040},  // 8
    };
    for (U64 c = 1; c <= 8; c++) {
        this->cycle();
        for (FwIndexType i = 0; i < Sensors; i++) {
            if (c == 6 && i == 0) {
                ASSERT_FALSE(this->last().present[0]);
                continue;
            }
            ASSERT_TRUE(this->last().present[i]);
            ASSERT_EQ(this->last().values[i], expected[c - 1][i]) << "cycle " << c << " sensor " << i;
        }
    }
}

void SensorSimTester ::scoring() {
    this->on(0, 100);
    // no voter connected: nothing chosen, every cycle withheld
    this->cycle();
    ASSERT_TLM_Withheld(0, 1);
    // a value within the accuracy, one outside, one unchecked
    this->invoke_to_selectedIn(0, 1, SensorSim::truth(1) + 100, true);
    this->invoke_to_selectedIn(0, 1, SensorSim::truth(1) + 101, true);
    ASSERT_EVENTS_WrongValue(0, 1, SensorSim::truth(1) + 101, SensorSim::truth(1));
    this->invoke_to_selectedIn(0, 1, 0, false);
    const SensorSim::Totals t = this->component.totals();
    ASSERT_EQ(t.correct, 1U);
    ASSERT_EQ(t.wrong, 1U);
    ASSERT_EQ(t.unchecked, 1U);
    // the plain average: a spike of 900 on one sensor moves it by 300
    this->fault(SensorFault::SPIKE, 0, 2, 1, 900);
    this->cycle();
    ASSERT_EQ(this->component.totals().averageWrong, 1U);
    this->cycle();
    ASSERT_EQ(this->component.totals().averageWrong, 1U);
}

}  // namespace Mru
