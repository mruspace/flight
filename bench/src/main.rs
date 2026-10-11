//! bench: time the `quorum` decision core on this host.
//!
//! Each case calls the core in a loop and reports the median of several
//! timed rounds, in nanoseconds per call. Inputs pass through
//! `std::hint::black_box` so the compiler cannot fold the work away. No
//! dependencies, so the numbers come from the same code that runs in flight.
//!
//! Output is CSV on stdout: `case,policy,mode,ns_per_call`. Each figure
//! includes the cost of the loop and of copying the replies, so it is an
//! upper bound on the core's own cost.

use quorum::{decide, Health, Policy, Reply, REPLICAS};
use std::hint::black_box;
use std::time::Instant;

const CALLS: u64 = 2_000_000;
const ROUNDS: usize = 9;

/// Median nanoseconds per call of `f` over `ROUNDS` rounds of `CALLS` calls.
fn time(mut f: impl FnMut(u64)) -> f64 {
    for i in 0..CALLS / 10 {
        f(i); // warm up
    }
    let mut rounds: Vec<f64> = (0..ROUNDS)
        .map(|_| {
            let start = Instant::now();
            for i in 0..CALLS {
                f(i);
            }
            start.elapsed().as_nanos() as f64 / CALLS as f64
        })
        .collect();
    rounds.sort_by(f64::total_cmp);
    rounds[ROUNDS / 2]
}

fn main() {
    // replies by number of live replicas and whether they agree
    let cases: [(&str, [Reply; REPLICAS]); 6] = [
        ("three_agree", [Reply::Single(7); REPLICAS]),
        (
            "three_one_outvoted",
            [Reply::Single(7), Reply::Single(9), Reply::Single(7)],
        ),
        (
            "two_agree",
            [Reply::Single(7), Reply::Absent, Reply::Single(7)],
        ),
        (
            "two_disagree",
            [Reply::Single(7), Reply::Absent, Reply::Single(9)],
        ),
        (
            "one_pair_agree",
            [Reply::Absent, Reply::Pair(7, 7), Reply::Absent],
        ),
        ("none", [Reply::Absent; REPLICAS]),
    ];

    println!("case,policy,mode,ns_per_call");
    for (name, replies) in cases {
        for (policy, label) in [(Policy::Shrink, "shrink"), (Policy::Tmr, "tmr")] {
            let mode = decide(policy, &replies).mode;
            let ns = time(|i| {
                let mut r = black_box(replies);
                // vary one value per call so no result can be reused
                if let Reply::Single(v) = &mut r[0] {
                    *v ^= i & 1;
                }
                black_box(decide(black_box(policy), &r));
            });
            println!("decide_{name},{label},{mode:?},{ns:.2}");
        }
    }

    let mut health = Health::new();
    let ns = time(|i| {
        black_box(health.strike(black_box((i % 3) as usize), black_box(i)));
    });
    println!("health_strike,any,none,{ns:.2}");
}
