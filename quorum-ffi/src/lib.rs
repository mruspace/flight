//! C interface to the `quorum` decision core.
//!
//! Each function converts plain C values, calls the core, and converts the
//! answer back. There is no decision logic here (MRU-FFI-004). Only integers,
//! booleans and fixed-size structs of them cross the interface; the health
//! record lives in the caller's memory (MRU-FFI-002). Out-of-range input gets
//! the safe answer: a policy that is not 0 or 1 halts, a reply kind that is
//! not 0 to 2 counts as absent, a replica index above 2 changes nothing and
//! returns false, and a null pointer does the same.
//!
//! The header is `include/quorum.h`. The Kani proofs at the end show that every
//! function gives the core's answer (MRU-FFI-005).

#![cfg_attr(not(test), no_std)]

use quorum::{Health, Mode, Outcome, Policy, Reply, Verdict, REPLICAS, STRIKES};

pub const QUORUM_SHRINK: u8 = 0;
pub const QUORUM_TMR: u8 = 1;

pub const QUORUM_VOTE: u8 = 0;
pub const QUORUM_COMPARE: u8 = 1;
pub const QUORUM_SELF_CHECK: u8 = 2;
pub const QUORUM_HALTED: u8 = 3;

pub const QUORUM_ABSENT: u8 = 0;
pub const QUORUM_SINGLE: u8 = 1;
pub const QUORUM_PAIR: u8 = 2;

pub const QUORUM_DELIVER: u8 = 0;
pub const QUORUM_DETECTED: u8 = 1;
pub const QUORUM_OUTCOME_HALTED: u8 = 2;

/// What one replica returned: `kind` is ABSENT, SINGLE or PAIR.
#[repr(C)]
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub struct QuorumReply {
    pub kind: u8,
    pub first: u64,
    pub second: u64,
}

/// The decision for one cycle. `value` is meaningful only when `outcome` is DELIVER.
#[repr(C)]
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct QuorumVerdict {
    pub mode: u8,
    pub outcome: u8,
    pub value: u64,
    pub dissent: [bool; REPLICAS],
    pub diagnose: bool,
}

/// `quorum::Health`, as plain values the caller owns.
#[repr(C)]
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub struct QuorumHealth {
    pub strikes: [[u64; STRIKES]; REPLICAS],
    pub count: [u64; REPLICAS],
}

const HALTED_VERDICT: QuorumVerdict = QuorumVerdict {
    mode: QUORUM_HALTED,
    outcome: QUORUM_OUTCOME_HALTED,
    value: 0,
    dissent: [false; REPLICAS],
    diagnose: false,
};

fn policy(p: u8) -> Option<Policy> {
    match p {
        QUORUM_SHRINK => Some(Policy::Shrink),
        QUORUM_TMR => Some(Policy::Tmr),
        _ => None,
    }
}

fn reply(r: QuorumReply) -> Reply {
    match r.kind {
        QUORUM_SINGLE => Reply::Single(r.first),
        QUORUM_PAIR => Reply::Pair(r.first, r.second),
        _ => Reply::Absent,
    }
}

fn mode_code(m: Mode) -> u8 {
    match m {
        Mode::Vote => QUORUM_VOTE,
        Mode::Compare => QUORUM_COMPARE,
        Mode::SelfCheck => QUORUM_SELF_CHECK,
        Mode::Halted => QUORUM_HALTED,
    }
}

fn verdict(v: Verdict) -> QuorumVerdict {
    let (outcome, value) = match v.outcome {
        Outcome::Deliver(x) => (QUORUM_DELIVER, x),
        Outcome::Detected => (QUORUM_DETECTED, 0),
        Outcome::Halted => (QUORUM_OUTCOME_HALTED, 0),
    };
    QuorumVerdict {
        mode: mode_code(v.mode),
        outcome,
        value,
        dissent: v.dissent,
        diagnose: v.diagnose,
    }
}

fn to_core(h: &QuorumHealth) -> Health {
    let count = h.count.map(|c| usize::try_from(c).unwrap_or(usize::MAX));
    Health::from_raw(h.strikes, count)
}

fn from_core(health: &Health) -> QuorumHealth {
    let (strikes, count) = health.to_raw();
    QuorumHealth {
        strikes,
        count: count.map(|c| c as u64),
    }
}

/// The mode `policy` runs in with `alive` live replicas.
#[no_mangle]
pub extern "C" fn quorum_mode(policy_code: u8, alive: u32) -> u8 {
    match policy(policy_code) {
        Some(p) => mode_code(quorum::mode(
            p,
            usize::try_from(alive).unwrap_or(usize::MAX),
        )),
        None => QUORUM_HALTED,
    }
}

