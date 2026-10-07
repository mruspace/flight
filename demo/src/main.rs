//! quorum: run Mru's shrinking quorum (or fixed TMR) on three real replica
//! processes, inject faults, and report what was delivered.
//!
//! The voter is this program; each replica is this program started again with
//! `--replica`, talking over its stdin and stdout. All decisions come from the
//! `quorum` crate, the same `no_std` code meant for flight. Faults are injected
//! by the voter on a schedule or at random, so every run is reproducible from
//! its seed.

// Replicas are indexed by position because a lost replica is taken out of its slot.
#![allow(clippy::needless_range_loop)]

use quorum::{decide, Health, Mode, Outcome, Policy, Reply, REPLICAS};
use std::fs::File;
use std::io::{BufWriter, Read, Write};
use std::process::{Child, ChildStdin, ChildStdout, Command, Stdio};
use std::time::Instant;

// ---- the payload task: hash a pseudo-random 4 KiB block derived from the tick

fn xorshift(s: &mut u64) -> u64 {
    *s ^= *s << 13;
    *s ^= *s >> 7;
    *s ^= *s << 17;
    *s
}

fn task(tick: u64) -> u64 {
    let mut s = tick.wrapping_mul(0x9E37_79B9_7F4A_7C15).wrapping_add(1);
    let mut h: u64 = 0xcbf2_9ce4_8422_2325; // FNV-1a over the block
    for _ in 0..512 {
        let w = xorshift(&mut s);
        for b in 0..8 {
            h ^= (w >> (8 * b)) & 0xff;
            h = h.wrapping_mul(0x0100_0000_01b3);
        }
    }
    h
}

// ---- voter <-> replica protocol: 10-byte requests, 16-byte responses

const FLIP_FIRST: u8 = 1; // corrupt the (first) result
const SELF_CHECK: u8 = 2; // compute twice and return both results
const FLIP_SECOND: u8 = 4; // corrupt the second result too (common mode)
const STUCK: u8 = 8; // persistent fault: return a fixed wrong value

/// Known-answer test input; its result is computed once at start-up.
const KAT_INPUT: u64 = 0;

fn replica_main() -> ! {
    let mut input = std::io::stdin().lock();
    let mut output = std::io::stdout().lock();
    let mut q = [0u8; 10];
    while input.read_exact(&mut q).is_ok() {
        let tick = u64::from_le_bytes(q[..8].try_into().unwrap());
        let (flags, bit) = (q[8], q[9] % 64);
        let mut first = task(tick);
        let mut second = if flags & SELF_CHECK != 0 {
            task(tick)
        } else {
            0
        };
        if flags & FLIP_FIRST != 0 {
            first ^= 1 << bit;
        }
        if flags & FLIP_SECOND != 0 {
            second ^= 1 << bit;
        }
        if flags & STUCK != 0 {
            first = 0xdead_beef;
            second = 0xdead_beef;
        }
        let mut r = [0u8; 16];
        r[..8].copy_from_slice(&first.to_le_bytes());
        r[8..].copy_from_slice(&second.to_le_bytes());
        if output.write_all(&r).and_then(|_| output.flush()).is_err() {
            break;
        }
    }
    std::process::exit(0)
}

struct Replica {
    child: Child,
    to: ChildStdin,
    from: ChildStdout,
    stuck: bool,
}

impl Replica {
    fn spawn() -> Replica {
        let exe = std::env::current_exe().expect("own path");
        let mut child = Command::new(exe)
            .arg("--replica")
            .stdin(Stdio::piped())
            .stdout(Stdio::piped())
            .spawn()
            .expect("spawn replica");
        let to = child.stdin.take().unwrap();
        let from = child.stdout.take().unwrap();
        Replica {
            child,
            to,
            from,
            stuck: false,
        }
    }

    /// Send one request; `None` if the replica is gone.
    fn ask(&mut self, tick: u64, mut flags: u8, bit: u8) -> Option<(u64, u64)> {
        if self.stuck {
            flags |= STUCK;
        }
        let mut q = [0u8; 10];
        q[..8].copy_from_slice(&tick.to_le_bytes());
        q[8] = flags;
        q[9] = bit;
        self.to.write_all(&q).and_then(|_| self.to.flush()).ok()?;
        let mut r = [0u8; 16];
        self.from.read_exact(&mut r).ok()?;
        Some((
            u64::from_le_bytes(r[..8].try_into().unwrap()),
            u64::from_le_bytes(r[8..].try_into().unwrap()),
        ))
    }

    fn known_answer_ok(&mut self, expected: u64) -> bool {
        self.ask(KAT_INPUT, 0, 0).map(|(a, _)| a) == Some(expected)
    }

    fn retire(mut self) {
        let _ = self.child.kill();
        let _ = self.child.wait();
    }
}

fn uniform(rng: &mut u64) -> f64 {
    (xorshift(rng) >> 11) as f64 * (1.0 / 9_007_199_254_740_992.0)
}

// ---- options and faults

#[derive(Clone, Copy, PartialEq)]
enum FaultKind {
    Kill,
    Corrupt,
    Stuck,
}

