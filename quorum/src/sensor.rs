//! Sensor selection: one value from up to three readings of the same
//! quantity, which may differ slightly, and the count that decides when a
//! sensor that keeps disagreeing is excluded.
//!
//! [`decide`](crate::decide) needs results that match exactly. Readings of
//! one quantity by three sensors never do: noise, calibration and timing make
//! them differ a little. So readings are compared within a tolerance:
//!
//! - **Three readings**: the median, if at least one other reading is within
//!   the tolerance of it. A reading further away is an outlier.
//! - **Two readings**: their midpoint, if they are within the tolerance of
//!   each other. If not, nothing says which is right: [`Selected::Disagree`].
//! - **One reading**: delivered as [`Selected::Unchecked`], since nothing
//!   checks it.
//!
//! [`observe`] separates a short spike from a sensor that has failed: a
//! sensor is excluded only after `limit` outlier cycles in a row.

/// Number of sensors of one quantity.
pub const SENSORS: usize = 3;

/// What [`select`] chose.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Selected {
    /// A value two or more readings agree on, within the tolerance.
    Agreed(i32),
    /// The only reading: not cross-checked.
    Unchecked(i32),
    /// The readings differ by more than the tolerance and nothing says which
    /// one is right. Nothing is delivered.
    Disagree,
    /// No reading.
    Nothing,
}

/// The choice for one cycle.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct Selection {
    pub selected: Selected,
    /// Readings more than the tolerance away from the agreed value. Set only
    /// when three readings give [`Selected::Agreed`].
    pub outlier: [bool; SENSORS],
}

/// Whether two readings are within `tol` of each other, with no overflow.
pub fn close(a: i32, b: i32, tol: u32) -> bool {
    (i64::from(a) - i64::from(b)).unsigned_abs() <= u64::from(tol)
}

/// The midpoint of two readings, rounded down, with no overflow.
fn midpoint(a: i32, b: i32) -> i32 {
    // the floor of the mean of two i32 values is always an i32
    ((i64::from(a) + i64::from(b)) >> 1) as i32
}

/// The median of three values, ties included.
fn median(a: i32, b: i32, c: i32) -> i32 {
    a.min(b).max(a.max(b).min(c))
}

/// Choose one value from the readings of the sensors in use (`None`: no
/// reading, or a sensor excluded), with tolerance `tol`.
pub fn select(readings: &[Option<i32>; SENSORS], tol: u32) -> Selection {
    let mut outlier = [false; SENSORS];
    let mut values = [0i32; SENSORS];
    let mut n = 0;
    for r in readings.iter().flatten() {
        values[n] = *r;
        n += 1;
    }
    let selected = match n {
        0 => Selected::Nothing,
        1 => Selected::Unchecked(values[0]),
        2 => {
            if close(values[0], values[1], tol) {
                Selected::Agreed(midpoint(values[0], values[1]))
            } else {
                Selected::Disagree
            }
        }
        _ => {
            let m = median(values[0], values[1], values[2]);
            let near = values.map(|v| close(v, m, tol));
            if near.iter().filter(|x| **x).count() >= 2 {
                // readings are all present here, so slot i holds values[i]
                for i in 0..SENSORS {
                    outlier[i] = !near[i];
                }
                Selected::Agreed(m)
            } else {
                Selected::Disagree
            }
        }
    };
    Selection { selected, outlier }
}

/// One cycle of a sensor's outlier count: `run` is its outlier cycles in a
/// row so far. Returns the new count, and whether the sensor must now be
/// excluded: after `limit` outlier cycles in a row (never when `limit` is 0).
/// Call it only in cycles with an agreed value; in others, keep the count.
pub fn observe(run: u8, outlier: bool, limit: u8) -> (u8, bool) {
    let run = if outlier { run.saturating_add(1) } else { 0 };
    (run, limit > 0 && run >= limit)
}

#[cfg(test)]
mod tests {
    use super::*;

    fn s(r: [Option<i32>; SENSORS], tol: u32) -> Selection {
        select(&r, tol)
    }

    #[test]
    fn three_agree_the_median() {
        let x = s([Some(100), Some(104), Some(98)], 10);
        assert_eq!(x.selected, Selected::Agreed(100));
        assert_eq!(x.outlier, [false; 3]);
    }

    #[test]
    fn a_spike_is_an_outlier_and_not_chosen() {
        let x = s([Some(100), Some(5000), Some(98)], 10);
        assert_eq!(x.selected, Selected::Agreed(100));
        assert_eq!(x.outlier, [false, true, false]);
        let x = s([Some(-9000), Some(100), Some(103)], 10);
        assert_eq!(x.selected, Selected::Agreed(100));
        assert_eq!(x.outlier, [true, false, false]);
    }

    #[test]
    fn ties_take_the_tied_value() {
        assert_eq!(median(5, 5, -9), 5);
        assert_eq!(median(-9, 5, 5), 5);
        assert_eq!(median(5, -9, 5), 5);
        assert_eq!(median(3, 1, 2), 2);
        let x = s([Some(7), Some(7), Some(900)], 0);
        assert_eq!(x.selected, Selected::Agreed(7));
        assert_eq!(x.outlier, [false, false, true]);
    }

    #[test]
    fn three_spread_out_disagree() {
        let x = s([Some(0), Some(100), Some(200)], 10);
        assert_eq!(x.selected, Selected::Disagree);
        assert_eq!(x.outlier, [false; 3]);
    }

