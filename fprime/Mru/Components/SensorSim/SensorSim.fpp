module Mru {

  @ Test builds only (MRU-SS-005). Three sensors of one known quantity, with
  @ noise and faults by command. Each cycle it sends their readings to
  @ SensorVoter and scores the value chosen against the true value. It also
  @ scores a plain average of the readings, the baseline a voter is compared
  @ with. Off until SENSORS_ON.
  passive component SensorSim {

    # ----------------------------------------------------------------------
    # Ports
    # ----------------------------------------------------------------------

    @ Rate group: one reading per sensor per cycle (MRU-SS-001)
    guarded input port schedIn: Svc.Sched

    @ The readings, to SensorVoter
    output port readingsOut: SensorReadings

    @ The value SensorVoter chose; called back inside schedIn, so not guarded
    sync input port selectedIn: SensorSelected

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

    # ----------------------------------------------------------------------
    # Commands
    # ----------------------------------------------------------------------

    @ Start the sensors (MRU-SS-001, 003)
    guarded command SENSORS_ON(
      noise: U32 @< Each reading is the true value plus uniform noise in [-noise, noise]
      accuracy: U32 @< A value within this of the true value is correct
      seed: U64 @< Seed of the noise
    )

    @ Schedule a fault on a sensor (MRU-SS-002)
    guarded command SENSOR_FAULT(
      kind: SensorFault @< The fault
      sensor: U8 @< The sensor, 0 to 2
      cycle: U64 @< Its first cycle
      cycles: U32 @< How long: a spike's or a drop's length, a drift's ramp
      magnitude: I32 @< A spike's height, or a drift's final bias
    )

    # ----------------------------------------------------------------------
    # Events
    # ----------------------------------------------------------------------

    @ The sensors were started
    event SensorsOn(
      noise: U32 @< Noise amplitude
      accuracy: U32 @< Accuracy for scoring
      seed: U64 @< Seed
    ) \
      severity activity high \
      format "Sensors on: noise {}, accuracy {}, seed {}"

    @ A fault was scheduled
    event SensorFaultScheduled(
      kind: SensorFault @< The fault
      sensor: U8 @< The sensor
      cycle: U64 @< Its first cycle
      cycles: U32 @< Its length
      magnitude: I32 @< Its magnitude
    ) \
      severity activity high \
      format "{} scheduled on sensor {} at cycle {} ({} cycles, magnitude {})"

    @ A fault took effect
    event SensorFaultApplied(
      kind: SensorFault @< The fault
      sensor: U8 @< The sensor
      cycle: U64 @< The cycle
    ) \
      severity warning low \
      format "{} applied to sensor {} at cycle {}"

    @ A fault was refused: no free slot, or a sensor out of range
    event SensorFaultRefused(
      sensor: U8 @< The sensor asked for
    ) \
      severity warning low \
      format "Fault on sensor {} refused"

    @ A value delivered as checked was further than the accuracy from the true value
    event WrongValue(
      cycle: U64 @< The cycle
      value: I32 @< The value delivered
      truth: I32 @< The true value
    ) \
      severity warning high \
      format "Wrong value at cycle {}: {} against {}" \
      throttle 10

    # ----------------------------------------------------------------------
    # Telemetry (MRU-SS-004)
    # ----------------------------------------------------------------------

    @ Checked values within the accuracy
    telemetry Correct: U64

    @ Checked values outside it
    telemetry Wrong: U64

    @ Unchecked values (one reading)
    telemetry UncheckedValues: U64

    @ Cycles with no value
    telemetry Withheld: U64

    @ Cycles in which the plain average of the readings was outside the accuracy
    telemetry AverageWrong: U64

  }

}