struct Fault {
    kind: FaultKind,
    replica: usize,
    tick: u64,
}

struct Options {
    policy: Policy,
    ticks: u64,
    seed: u64,
    upset_rate: f64,
    common_mode: f64,
    kat_period: u64,
    faults: Vec<Fault>,
    log: Option<String>,
}

fn usage() -> ! {
    eprintln!(
        "usage: quorum [--policy shrink|tmr] [--ticks N] [--seed S]\n\
         \x20             [--upset-rate R] [--common-mode C] [--kat-period P]\n\
         \x20             [--fault kill:I@T] [--fault corrupt:I@T] [--fault stuck:I@T] ...\n\
         \x20             [--log file.csv]"
    );
    std::process::exit(2)
}

fn parse_fault(s: &str) -> Option<Fault> {
    let (kind, rest) = s.split_once(':')?;
    let (replica, tick) = rest.split_once('@')?;
    let kind = match kind {
        "kill" => FaultKind::Kill,
        "corrupt" => FaultKind::Corrupt,
        "stuck" => FaultKind::Stuck,
        _ => return None,
    };
    let replica: usize = replica.parse().ok()?;
    (replica < REPLICAS).then_some(Fault {
        kind,
        replica,
        tick: tick.parse().ok()?,
    })
}

fn parse() -> Options {
    let mut o = Options {
        policy: Policy::Shrink,
        ticks: 10_000,
        seed: 1,
        upset_rate: 0.0,
        common_mode: 0.05,
        kat_period: 64,
        faults: Vec::new(),
        log: None,
    };
    let mut args = std::env::args().skip(1);
    while let Some(a) = args.next() {
        let mut next = || args.next().unwrap_or_else(|| usage());
        match a.as_str() {
            "--policy" => {
                o.policy = match next().as_str() {
                    "shrink" => Policy::Shrink,
                    "tmr" => Policy::Tmr,
                    _ => usage(),
                }
            }
            "--ticks" => o.ticks = next().parse().unwrap_or_else(|_| usage()),
            "--seed" => o.seed = next().parse().unwrap_or_else(|_| usage()),
            "--upset-rate" => o.upset_rate = next().parse().unwrap_or_else(|_| usage()),
            "--common-mode" => o.common_mode = next().parse().unwrap_or_else(|_| usage()),
            "--kat-period" => o.kat_period = next().parse().unwrap_or_else(|_| usage()),
            "--fault" => o
                .faults
                .push(parse_fault(&next()).unwrap_or_else(|| usage())),
            "--log" => o.log = Some(next()),
            _ => usage(),
        }
    }
    o
}

// ---- footprint: getrusage without dependencies (same layout on 64-bit Linux and macOS)

#[repr(C)]
#[derive(Default)]
struct Timeval {
    sec: i64,
    usec: i64,
}

#[repr(C)]
#[derive(Default)]
struct Rusage {
    utime: Timeval,
    stime: Timeval,
    maxrss: i64,
    rest: [i64; 13],
}

extern "C" {
    fn getrusage(who: i32, usage: *mut Rusage) -> i32;
}

/// Peak resident memory in KiB and CPU seconds, for this process (0) or its
/// waited-for children (-1).
fn usage_of(who: i32) -> (i64, f64) {
    let mut u = Rusage::default();
    // SAFETY: getrusage writes a struct rusage, which has this layout on
    // 64-bit Linux and macOS.
    unsafe { getrusage(who, &mut u) };
    let kib = if cfg!(target_os = "macos") {
        u.maxrss / 1024
    } else {
        u.maxrss
    };
    let usec = |t: &Timeval| (t.usec & 0xffff_ffff) as f64 / 1e6; // tv_usec is 32-bit on macOS
    (
        kib,
        u.utime.sec as f64 + usec(&u.utime) + u.stime.sec as f64 + usec(&u.stime),
    )
}

// ---- run

