//! quorum: run Mru's shrinking quorum (or fixed TMR) on three real replica
//! processes, inject faults, and report what was delivered.
//!
//! The voter is this program; each replica is this program started again with
//! `--replica`, talking over its stdin and stdout. All decisions come from the
//! `quorum` crate, the same `no_std` code meant for flight. Faults are injected
//! by the voter on a schedule or at random, so every run is reproducible from
//! its seed.
//!
//! Safety for running on shared hardware (see docs/opssat.md): every process
//! enforces a memory limit and an optional CPU-time limit on itself, the voter
//! can throttle its CPU share, stops cleanly on SIGTERM or SIGINT or after a
//! maximum run time, and treats a replica that stops answering as lost.
//! Replicas exit on their own when the voter goes away.

// Replicas are indexed by position because a lost replica is taken out of its slot.
#![allow(clippy::needless_range_loop)]

use quorum::{decide, Health, Mode, Outcome, Policy, Reply, REPLICAS};
use std::fs::File;
use std::io::{BufWriter, Read, Write};
use std::os::fd::AsRawFd;
use std::process::{Child, ChildStdin, ChildStdout, Command, Stdio};
use std::sync::atomic::{AtomicBool, Ordering};
use std::time::{Duration, Instant};

// ---- the payload task: hash a 4 KiB block of working memory derived from the tick

const BLOCK_WORDS: usize = 512;

fn xorshift(s: &mut u64) -> u64 {
    *s ^= *s << 13;
    *s ^= *s >> 7;
    *s ^= *s << 17;
    *s
}

/// Fill the working block for `tick`, apply an optional upset (a bit flip in
/// working memory, as radiation would cause), and hash the block.
fn compute(block: &mut [u64; BLOCK_WORDS], tick: u64, upset: Option<(usize, u32)>) -> u64 {
    let mut s = tick.wrapping_mul(0x9E37_79B9_7F4A_7C15).wrapping_add(1);
    for w in block.iter_mut() {
        *w = xorshift(&mut s);
    }
    if let Some((word, bit)) = upset {
        block[word % BLOCK_WORDS] ^= 1 << (bit % 64);
    }
    let mut h: u64 = 0xcbf2_9ce4_8422_2325; // FNV-1a over the block
    for w in block.iter() {
        for b in 0..8 {
            h ^= (w >> (8 * b)) & 0xff;
            h = h.wrapping_mul(0x0100_0000_01b3);
        }
    }
    h
}

/// The correct result for `tick`, computed by the voter for scoring only.
fn truth(tick: u64) -> u64 {
    compute(&mut [0; BLOCK_WORDS], tick, None)
}

// ---- voter <-> replica protocol: 12-byte requests, 16-byte responses

const UPSET_FIRST: u8 = 1; // flip a bit in working memory during the (first) run
const SELF_CHECK: u8 = 2; // compute twice and return both results
const UPSET_SECOND: u8 = 4; // the same flip during the second run too (common mode)
const STUCK: u8 = 8; // persistent fault: return a fixed wrong value
const HANG: u8 = 16; // persistent fault: stop answering

/// Known-answer test input; its result is computed once at start-up.
const KAT_INPUT: u64 = 0;

