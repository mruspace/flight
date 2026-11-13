/* C test of quorum.h against the static library (MRU-FFI-001, 003). */
#include <assert.h>
#include <stddef.h>
#include <stdio.h>

#include "quorum.h"

int main(void) {
    /* layout: the Rust test checks the same numbers */
    assert(sizeof(quorum_reply) == 24 && _Alignof(quorum_reply) == 8);
    assert(sizeof(quorum_verdict) == 24 && _Alignof(quorum_verdict) == 8);
    assert(sizeof(quorum_health) == 96 && _Alignof(quorum_health) == 8);
    assert(offsetof(quorum_verdict, value) == 8);
    assert(offsetof(quorum_verdict, dissent) == 16);
    assert(offsetof(quorum_verdict, diagnose) == 19);

    quorum_reply r[3] = {{QUORUM_SINGLE, 7, 0}, {QUORUM_SINGLE, 9, 0}, {QUORUM_SINGLE, 7, 0}};
    quorum_verdict v = quorum_decide(QUORUM_SHRINK, r);
    assert(v.mode == QUORUM_VOTE && v.outcome == QUORUM_DELIVER && v.value == 7);
    assert(!v.dissent[0] && v.dissent[1] && !v.dissent[2] && !v.diagnose);

    quorum_reply pair[3] = {{QUORUM_SINGLE, 7, 0}, {QUORUM_ABSENT, 0, 0}, {QUORUM_SINGLE, 9, 0}};
    v = quorum_decide(QUORUM_SHRINK, pair);
    assert(v.mode == QUORUM_COMPARE && v.outcome == QUORUM_DETECTED && v.diagnose);

    quorum_reply one[3] = {{QUORUM_ABSENT, 0, 0}, {QUORUM_PAIR, 5, 5}, {QUORUM_ABSENT, 0, 0}};
    v = quorum_decide(QUORUM_SHRINK, one);
    assert(v.mode == QUORUM_SELF_CHECK && v.outcome == QUORUM_DELIVER && v.value == 5);
    v = quorum_decide(QUORUM_TMR, one);
    assert(v.mode == QUORUM_HALTED && v.outcome == QUORUM_OUTCOME_HALTED);
    v = quorum_decide(7, r);
    assert(v.outcome == QUORUM_OUTCOME_HALTED);
    v = quorum_decide(QUORUM_SHRINK, NULL);
    assert(v.outcome == QUORUM_OUTCOME_HALTED);

    assert(quorum_mode(QUORUM_SHRINK, 1) == QUORUM_SELF_CHECK);
    assert(quorum_mode(QUORUM_TMR, 2) == QUORUM_COMPARE);

    quorum_health h;
    quorum_health_init(&h);
    assert(!quorum_health_strike(&h, 2, 100));
    assert(!quorum_health_strike(&h, 2, 150));
    assert(quorum_health_strike(&h, 2, 250));
    quorum_health_clear(&h, 2);
    assert(h.count[2] == 0);
    assert(!quorum_health_strike(&h, 3, 1));
    assert(!quorum_health_strike(NULL, 0, 1));

    assert(sizeof(quorum_reading) == 8 && _Alignof(quorum_reading) == 4);
    assert(sizeof(quorum_sensor_selection) == 12 && _Alignof(quorum_sensor_selection) == 4);
    assert(offsetof(quorum_sensor_selection, value) == 4);
    assert(offsetof(quorum_sensor_selection, outlier) == 8);
    assert(sizeof(quorum_sensor_run) == 2);

    quorum_reading rd[3] = {{true, 100}, {true, 5000}, {true, 98}};
    quorum_sensor_selection s = quorum_sensor_select(rd, 10);
    assert(s.outcome == QUORUM_SENSOR_AGREED && s.value == 100);
    assert(!s.outlier[0] && s.outlier[1] && !s.outlier[2]);
    quorum_reading two[3] = {{true, 100}, {false, 0}, {true, 300}};
    s = quorum_sensor_select(two, 10);
    assert(s.outcome == QUORUM_SENSOR_DISAGREE);
    s = quorum_sensor_select(NULL, 10);
    assert(s.outcome == QUORUM_SENSOR_NOTHING);
    quorum_sensor_run run = quorum_sensor_observe(4, true, 5);
    assert(run.run == 5 && run.exclude);
    run = quorum_sensor_observe(4, false, 5);
    assert(run.run == 0 && !run.exclude);

    puts("quorum.h: all checks passed");
    return 0;
}