fn main() {
    if std::env::args().nth(1).as_deref() == Some("--replica") {
        replica_main();
    }
    let o = parse();
    let mut log = o
        .log
        .as_ref()
        .map(|p| BufWriter::new(File::create(p).expect("log file")));
    if let Some(l) = log.as_mut() {
        let _ = writeln!(l, "tick,event,detail");
    }
    let mut event = |tick: u64, what: &str, detail: String| {
        if let Some(l) = log.as_mut() {
            let _ = writeln!(l, "{tick},{what},{detail}");
        }
    };

    let mut replicas: Vec<Option<Replica>> =
        (0..REPLICAS).map(|_| Some(Replica::spawn())).collect();
    let mut health = Health::new();
    let kat = task(KAT_INPUT);
    let mut rng = o.seed.wrapping_mul(0x9E37_79B9_7F4A_7C15).wrapping_add(7);

    let (mut useful, mut detected, mut wrong, mut halted_at) = (0u64, 0u64, 0u64, None);
    let start = Instant::now();

    for tick in 1..=o.ticks {
        // scheduled faults
        let mut corrupt = [false; REPLICAS];
        for f in o.faults.iter().filter(|f| f.tick == tick) {
            if replicas[f.replica].is_none() {
                continue;
            }
            match f.kind {
                FaultKind::Kill => {
                    replicas[f.replica].take().unwrap().retire();
                    event(tick, "killed", f.replica.to_string());
                }
                FaultKind::Corrupt => corrupt[f.replica] = true,
                FaultKind::Stuck => {
                    replicas[f.replica].as_mut().unwrap().stuck = true;
                    event(tick, "stuck", f.replica.to_string());
                }
            }
        }

        let alive = replicas.iter().filter(|r| r.is_some()).count();
        let mode = quorum::mode(o.policy, alive);
        if mode == Mode::Halted {
            halted_at = Some(tick);
            event(tick, "halted", alive.to_string());
            break;
        }
        // self-check computes everything twice, so it delivers at half rate
        if mode == Mode::SelfCheck && tick % 2 == 1 {
            continue;
        }

        let mut replies = [Reply::Absent; REPLICAS];
        for i in 0..REPLICAS {
            let Some(r) = replicas[i].as_mut() else {
                continue;
            };
            let bit = (xorshift(&mut rng) & 63) as u8;
            let upset = corrupt[i] || (o.upset_rate > 0.0 && uniform(&mut rng) < o.upset_rate);
            let mut flags = 0;
            if mode == Mode::SelfCheck {
                flags |= SELF_CHECK;
                if upset {
                    flags |= FLIP_FIRST;
                    if uniform(&mut rng) < o.common_mode {
                        flags |= FLIP_SECOND; // both runs hit alike: this can slip through
                    }
                }
            } else if upset {
                flags |= FLIP_FIRST;
            }
            match r.ask(tick, flags, bit) {
                Some((a, b)) => {
                    replies[i] = if mode == Mode::SelfCheck {
                        Reply::Pair(a, b)
                    } else {
                        Reply::Single(a)
                    }
                }
                None => {
                    replicas[i].take().unwrap().retire(); // the replica died on its own
                    event(tick, "lost", i.to_string());
                }
            }
        }

        let verdict = decide(o.policy, &replies);
        let mut outcome = verdict.outcome;

        if o.policy == Policy::Shrink {
            // health scoring: three outvotes in the window trigger a known-answer
            // test; passing clears the strikes (the upsets were transient),
            // failing retires the replica
            for i in 0..REPLICAS {
                if !verdict.dissent[i] || !health.strike(i, tick) {
                    continue;
                }
                let ok = replicas[i].as_mut().is_some_and(|r| r.known_answer_ok(kat));
                if ok {
                    health.clear(i);
                    event(tick, "cleared", i.to_string());
                } else if let Some(r) = replicas[i].take() {
                    r.retire();
                    event(tick, "retired", i.to_string());
                }
            }
            // two replicas disagree: the known-answer test tells which failed
            if verdict.diagnose {
                for i in 0..REPLICAS {
                    let failed = replicas[i]
                        .as_mut()
                        .is_some_and(|r| !r.known_answer_ok(kat));
                    if failed {
                        replicas[i].take().unwrap().retire();
                        event(tick, "retired", i.to_string());
                    }
                }
            }
            // on one replica, a periodic known-answer test catches a stuck fault
            if verdict.mode == Mode::SelfCheck && tick % o.kat_period.max(1) == 0 {
                for i in 0..REPLICAS {
                    let failed = replicas[i]
                        .as_mut()
                        .is_some_and(|r| !r.known_answer_ok(kat));
                    if failed {
                        replicas[i].take().unwrap().retire();
                        event(tick, "retired", i.to_string());
                        outcome = Outcome::Detected;
                    }
                }
            }
        }

        match outcome {
            Outcome::Deliver(v) if v == task(tick) => useful += 1,
            Outcome::Deliver(_) => {
                wrong += 1;
                event(tick, "wrong", format!("{:?}", verdict.mode));
            }
            Outcome::Detected => {
                detected += 1;
                event(tick, "detected", format!("{:?}", verdict.mode));
            }
            Outcome::Halted => {}
        }
    }

    let alive_at_end = replicas.iter().filter(|r| r.is_some()).count();
    for r in replicas.into_iter().flatten() {
        drop(r.to); // end of input: the replica exits
        let mut child = r.child;
        let _ = child.wait();
    }
    let wall = start.elapsed().as_secs_f64();
    let (voter_kib, voter_cpu) = usage_of(0);
    let (replica_kib, replica_cpu) = usage_of(-1);

    let policy = if o.policy == Policy::Shrink {
        "shrink"
    } else {
        "tmr"
    };
    println!(
        "policy={policy} ticks={} seed={} upset_rate={}",
        o.ticks, o.seed, o.upset_rate
    );
    println!(
        "useful={useful} detected={detected} wrong={wrong} halted_at={} alive_at_end={alive_at_end}",
        halted_at.map_or("never".to_string(), |t| t.to_string())
    );
    println!(
        "footprint: voter_max_rss={voter_kib}KiB replica_max_rss={replica_kib}KiB cpu={:.2}s wall={wall:.2}s",
        voter_cpu + replica_cpu
    );
}