fn replica_main(limits: Limits) -> ! {
    limits.apply();
    let mut input = std::io::stdin().lock();
    let mut output = std::io::stdout().lock();
    let mut block = Box::new([0u64; BLOCK_WORDS]); // working memory, allocated once
    let mut q = [0u8; 12];
    while input.read_exact(&mut q).is_ok() {
        let tick = u64::from_le_bytes(q[..8].try_into().unwrap());
        let (flags, bit) = (q[8], u32::from(q[9]));
        let word = usize::from(u16::from_le_bytes([q[10], q[11]]));
        if flags & HANG != 0 {
            loop {
                std::thread::sleep(Duration::from_secs(3600));
            }
        }
        let at = (word, bit);
        let mut first = compute(&mut block, tick, (flags & UPSET_FIRST != 0).then_some(at));
        let mut second = if flags & SELF_CHECK != 0 {
            compute(&mut block, tick, (flags & UPSET_SECOND != 0).then_some(at))
        } else {
            0
        };
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
    // end of input: the voter has finished or gone away
    std::process::exit(0)
}

struct Replica {
    child: Child,
    to: ChildStdin,
    from: ChildStdout,
    stuck: bool,
    hung: bool,
}

impl Replica {
    fn spawn(limits: Limits) -> Replica {
        let exe = std::env::current_exe().expect("own path");
        let mut child = Command::new(exe)
            .args([
                "--replica",
                "--max-memory-mb",
                &limits.memory_mb.to_string(),
            ])
            .args(["--max-cpu-seconds", &limits.cpu_seconds.to_string()])
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
            hung: false,
        }
    }

    /// Send one request; `None` if the replica is gone or does not answer in time.
    fn ask(
        &mut self,
        tick: u64,
        mut flags: u8,
        bit: u8,
        word: u16,
        timeout: Duration,
    ) -> Option<(u64, u64)> {
        if self.stuck {
            flags |= STUCK;
        }
        if self.hung {
            flags |= HANG;
        }
        let mut q = [0u8; 12];
        q[..8].copy_from_slice(&tick.to_le_bytes());
        q[8] = flags;
        q[9] = bit;
        q[10..].copy_from_slice(&word.to_le_bytes());
        self.to.write_all(&q).and_then(|_| self.to.flush()).ok()?;
        let mut r = [0u8; 16];
        read_exact_timeout(&mut self.from, &mut r, timeout)?;
        Some((
            u64::from_le_bytes(r[..8].try_into().unwrap()),
            u64::from_le_bytes(r[8..].try_into().unwrap()),
        ))
    }

    fn known_answer_ok(&mut self, expected: u64, timeout: Duration) -> bool {
        self.ask(KAT_INPUT, 0, 0, 0, timeout).map(|(a, _)| a) == Some(expected)
    }

    fn retire(mut self) {
        let _ = self.child.kill();
        let _ = self.child.wait();
    }
}

/// Read exactly `buf.len()` bytes, giving up after `timeout` without data.
fn read_exact_timeout(from: &mut ChildStdout, buf: &mut [u8], timeout: Duration) -> Option<()> {
    let ms = libc::c_int::try_from(timeout.as_millis()).unwrap_or(libc::c_int::MAX);
    let mut filled = 0;
    while filled < buf.len() {
        let mut p = libc::pollfd {
            fd: from.as_raw_fd(),
            events: libc::POLLIN,
            revents: 0,
        };
        // SAFETY: one valid pollfd, owned by this frame.
        let ready = unsafe { libc::poll(&mut p, 1, ms) };
        if ready <= 0 {
            return None; // timed out or failed
        }
        match from.read(&mut buf[filled..]) {
            Ok(0) | Err(_) => return None,
            Ok(n) => filled += n,
        }
    }
    Some(())
}

// ---- resource limits, enforced by each process on itself

#[derive(Clone, Copy)]
struct Limits {
    /// Address-space limit per process, in MB (enforced on Linux).
    memory_mb: u64,
    /// CPU-time limit per process, in seconds; 0 means none.
    cpu_seconds: u64,
}

impl Limits {
    fn apply(self) {
        fn set(resource: libc::c_int, value: u64) {
            let v = libc::rlim_t::try_from(value).unwrap_or(libc::RLIM_INFINITY);
            let r = libc::rlimit {
                rlim_cur: v,
                rlim_max: v,
            };
            // SAFETY: a valid rlimit for this process.
            unsafe { libc::setrlimit(resource as _, &r) };
        }
        #[cfg(target_os = "linux")]
        if self.memory_mb > 0 {
            set(libc::RLIMIT_AS as libc::c_int, self.memory_mb * 1024 * 1024);
        }
        if self.cpu_seconds > 0 {
            set(libc::RLIMIT_CPU as libc::c_int, self.cpu_seconds);
        }
    }
}

