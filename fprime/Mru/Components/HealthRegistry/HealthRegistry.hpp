// ======================================================================
// \title  HealthRegistry.hpp
// \brief  Strike record per replica; the record and its rule are quorum::Health
// ======================================================================

#ifndef Mru_HealthRegistry_HPP
#define Mru_HealthRegistry_HPP

#include "Mru/Components/HealthRegistry/HealthRegistryComponentAc.hpp"
#include "Mru/Types/FppConstantsAc.hpp"
#include "quorum.h"

namespace Mru {

class HealthRegistry final : public HealthRegistryComponentBase {
  public:
    //! Construct HealthRegistry object
    explicit HealthRegistry(const char* const compName);

    //! Destroy HealthRegistry object
    ~HealthRegistry() override;

    HealthRegistry(const HealthRegistry&) = delete;
    HealthRegistry& operator=(const HealthRegistry&) = delete;

  private:
    //! Forget a replica's strikes (MRU-HR-002)
    void clearIn_handler(FwIndexType portNum, FwIndexType replica) override;

    //! Record an outvote; true when the replica must be diagnosed (MRU-HR-001)
    bool strikeIn_handler(FwIndexType portNum, FwIndexType replica, U64 cycle) override;

    //! Report strikes since the last clear, capped at 3 (MRU-HR-003)
    void reportStrikes();

    //! The strike record, held in this component's memory
    quorum_health m_health;
};

}  // namespace Mru

#endif