    #[test]
    fn two_agree_on_their_midpoint_or_disagree() {
        assert_eq!(
            s([Some(100), None, Some(105)], 10).selected,
            Selected::Agreed(102)
        );
        assert_eq!(
            s([None, Some(-3), Some(-4)], 10).selected,
            Selected::Agreed(-4)
        );
        assert_eq!(
            s([Some(100), Some(200), None], 10).selected,
            Selected::Disagree
        );
    }

    #[test]
    fn one_is_unchecked_none_is_nothing() {
        assert_eq!(s([None, Some(7), None], 0).selected, Selected::Unchecked(7));
        assert_eq!(s([None, None, None], 0).selected, Selected::Nothing);
    }

    #[test]
    fn no_overflow_at_the_extremes() {
        assert_eq!(
            s([Some(i32::MIN), Some(i32::MAX), None], u32::MAX).selected,
            Selected::Agreed(-1)
        );
        assert!(!close(i32::MIN, i32::MAX, u32::MAX >> 1));
        assert_eq!(
            s([Some(i32::MAX), Some(i32::MAX), Some(i32::MIN)], 0).selected,
            Selected::Agreed(i32::MAX)
        );
    }

    #[test]
    fn a_spike_shorter_than_the_limit_is_forgiven() {
        let mut run = 0;
        for _ in 0..4 {
            let (r, out) = observe(run, true, 5);
            assert!(!out);
            run = r;
        }
        let (r, out) = observe(run, false, 5);
        assert_eq!((r, out), (0, false));
        let mut run = 0;
        let mut excluded = false;
        for _ in 0..5 {
            let (r, out) = observe(run, true, 5);
            run = r;
            excluded = out;
        }
        assert!(excluded);
        assert_eq!(observe(200, true, 0), (201, false));
        assert_eq!(observe(255, true, 3), (255, true));
    }
}

/// Kani proofs, for every set of three readings and every tolerance.
/// Run with `cargo kani -p quorum`.
#[cfg(kani)]
mod proofs {
    use super::*;

    fn any_reading() -> Option<i32> {
        if kani::any() {
            Some(kani::any())
        } else {
            None
        }
    }

    fn any_readings() -> [Option<i32>; SENSORS] {
        [any_reading(), any_reading(), any_reading()]
    }

    /// An agreed value has at least two readings within the tolerance of it,
    /// and lies between the smallest and the largest reading.
    #[kani::proof]
    #[kani::unwind(5)]
    fn agreed_values_are_agreed_and_in_range() {
        let r = any_readings();
        let tol: u32 = kani::any();
        if let Selected::Agreed(v) = select(&r, tol).selected {
            let mut near = 0;
            let mut below = false;
            let mut above = false;
            for x in r.iter().flatten() {
                if close(*x, v, tol) {
                    near += 1;
                }
                below |= *x <= v;
                above |= *x >= v;
            }
            assert!(near >= 2);
            assert!(below && above);
        }
    }

    /// An outlier is a reading, of an agreed cycle, more than the tolerance
    /// away from the value chosen, so it is never the value delivered. Every
    /// other reading of that cycle is within the tolerance.
    #[kani::proof]
    #[kani::unwind(5)]
    fn outliers_are_never_chosen() {
        let r = any_readings();
        let tol: u32 = kani::any();
        let s = select(&r, tol);
        for i in 0..SENSORS {
            if s.outlier[i] {
                let Selected::Agreed(v) = s.selected else {
                    panic!("an outlier outside an agreed cycle");
                };
                let Some(x) = r[i] else {
                    panic!("an outlier with no reading");
                };
                assert!(!close(x, v, tol));
                assert!(x != v);
            } else if let (Selected::Agreed(v), Some(x)) = (s.selected, r[i]) {
                if r.iter().all(|y| y.is_some()) {
                    assert!(close(x, v, tol));
                }
            }
        }
    }

    /// `median` returns one of the three values, with at least two of them on
    /// each side of it (itself counted).
    #[kani::proof]
    fn median_is_the_median() {
        let (a, b, c): (i32, i32, i32) = (kani::any(), kani::any(), kani::any());
        let m = median(a, b, c);
        assert!(m == a || m == b || m == c);
        let below = [a, b, c].iter().filter(|x| **x <= m).count();
        let above = [a, b, c].iter().filter(|x| **x >= m).count();
        assert!(below >= 2 && above >= 2);
    }

    /// Nothing is delivered unchecked while two or more readings are in.
    #[kani::proof]
    #[kani::unwind(5)]
    fn unchecked_only_with_one_reading() {
        let r = any_readings();
        let tol: u32 = kani::any();
        let present = r.iter().filter(|x| x.is_some()).count();
        match select(&r, tol).selected {
            Selected::Unchecked(_) => assert_eq!(present, 1),
            Selected::Nothing => assert_eq!(present, 0),
            _ => assert!(present >= 2),
        }
    }

    /// A sensor is excluded exactly when its last `limit` cycles were all
    /// outliers: a spike shorter than the limit never excludes it.
    #[kani::proof]
    #[kani::unwind(8)]
    fn excluded_only_after_limit_outliers_in_a_row() {
        let limit: u8 = kani::any();
        kani::assume(limit >= 1 && limit <= 6);
        let cycles: [bool; 6] = kani::any();
        let mut run = 0u8;
        let mut streak = 0u8;
        for c in cycles {
            let (r, excluded) = observe(run, c, limit);
            run = r;
            streak = if c { streak + 1 } else { 0 };
            assert_eq!(excluded, streak >= limit);
        }
    }
}