// ---- clean stop on SIGTERM or SIGINT

static STOP: AtomicBool = AtomicBool::new(false);

extern "C" fn on_signal(_: libc::c_int) {
    STOP.store(true, Ordering::SeqCst);
}

fn install_stop_handlers() {
    // SAFETY: the handler only stores to an atomic, which is async-signal-safe.
    unsafe {
        libc::signal(libc::SIGTERM, on_signal as libc::sighandler_t);
        libc::signal(libc::SIGINT, on_signal as libc::sighandler_t);
    }
}

fn uniform(rng: &mut u64) -> f64 {
    (xorshift(rng) >> 11) as f64 * (1.0 / 9_007_199_254_740_992.0)
}

// ---- radiation sensor: a block of memory with a known pattern, checked for flips

/// Holds `mb` megabytes of a known, address-dependent pattern. Each scan
/// reads every word back and reports and repairs any bit that changed, so on
/// hardware without error-correcting memory it measures the real upset rate.
struct Sensor {
    words: Vec<u64>,
    scans: u64,
    flips: u64,
}

impl Sensor {
    fn pattern(i: usize) -> u64 {
        (i as u64).wrapping_mul(0x9E37_79B9_7F4A_7C15) ^ 0x5555_5555_5555_5555
    }

    fn new(mb: u64) -> Sensor {
        let n = (mb as usize) * 1024 * 1024 / 8;
        let words = (0..n).map(Sensor::pattern).collect(); // written, so resident
        Sensor {
            words,
            scans: 0,
            flips: 0,
        }
    }

    /// Check every word; call `report(word, bit)` for each flipped bit and repair it.
    fn scan(&mut self, mut report: impl FnMut(usize, u32)) {
        self.scans += 1;
        for (i, w) in self.words.iter_mut().enumerate() {
            // SAFETY: a valid, aligned pointer into our own vector; volatile so
            // the read really goes to memory.
            let seen = unsafe { std::ptr::read_volatile(w) };
            let diff = seen ^ Sensor::pattern(i);
            if diff != 0 {
                for bit in 0..64 {
                    if diff >> bit & 1 == 1 {
                        report(i, bit);
                        self.flips += 1;
                    }
                }
                *w = Sensor::pattern(i);
            }
        }
    }

    /// Flip one bit, as a test of the sensor itself.
    fn inject(&mut self, word: usize, bit: u32) {
        let n = self.words.len();
        if n > 0 {
            self.words[word % n] ^= 1 << (bit % 64);
        }
    }
}

// ---- options and faults

#[derive(Clone, Copy, PartialEq)]
enum FaultKind {
    Kill,
    Corrupt,
    Stuck,
    Hang,
    /// Flip a bit in the radiation sensor's memory (tests the sensor).
    Sensor,
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
    limits: Limits,
    cpu_percent: f64,
    max_seconds: f64,
    reply_timeout: Duration,
    summary: Option<String>,
    sensor_mb: u64,
    sensor_every: u64,
    progress_every: u64,
}

fn usage() -> ! {
    eprintln!(
        "usage: quorum [--policy shrink|tmr] [--ticks N] [--seed S]\n\
         \x20             [--upset-rate R] [--common-mode C] [--kat-period P]\n\
         \x20             [--fault kill|corrupt|stuck|hang|sensor:I@T] ... [--faults-file F]\n\
         \x20             [--max-memory-mb M] [--max-cpu-seconds S] [--cpu-percent P]\n\
         \x20             [--max-seconds S] [--reply-timeout-ms T]\n\
         \x20             [--sensor-mb M] [--sensor-every N]\n\
         \x20             [--log file.csv] [--progress-every N] [--summary file.txt]"
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
        "hang" => FaultKind::Hang,
        "sensor" => FaultKind::Sensor,
        _ => return None,
    };
    let replica: usize = replica.parse().ok()?;
    (replica < REPLICAS).then_some(Fault {
        kind,
        replica,
        tick: tick.parse().ok()?,
    })
}

