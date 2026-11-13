module Mru {

  @ Chooses one value each cycle from up to three sensors of the same
  @ quantity, within a tolerance, and excludes a sensor that keeps
  @ disagreeing. Every decision comes from the quorum crate's sensor module
  @ (MRU-SV-001).
  passive component SensorVoter {

    # ----------------------------------------------------------------------
    # Ports
    # ----------------------------------------------------------------------

    @ The sensors' readings for one cycle (MRU-SV-002)
    guarded input port readingsIn: SensorReadings

    @ The value chosen, when there is one (MRU-SV-003)
    output port selectedOut: SensorSelected

    # ----------------------------------------------------------------------
    # Special ports
    # ----------------------------------------------------------------------

    command recv port cmdIn
    command reg port cmdRegOut
    command resp port cmdResponseOut
    event port logOut
    text event port logTextOut
    time get port timeCaller
    telemetry port tlmOut
    param get port prmGetOut
    param set port prmSetOut

    # ----------------------------------------------------------------------
    # Commands
    # ----------------------------------------------------------------------

    @ Use an excluded sensor again, as after a repair or a recalibration (MRU-SV-006)
    guarded command INCLUDE_SENSOR(
      sensor: U8 @< The sensor, 0 to 2
    )

    # ----------------------------------------------------------------------
    # Parameters
    # ----------------------------------------------------------------------

    @ Readings within this of each other agree, in the readings' units (MRU-SV-002)
    param Tolerance: U32 default 0

    @ Outlier cycles in a row that exclude a sensor; 0 never excludes (MRU-SV-004)
    param ExcludeAfter: U8 default 5

    # ----------------------------------------------------------------------
    # Events
    # ----------------------------------------------------------------------

    @ A sensor's reading was more than the tolerance from the agreed value (MRU-SV-004)
    event SensorOutlier(
      sensor: U8 @< The sensor
      cycle: U64 @< The cycle
      reading: I32 @< Its reading
      agreed: I32 @< The value the others agreed on
    ) \
      severity warning low \
      format "Sensor {} outlier at cycle {}: {} against {}" \
      throttle 10

    @ A sensor was excluded after outlier cycles in a row (MRU-SV-004)
    event SensorExcluded(
      sensor: U8 @< The sensor
      cycle: U64 @< The cycle
      cycles: U8 @< Outlier cycles in a row
    ) \
      severity warning high \
      format "Sensor {} excluded at cycle {}: {} outlier cycles in a row"

    @ The readings disagreed and nothing said which was right; nothing was
    @ delivered (MRU-SV-003)
    event SensorsDisagree(
      cycle: U64 @< The cycle
    ) \
      severity warning low \
      format "Sensors disagree at cycle {}: nothing delivered" \
      throttle 10

    @ A value came from one reading only (MRU-SV-005)
    event UncheckedValue(
      cycle: U64 @< The cycle
    ) \
      severity warning low \
      format "Unchecked value at cycle {}: one reading only" \
      throttle 5

    @ An excluded sensor was included again by command (MRU-SV-006)
    event SensorIncluded(
      sensor: U8 @< The sensor
    ) \
      severity activity high \
      format "Sensor {} included again"

    @ INCLUDE_SENSOR was refused: no such sensor, or not excluded
    event IncludeRefused(
      sensor: U8 @< The sensor asked for
    ) \
      severity warning low \
      format "Include of sensor {} refused"

    # ----------------------------------------------------------------------
    # Telemetry (MRU-SV-007)
    # ----------------------------------------------------------------------

    @ Cycles with an agreed value
    telemetry Agreed: U64

    @ Cycles with an unchecked value
    telemetry Unchecked: U64

    @ Cycles with readings that disagreed
    telemetry Disagreed: U64

    @ Outlier readings
    telemetry Outliers: U64

    @ Sensors excluded
    telemetry Excluded: SensorFlags update on change

  }

}