/// Decide what to deliver from three replies.
///
/// # Safety
/// `replies` must be null or point to `QUORUM_REPLICAS` readable replies.
#[no_mangle]
pub unsafe extern "C" fn quorum_decide(
    policy_code: u8,
    replies: *const QuorumReply,
) -> QuorumVerdict {
    let Some(p) = policy(policy_code) else {
        return HALTED_VERDICT;
    };
    if replies.is_null() {
        return HALTED_VERDICT;
    }
    // SAFETY: the caller guarantees REPLICAS readable replies.
    let r = unsafe { core::ptr::read(replies as *const [QuorumReply; REPLICAS]) };
    verdict(quorum::decide(p, &r.map(reply)))
}

/// Set a health record to no strikes.
///
/// # Safety
/// `h` must be null or point to a writable record.
#[no_mangle]
pub unsafe extern "C" fn quorum_health_init(h: *mut QuorumHealth) {
    if let Some(h) = unsafe { h.as_mut() } {
        *h = from_core(&Health::new());
    }
}

/// Record that `replica` was outvoted at `cycle`. True when it must be diagnosed.
///
/// # Safety
/// `h` must be null or point to a writable record.
#[no_mangle]
pub unsafe extern "C" fn quorum_health_strike(
    h: *mut QuorumHealth,
    replica: u32,
    cycle: u64,
) -> bool {
    let Some(h) = (unsafe { h.as_mut() }) else {
        return false;
    };
    let i = replica as usize;
    if i >= REPLICAS {
        return false;
    }
    let mut health = to_core(h);
    let diagnose = health.strike(i, cycle);
    *h = from_core(&health);
    diagnose
}

/// Forget `replica`'s strikes.
///
/// # Safety
/// `h` must be null or point to a writable record.
#[no_mangle]
pub unsafe extern "C" fn quorum_health_clear(h: *mut QuorumHealth, replica: u32) {
    let Some(h) = (unsafe { h.as_mut() }) else {
        return;
    };
    let i = replica as usize;
    if i >= REPLICAS {
        return;
    }
    let mut health = to_core(h);
    health.clear(i);
    *h = from_core(&health);
}

#[cfg(all(feature = "c-staticlib", not(test), not(kani)))]
#[panic_handler]
fn panic(_: &core::panic::PanicInfo) -> ! {
    // The core does not panic on valid input, and this interface checks its
    // input; if it ever does, stop rather than continue in an unknown state.
    loop {}
}

#[cfg(test)]
mod tests {
    use super::*;
    use core::mem::{align_of, size_of};

    /// The C test checks the same sizes (MRU-FFI-003).
    #[test]
    fn layout_matches_the_header() {
        assert_eq!(
            (size_of::<QuorumReply>(), align_of::<QuorumReply>()),
            (24, 8)
        );
        assert_eq!(
            (size_of::<QuorumVerdict>(), align_of::<QuorumVerdict>()),
            (24, 8)
        );
        assert_eq!(
            (size_of::<QuorumHealth>(), align_of::<QuorumHealth>()),
            (96, 8)
        );
    }

    #[test]
    fn known_cases() {
        let s = |v| QuorumReply {
            kind: QUORUM_SINGLE,
            first: v,
            second: 0,
        };
        let none = QuorumReply::default();
        let v = unsafe { quorum_decide(QUORUM_SHRINK, [s(7), s(9), s(7)].as_ptr()) };
        assert_eq!(
            (v.mode, v.outcome, v.value, v.dissent),
            (QUORUM_VOTE, QUORUM_DELIVER, 7, [false, true, false])
        );
        let v = unsafe { quorum_decide(QUORUM_SHRINK, [s(7), none, s(9)].as_ptr()) };
        assert_eq!(
            (v.mode, v.outcome, v.diagnose),
            (QUORUM_COMPARE, QUORUM_DETECTED, true)
        );
        let v = unsafe { quorum_decide(QUORUM_TMR, [none, s(7), none].as_ptr()) };
        assert_eq!(v, HALTED_VERDICT);
        let v = unsafe { quorum_decide(9, [s(7), s(7), s(7)].as_ptr()) };
        assert_eq!(v, HALTED_VERDICT);
        let v = unsafe { quorum_decide(QUORUM_SHRINK, core::ptr::null()) };
        assert_eq!(v, HALTED_VERDICT);
        assert_eq!(quorum_mode(QUORUM_SHRINK, 1), QUORUM_SELF_CHECK);
        assert_eq!(quorum_mode(QUORUM_TMR, 1), QUORUM_HALTED);
        assert_eq!(quorum_mode(7, 3), QUORUM_HALTED);
    }