fn parse_limits(args: &[String]) -> Limits {
    let mut limits = Limits {
        memory_mb: 128,
        cpu_seconds: 0,
    };
    let mut i = 0;
    while i + 1 < args.len() {
        match args[i].as_str() {
            "--max-memory-mb" => limits.memory_mb = args[i + 1].parse().unwrap_or_else(|_| usage()),
            "--max-cpu-seconds" => {
                limits.cpu_seconds = args[i + 1].parse().unwrap_or_else(|_| usage())
            }
            _ => {}
        }
        i += 1;
    }
    limits
}

fn parse(args: &[String]) -> Options {
    let mut o = Options {
        policy: Policy::Shrink,
        ticks: 10_000,
        seed: 1,
        upset_rate: 0.0,
        common_mode: 0.05,
        kat_period: 64,
        faults: Vec::new(),
        log: None,
        limits: parse_limits(args),
        cpu_percent: 0.0,
        max_seconds: 0.0,
        reply_timeout: Duration::from_millis(2000),
        summary: None,
        sensor_mb: 0,
        sensor_every: 1000,
        progress_every: 0,
    };
    let mut args = args.iter();
    while let Some(a) = args.next() {
        let mut next = || args.next().cloned().unwrap_or_else(|| usage());
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
            "--summary" => o.summary = Some(next()),
            "--faults-file" => {
                let text = std::fs::read_to_string(next()).unwrap_or_else(|_| usage());
                for line in text.lines().map(str::trim) {
                    if line.is_empty() || line.starts_with('#') {
                        continue;
                    }
                    o.faults.push(parse_fault(line).unwrap_or_else(|| usage()));
                }
            }
            "--sensor-mb" => o.sensor_mb = next().parse().unwrap_or_else(|_| usage()),
            "--sensor-every" => o.sensor_every = next().parse().unwrap_or_else(|_| usage()),
            "--progress-every" => o.progress_every = next().parse().unwrap_or_else(|_| usage()),
            "--max-memory-mb" | "--max-cpu-seconds" => {
                next(); // read by parse_limits
            }
            "--cpu-percent" => o.cpu_percent = next().parse().unwrap_or_else(|_| usage()),
            "--max-seconds" => o.max_seconds = next().parse().unwrap_or_else(|_| usage()),
            "--reply-timeout-ms" => {
                o.reply_timeout = Duration::from_millis(next().parse().unwrap_or_else(|_| usage()))
            }
            _ => usage(),
        }
    }
    // the sensor lives in the voter, inside its memory limit, with room to spare
    if o.sensor_mb > 0 && o.limits.memory_mb > 0 && o.sensor_mb + 32 > o.limits.memory_mb {
        eprintln!(
            "--sensor-mb {} does not fit under --max-memory-mb {} (leave at least 32 MB)",
            o.sensor_mb, o.limits.memory_mb
        );
        std::process::exit(2);
    }
    o
}

// ---- footprint

/// Peak resident memory in KiB and CPU seconds, for this process or its
/// waited-for children.
fn usage_of(who: libc::c_int) -> (i64, f64) {
    // SAFETY: getrusage fills a zeroed rusage owned by this frame.
    let u = unsafe {
        let mut u: libc::rusage = std::mem::zeroed();
        libc::getrusage(who, &mut u);
        u
    };
    #[allow(clippy::useless_conversion)] // c_long is 32-bit on 32-bit targets
    let rss = i64::from(u.ru_maxrss);
    let kib = if cfg!(target_os = "macos") {
        rss / 1024
    } else {
        rss
    };
    let secs = |t: libc::timeval| t.tv_sec as f64 + t.tv_usec as f64 / 1e6;
    (kib, secs(u.ru_utime) + secs(u.ru_stime))
}

// ---- run

