//! Decision core of Mru's shrinking quorum.
//!
//! Three replicas compute the same result. This crate decides, each tick, what
//! the system may deliver given what the replicas returned:
//!
//! - **Shrinking quorum** (`Policy::Shrink`): vote on three, compare on two,
//!   self-check on one.
//! - **Fixed TMR** (`Policy::Tmr`): vote on three, compare on two, stop on one.
//!
//! It also keeps the health record that decides when a replica that keeps
//! being outvoted must be diagnosed.
//!
//! The crate is `no_std`, allocates nothing and has no dependencies, so the
//! same code can run on flight hardware and be checked with Kani
//! (see the `proofs` module, built only under `cargo kani`).

#![no_std]

/// Number of replicas.
pub const REPLICAS: usize = 3;

/// Strikes within [`STRIKE_WINDOW`] ticks that trigger a diagnosis.
pub const STRIKES: usize = 3;

/// Length of the strike window, in ticks.
pub const STRIKE_WINDOW: u64 = 200;

/// Redundancy policy.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Policy {
    /// Vote on three, compare on two, self-check on one.
    Shrink,
    /// Vote on three, compare on two, stop on one (classic fixed TMR).
    Tmr,
}

/// Operating mode for a given number of live replicas.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Mode {
    Vote,
    Compare,
    /// One replica computes everything twice and compares with itself.
    SelfCheck,
    Halted,
}

/// What one replica returned this tick.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Reply {
    /// The replica is dead or did not answer.
    Absent,
    /// One result.
    Single(u64),
    /// Two results of the same computation (self-check).
    Pair(u64, u64),
}

impl Reply {
    fn value(self) -> Option<u64> {
        match self {
            Reply::Absent => None,
            Reply::Single(v) | Reply::Pair(v, _) => Some(v),
        }
    }
}

/// What the system does with this tick.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Outcome {
    /// Deliver this result.
    Deliver(u64),
    /// A disagreement was caught; deliver nothing this tick.
    Detected,
    /// The policy cannot continue with the replicas left.
    Halted,
}

/// The decision for one tick.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct Verdict {
    pub mode: Mode,
    pub outcome: Outcome,
    /// Replicas that were outvoted (vote mode only).
    pub dissent: [bool; REPLICAS],
    /// Two replicas disagree and nothing tells which is wrong: run a
    /// known-answer test on both (shrinking quorum only).
    pub diagnose: bool,
}

/// The mode a policy runs in with `alive` live replicas.
pub fn mode(policy: Policy, alive: usize) -> Mode {
    match (policy, alive) {
        (_, 0) => Mode::Halted,
        (Policy::Shrink, 1) => Mode::SelfCheck,
        (Policy::Tmr, 1) => Mode::Halted,
        (_, 2) => Mode::Compare,
        _ => Mode::Vote,
    }
}

/// Decide what to deliver from the replicas' replies.
pub fn decide(policy: Policy, replies: &[Reply; REPLICAS]) -> Verdict {
    let alive = replies.iter().filter(|r| **r != Reply::Absent).count();
    let mode = mode(policy, alive);
    let mut verdict = Verdict {
        mode,
        outcome: Outcome::Detected,
        dissent: [false; REPLICAS],
        diagnose: false,
    };

    match mode {
        Mode::Halted => verdict.outcome = Outcome::Halted,
        Mode::Vote => {
            // deliver a value that at least two replicas agree on
            for r in replies {
                let Some(v) = r.value() else { continue };
                let agree = replies.iter().filter(|o| o.value() == Some(v)).count();
                if agree >= 2 {
                    verdict.outcome = Outcome::Deliver(v);
                    for (i, o) in replies.iter().enumerate() {
                        verdict.dissent[i] = matches!(o.value(), Some(x) if x != v);
                    }
                    break;
                }
            }
        }
        Mode::Compare => {
            let mut values = replies.iter().filter_map(|r| r.value());
            if let (Some(a), Some(b)) = (values.next(), values.next()) {
                if a == b {
                    verdict.outcome = Outcome::Deliver(a);
                } else {
                    verdict.diagnose = policy == Policy::Shrink;
                }
            }
        }
        Mode::SelfCheck => {
            for r in replies {
                if let Reply::Pair(a, b) = *r {
                    if a == b {
                        verdict.outcome = Outcome::Deliver(a);
                    }
                }
            }
        }
    }
    verdict
}

/// Strike record per replica: the ticks of its most recent outvotes.
#[derive(Clone, Copy, Debug)]
pub struct Health {
    strikes: [[u64; STRIKES]; REPLICAS],
    count: [usize; REPLICAS],
}

impl Default for Health {
    fn default() -> Self {
        Self::new()
    }
}

impl Health {
    pub const fn new() -> Self {
        Self {
            strikes: [[0; STRIKES]; REPLICAS],
            count: [0; REPLICAS],
        }
    }

    /// Record that replica `i` was outvoted at `tick`. Returns `true` when it
    /// has [`STRIKES`] strikes within [`STRIKE_WINDOW`] ticks and must be
    /// diagnosed.
    pub fn strike(&mut self, i: usize, tick: u64) -> bool {
        let ring = &mut self.strikes[i];
        let n = self.count[i];
        ring[n % STRIKES] = tick;
        self.count[i] = n + 1;
        if self.count[i] < STRIKES {
            return false;
        }
        let oldest = ring.iter().copied().min().unwrap_or(0);
        tick.saturating_sub(oldest) <= STRIKE_WINDOW
    }

