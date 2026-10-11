// ======================================================================
// \title  HealthRegistry.cpp
// \brief  Strike record per replica; the record and its rule are quorum::Health
// ======================================================================

#include "Mru/Components/HealthRegistry/HealthRegistry.hpp"

namespace Mru {

HealthRegistry ::HealthRegistry(const char* const compName) : HealthRegistryComponentBase(compName), m_health() {
    quorum_health_init(&this->m_health);
}

HealthRegistry ::~HealthRegistry() {}

void HealthRegistry ::clearIn_handler(FwIndexType portNum, FwIndexType replica) {
    (void)portNum;
    // the core ignores an index above 2; a negative one is out of range too
    if (replica < 0) {
        return;
    }
    quorum_health_clear(&this->m_health, static_cast<U32>(replica));
    this->reportStrikes();
}

bool HealthRegistry ::strikeIn_handler(FwIndexType portNum, FwIndexType replica, U64 cycle) {
    (void)portNum;
    if (replica < 0) {
        return false;
    }
    const bool diagnose = quorum_health_strike(&this->m_health, static_cast<U32>(replica), cycle);
    this->reportStrikes();
    return diagnose;
}

void HealthRegistry ::reportStrikes() {
    // strikes since the last clear, capped at QUORUM_STRIKES
    StrikeCounts counts;
    for (FwIndexType i = 0; i < Replicas; i++) {
        const U64 n = this->m_health.count[i];
        counts[i] = static_cast<U32>((n < QUORUM_STRIKES) ? n : QUORUM_STRIKES);
    }
    this->tlmWrite_Strikes(counts);
}

}  // namespace Mru