fn main() {
    let args: Vec<String> = std::env::args().skip(1).collect();
    if args.first().map(String::as_str) == Some("--replica") {
        replica_main(parse_limits(&args));
    }
    let o = parse(&args);
    o.limits.apply();
    install_stop_handlers();

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

    let mut replicas: Vec<Option<Replica>> = (0..REPLICAS)
        .map(|_| Some(Replica::spawn(o.limits)))
        .collect();
    let mut health = Health::new();
    let mut sensor = (o.sensor_mb > 0).then(|| Sensor::new(o.sensor_mb));
    let kat = truth(KAT_INPUT);
    let timeout = o.reply_timeout;
    let mut rng = o.seed.wrapping_mul(0x9E37_79B9_7F4A_7C15).wrapping_add(7);

    let (mut useful, mut detected, mut wrong, mut halted_at) = (0u64, 0u64, 0u64, None);
    let mut stopped = "completed";
    let start = Instant::now();
    let mut last_tick = 0;
    let progress = |useful: u64, detected: u64, wrong: u64, alive: usize| {
        format!("useful={useful} detected={detected} wrong={wrong} alive={alive}")
    };

    for tick in 1..=o.ticks {
        // progress: the running totals up to the previous tick, for plots and
        // for a heartbeat in the downlinked log
        if o.progress_every > 0 && last_tick > 0 && last_tick % o.progress_every == 0 {
            let alive = replicas.iter().filter(|r| r.is_some()).count();
            event(
                last_tick,
                "progress",
                progress(useful, detected, wrong, alive),
            );
        }
        if STOP.load(Ordering::SeqCst) {
            stopped = "signal";
            event(tick, "stopped", stopped.into());
            break;
        }
        if o.max_seconds > 0.0 && start.elapsed().as_secs_f64() >= o.max_seconds {
            stopped = "time-limit";
            event(tick, "stopped", stopped.into());
            break;
        }
        let tick_start = Instant::now();

        // scheduled faults
        let mut corrupt = [false; REPLICAS];
        for f in o.faults.iter().filter(|f| f.tick == tick) {
            if f.kind == FaultKind::Sensor {
                if let Some(s) = sensor.as_mut() {
                    s.inject(tick as usize, (tick % 64) as u32);
                    event(tick, "sensor_injected", String::new());
                }
                continue;
            }
            let Some(r) = replicas[f.replica].as_mut() else {
                continue;
            };
            match f.kind {
                FaultKind::Kill => {
                    replicas[f.replica].take().unwrap().retire();
                    event(tick, "killed", f.replica.to_string());
                }
                FaultKind::Corrupt => corrupt[f.replica] = true,
                FaultKind::Stuck => {
                    r.stuck = true;
                    event(tick, "stuck", f.replica.to_string());
                }
                FaultKind::Hang => {
                    r.hung = true;
                    event(tick, "hang", f.replica.to_string());
                }
                FaultKind::Sensor => {}
            }
        }

        let alive = replicas.iter().filter(|r| r.is_some()).count();
        let mode = quorum::mode(o.policy, alive);
        if mode == Mode::Halted {
            halted_at = Some(tick);
            stopped = "halted";
            event(tick, "halted", alive.to_string());
            break;
        }
        last_tick = tick;
        // self-check computes everything twice, so it delivers at half rate
        if mode == Mode::SelfCheck && tick % 2 == 1 {
            continue;
        }

        let mut replies = [Reply::Absent; REPLICAS];
        for i in 0..REPLICAS {
            let Some(r) = replicas[i].as_mut() else {
                continue;
            };
            let draw = xorshift(&mut rng);
            let (bit, word) = ((draw & 63) as u8, ((draw >> 6) % BLOCK_WORDS as u64) as u16);
            let upset = corrupt[i] || (o.upset_rate > 0.0 && uniform(&mut rng) < o.upset_rate);
            let mut flags = 0;
            if mode == Mode::SelfCheck {
                flags |= SELF_CHECK;
                if upset {
                    flags |= UPSET_FIRST;
                    if uniform(&mut rng) < o.common_mode {
                        flags |= UPSET_SECOND; // both runs hit alike: this can slip through
                    }
                }
            } else if upset {
                flags |= UPSET_FIRST;
            }
            match r.ask(tick, flags, bit, word, timeout) {
                Some((a, b)) => {
                    replies[i] = if mode == Mode::SelfCheck {
                        Reply::Pair(a, b)
                    } else {
                        Reply::Single(a)
                    }
                }
                None => {
                    // died or stopped answering within the timeout
                    replicas[i].take().unwrap().retire();
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
                let ok = replicas[i]
                    .as_mut()
                    .is_some_and(|r| r.known_answer_ok(kat, timeout));
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
                        .is_some_and(|r| !r.known_answer_ok(kat, timeout));
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
                        .is_some_and(|r| !r.known_answer_ok(kat, timeout));
                    if failed {
                        replicas[i].take().unwrap().retire();
                        event(tick, "retired", i.to_string());
                        outcome = Outcome::Detected;
                    }
                }
            }
        }

        match outcome {
            Outcome::Deliver(v) if v == truth(tick) => useful += 1,
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

        // radiation sensor: check the pattern every sensor_every ticks
        if let Some(s) = sensor.as_mut() {
            if tick % o.sensor_every.max(1) == 0 {
                s.scan(|word, bit| event(tick, "sensor_upset", format!("{word}:{bit}")));
            }
        }

        // CPU share: sleep so that work takes at most cpu_percent of wall time
        if o.cpu_percent > 0.0 && o.cpu_percent < 100.0 {
            let work = tick_start.elapsed();
            std::thread::sleep(work.mul_f64((100.0 - o.cpu_percent) / o.cpu_percent));
        }
    }

    let alive_at_end = replicas.iter().filter(|r| r.is_some()).count();
    if o.progress_every > 0 && last_tick > 0 {
        event(
            last_tick,
            "progress",
            progress(useful, detected, wrong, alive_at_end),
        );
    }
    for r in replicas.into_iter().flatten() {
        if r.hung {
            r.retire();
            continue;
        }
        drop(r.to); // end of input: the replica exits
        let mut child = r.child;
        let _ = child.wait();
    }
    let wall = start.elapsed().as_secs_f64();
    let (voter_kib, voter_cpu) = usage_of(libc::RUSAGE_SELF);
    let (replica_kib, replica_cpu) = usage_of(libc::RUSAGE_CHILDREN);

    let policy = if o.policy == Policy::Shrink {
        "shrink"
    } else {
        "tmr"
    };
    let none = || "none".to_string();
    let mut summary = vec![
        format!("policy={policy} ticks={} seed={} upset_rate={}", o.ticks, o.seed, o.upset_rate),
        format!(
            "useful={useful} detected={detected} wrong={wrong} halted_at={} alive_at_end={alive_at_end} stopped={stopped}",
            halted_at.map_or("never".to_string(), |t| t.to_string())
        ),
        format!(
            "footprint: voter_max_rss={voter_kib}KiB replica_max_rss={replica_kib}KiB cpu={:.2}s wall={wall:.2}s",
            voter_cpu + replica_cpu
        ),
        format!(
            "limits: memory={}MB/process cpu_time={} cpu_share={} max_run={} reply_timeout={}ms",
            o.limits.memory_mb,
            if o.limits.cpu_seconds > 0 { format!("{}s/process", o.limits.cpu_seconds) } else { none() },
            if o.cpu_percent > 0.0 { format!("{}%", o.cpu_percent) } else { none() },
            if o.max_seconds > 0.0 { format!("{}s", o.max_seconds) } else { none() },
            o.reply_timeout.as_millis()
        ),
    ];
    if let Some(s) = &sensor {
        summary.push(format!(
            "sensor: size={}MB scans={} flips={}",
            o.sensor_mb, s.scans, s.flips
        ));
    }
    let text = summary.join("\n") + "\n";
    print!("{text}");
    if let Some(path) = &o.summary {
        if let Err(e) = std::fs::write(path, &text) {
            eprintln!("could not write summary to {path}: {e}");
        }
    }
}