    /// Forget replica `i`'s strikes (it passed its diagnosis).
    pub fn clear(&mut self, i: usize) {
        self.strikes[i] = [0; STRIKES];
        self.count[i] = 0;
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    const VALUES: [u64; 3] = [1, 2, 3];

    // every combination of replies over a small value domain
    fn all_replies() -> impl Iterator<Item = [Reply; REPLICAS]> {
        let mut one = [Reply::Absent; 13];
        let mut k = 1;
        for a in VALUES {
            one[k] = Reply::Single(a);
            k += 1;
        }
        for a in VALUES {
            for b in VALUES {
                if k < one.len() {
                    one[k] = Reply::Pair(a, b);
                    k += 1;
                }
            }
        }
        let one_iter = move || one.into_iter();
        one_iter()
            .flat_map(move |x| one_iter().flat_map(move |y| one_iter().map(move |z| [x, y, z])))
    }

    fn alive(r: &[Reply; REPLICAS]) -> usize {
        r.iter().filter(|x| **x != Reply::Absent).count()
    }

    #[test]
    fn identical_while_tmr_can_run() {
        for r in all_replies() {
            if alive(&r) >= 2 {
                assert_eq!(
                    decide(Policy::Shrink, &r).outcome,
                    decide(Policy::Tmr, &r).outcome,
                    "{r:?}"
                );
            }
        }
    }

    #[test]
    fn tmr_stops_below_two() {
        for r in all_replies() {
            if alive(&r) < 2 {
                assert_eq!(decide(Policy::Tmr, &r).outcome, Outcome::Halted, "{r:?}");
            }
        }
    }

    #[test]
    fn shrink_halts_only_with_nothing_left() {
        for r in all_replies() {
            let halted = decide(Policy::Shrink, &r).outcome == Outcome::Halted;
            assert_eq!(halted, alive(&r) == 0, "{r:?}");
        }
    }

    #[test]
    fn delivers_only_agreed_values() {
        for r in all_replies() {
            for p in [Policy::Shrink, Policy::Tmr] {
                if let Outcome::Deliver(v) = decide(p, &r).outcome {
                    let agreeing = r.iter().filter(|x| x.value() == Some(v)).count();
                    let self_checked = r.iter().any(|x| *x == Reply::Pair(v, v));
                    assert!(
                        agreeing >= 2 || (alive(&r) == 1 && self_checked),
                        "{p:?} {r:?}"
                    );
                }
            }
        }
    }

    #[test]
    fn three_strikes_in_window_trigger_diagnosis() {
        let mut h = Health::new();
        assert!(!h.strike(0, 100));
        assert!(!h.strike(0, 150));
        assert!(h.strike(0, 250));
        h.clear(0);
        assert!(!h.strike(0, 1000));
        assert!(!h.strike(0, 1100));
        assert!(!h.strike(0, 1300)); // 1000..1300 is wider than the window
    }
}

/// Kani proofs: the same properties as the tests, for every possible input.
/// Run with `cargo kani -p quorum`.
#[cfg(kani)]
mod proofs {
    use super::*;

    fn any_reply() -> Reply {
        match kani::any::<u8>() % 3 {
            0 => Reply::Absent,
            1 => Reply::Single(kani::any()),
            _ => Reply::Pair(kani::any(), kani::any()),
        }
    }

    fn any_replies() -> [Reply; REPLICAS] {
        [any_reply(), any_reply(), any_reply()]
    }

    fn alive(r: &[Reply; REPLICAS]) -> usize {
        r.iter().filter(|x| **x != Reply::Absent).count()
    }

    /// The two policies decide identically while fixed TMR can still run, so
    /// the shrinking quorum can never deliver less than fixed TMR.
    #[kani::proof]
    #[kani::unwind(5)]
    fn identical_while_tmr_can_run() {
        let r = any_replies();
        kani::assume(alive(&r) >= 2);
        assert_eq!(
            decide(Policy::Shrink, &r).outcome,
            decide(Policy::Tmr, &r).outcome
        );
    }

    /// Fixed TMR stops once fewer than two replicas are left.
    #[kani::proof]
    #[kani::unwind(5)]
    fn tmr_stops_below_two() {
        let r = any_replies();
        kani::assume(alive(&r) < 2);
        assert_eq!(decide(Policy::Tmr, &r).outcome, Outcome::Halted);
    }

    /// The shrinking quorum halts only when no replica is left.
    #[kani::proof]
    #[kani::unwind(5)]
    fn shrink_halts_only_with_nothing_left() {
        let r = any_replies();
        let halted = decide(Policy::Shrink, &r).outcome == Outcome::Halted;
        assert_eq!(halted, alive(&r) == 0);
    }

    /// A delivered value was produced by at least two replicas, or by both
    /// runs of the last replica's self-check.
    #[kani::proof]
    #[kani::unwind(5)]
    fn delivers_only_agreed_values() {
        let r = any_replies();
        let policy = if kani::any() {
            Policy::Shrink
        } else {
            Policy::Tmr
        };
        if let Outcome::Deliver(v) = decide(policy, &r).outcome {
            let agreeing = r.iter().filter(|x| x.value() == Some(v)).count();
            let self_checked = r.iter().any(|x| *x == Reply::Pair(v, v));
            assert!(agreeing >= 2 || (alive(&r) == 1 && self_checked));
        }
    }
}
