//! Gate commands on the mode. Only a result that two or more replicas agree
//! on (vote or compare) may command the spacecraft. A result from the last
//! replica's self-check goes out as a data product only.
use quorum::{decide, Mode, Outcome, Policy, Reply, REPLICAS};

/// What to do with one tick's result.
enum Action {
    /// Agreed by two or more replicas: may be used as a command.
    Command(u64),
    /// Self-check on the last replica: data product only.
    Data(u64),
    /// Nothing to send this tick.
    Hold,
}

fn gate(replies: &[Reply; REPLICAS]) -> Action {
    let verdict = decide(Policy::Shrink, replies);
    match (verdict.mode, verdict.outcome) {
        (Mode::Vote | Mode::Compare, Outcome::Deliver(x)) => Action::Command(x),
        (Mode::SelfCheck, Outcome::Deliver(x)) => Action::Data(x),
        _ => Action::Hold,
    }
}

fn main() {
    use Reply::{Absent, Pair, Single};
    let ticks = [
        [Single(7), Single(7), Single(9)], // vote: two agree on 7
        [Absent, Single(7), Single(7)],    // compare: both say 7
        [Absent, Single(7), Single(9)],    // compare: mismatch
        [Absent, Absent, Pair(7, 7)],      // self-check on the last replica
        [Absent, Absent, Absent],          // nothing left
    ];
    for replies in &ticks {
        match gate(replies) {
            Action::Command(x) => println!("{replies:?}: command {x}"),
            Action::Data(x) => println!("{replies:?}: data product {x}"),
            Action::Hold => println!("{replies:?}: hold"),
        }
    }
}