    #[test]
    fn health_through_the_interface() {
        let mut h = QuorumHealth::default();
        unsafe {
            quorum_health_init(&mut h);
            assert!(!quorum_health_strike(&mut h, 0, 100));
            assert!(!quorum_health_strike(&mut h, 0, 150));
            assert!(quorum_health_strike(&mut h, 0, 250));
            quorum_health_clear(&mut h, 0);
            assert!(!quorum_health_strike(&mut h, 0, 1000));
            assert!(!quorum_health_strike(&mut h, 3, 1000));
            assert!(!quorum_health_strike(core::ptr::null_mut(), 0, 1000));
        }
        assert_eq!(h.count, [1, 0, 0]);
    }
}

/// Kani proofs: every function gives the core's answer (MRU-FFI-005).
/// Run with `cargo kani -p quorum-ffi`.
#[cfg(kani)]
mod proofs {
    use super::*;

    /// Two records hold the same values. Compared value by value: `==` on the
    /// struct becomes a byte-wise memcmp, which needs a much deeper loop bound.
    fn same(a: &QuorumHealth, b: &QuorumHealth) -> bool {
        let mut same = true;
        for i in 0..REPLICAS {
            for k in 0..STRIKES {
                same &= a.strikes[i][k] == b.strikes[i][k];
            }
            same &= a.count[i] == b.count[i];
        }
        same
    }

    fn any_reply() -> QuorumReply {
        QuorumReply {
            kind: kani::any(),
            first: kani::any(),
            second: kani::any(),
        }
    }

    /// For a valid policy, `quorum_decide` is the core's `decide`, with any
    /// unknown reply kind read as absent.
    #[kani::proof]
    #[kani::unwind(5)]
    fn decide_is_the_core() {
        let code: u8 = kani::any();
        kani::assume(code <= QUORUM_TMR);
        let r = [any_reply(), any_reply(), any_reply()];
        let got = unsafe { quorum_decide(code, r.as_ptr()) };
        let p = if code == QUORUM_SHRINK {
            Policy::Shrink
        } else {
            Policy::Tmr
        };
        assert_eq!(got, verdict(quorum::decide(p, &r.map(reply))));
    }

    /// Any other policy code halts.
    #[kani::proof]
    #[kani::unwind(5)]
    fn bad_policy_halts() {
        let code: u8 = kani::any();
        kani::assume(code > QUORUM_TMR);
        let r = [any_reply(), any_reply(), any_reply()];
        assert_eq!(unsafe { quorum_decide(code, r.as_ptr()) }, HALTED_VERDICT);
        assert_eq!(quorum_mode(code, kani::any()), QUORUM_HALTED);
    }

    /// `quorum_mode` is the core's `mode`.
    #[kani::proof]
    fn mode_is_the_core() {
        let code: u8 = kani::any();
        kani::assume(code <= QUORUM_TMR);
        let alive: u32 = kani::any();
        let p = if code == QUORUM_SHRINK {
            Policy::Shrink
        } else {
            Policy::Tmr
        };
        assert_eq!(
            quorum_mode(code, alive),
            mode_code(quorum::mode(p, alive as usize))
        );
    }

    /// `quorum_health_strike` gives the core's answer and leaves the core's
    /// record, for any record; an index above 2 changes nothing.
    #[kani::proof]
    #[kani::unwind(5)]
    fn strike_is_the_core() {
        let strikes: [[u64; STRIKES]; REPLICAS] = kani::any();
        let count: [u64; REPLICAS] = [
            kani::any::<u32>() as u64,
            kani::any::<u32>() as u64,
            kani::any::<u32>() as u64,
        ];
        let mut h = QuorumHealth { strikes, count };
        let before = h;
        let replica: u32 = kani::any();
        let cycle: u64 = kani::any();
        let got = unsafe { quorum_health_strike(&mut h, replica, cycle) };
        if (replica as usize) < REPLICAS {
            let mut core = to_core(&before);
            assert_eq!(got, core.strike(replica as usize, cycle));
            assert!(same(&h, &from_core(&core)));
        } else {
            assert!(!got);
            assert!(same(&h, &before));
        }
    }

    /// `quorum_health_clear` is the core's `clear`; an index above 2 changes nothing.
    #[kani::proof]
    #[kani::unwind(5)]
    fn clear_is_the_core() {
        let strikes: [[u64; STRIKES]; REPLICAS] = kani::any();
        let count: [u64; REPLICAS] = [
            kani::any::<u32>() as u64,
            kani::any::<u32>() as u64,
            kani::any::<u32>() as u64,
        ];
        let mut h = QuorumHealth { strikes, count };
        let before = h;
        let replica: u32 = kani::any();
        unsafe { quorum_health_clear(&mut h, replica) };
        if (replica as usize) < REPLICAS {
            let mut core = to_core(&before);
            core.clear(replica as usize);
            assert!(same(&h, &from_core(&core)));
        } else {
            assert!(same(&h, &before));
        }
    }
}
