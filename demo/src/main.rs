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
/// working memory, as radiation would cause), and hash the block. A stuck-at
/// bit in the block is forced after the fill and after the upset; one in the
/// accumulator is forced after every multiply.
fn compute(
    block: &mut [u64; BLOCK_WORDS],
    tick: u64,
    upset: Option<(usize, u32)>,
    stuck: Option<StuckAt>,
) -> u64 {
    let mut s = tick.wrapping_mul(0x9E37_79B9_7F4A_7C15).wrapping_add(1);
    for w in block.iter_mut() {
        *w = xorshift(&mut s);
    }
    if let Some((word, bit)) = upset {
        block[word % BLOCK_WORDS] ^= 1 << (bit % 64);
    }
    match stuck {
        Some(StuckAt {
            loc: Loc::Block,
            bit,
            value,
        }) => {
            let (word, b) = (usize::from(bit) / 64, u32::from(bit % 64));
            block[word] = set_bit(block[word], b, value);
        }
        Some(StuckAt {
            loc: Loc::Acc,
            bit,
            value,
        }) => return hash(block, |h| set_bit(h, u32::from(bit), value)),
        _ => {}
    }
    hash(block, |h| h)
}

/// FNV-1a over the block; `after` acts on the accumulator after every
/// multiply (it does nothing on healthy hardware).
#[inline(always)]
fn hash(block: &[u64; BLOCK_WORDS], after: impl Fn(u64) -> u64) -> u64 {
    let mut h: u64 = 0xcbf2_9ce4_8422_2325;
    for w in block.iter() {
        for b in 0..8 {
            h ^= (w >> (8 * b)) & 0xff;
            h = after(h.wrapping_mul(0x0100_0000_01b3));
        }
    }
    h
}

/// The correct result for `tick`, computed by the voter for scoring only.
fn truth(tick: u64) -> u64 {
    compute(&mut [0; BLOCK_WORDS], tick, None, None)
}

/// `x` with bit `b` forced to `v`.
fn set_bit(x: u64, b: u32, v: bool) -> u64 {
    (x & !(1 << b)) | (u64::from(v) << b)
}

// ---- stuck-at bits: a permanent hardware fault that holds one bit at 0 or 1

/// Where a stuck-at bit sits in the replica.
#[derive(Clone, Copy, PartialEq, Eq, Debug)]
enum Loc {
    /// The request's input (the tick), bits 0..63, forced before compute.
    In,
    /// The 4 KiB working block, bits 0..32767 (word = bit / 64).
    Block,
    /// The hash accumulator, bits 0..63, forced after every multiply.
    Acc,
    /// The result register, bits 0..63, forced after compute.
    Out,
}

#[derive(Clone, Copy, PartialEq, Eq, Debug)]
struct StuckAt {
    loc: Loc,
    bit: u16,
    value: bool,
}

impl StuckAt {
    const LOCS: [(Loc, &'static str, u16); 4] = [
        (Loc::In, "in", 64),
        (Loc::Block, "block", (BLOCK_WORDS * 64) as u16),
        (Loc::Acc, "acc", 64),
        (Loc::Out, "out", 64),
    ];

    /// Parse `LOC.BIT.V`, for example `out.17.1`.
    fn parse(s: &str) -> Option<StuckAt> {
        let mut parts = s.split('.');
        let (name, bit, value) = (parts.next()?, parts.next()?, parts.next()?);
        if parts.next().is_some() {
            return None;
        }
        let &(loc, _, bits) = StuckAt::LOCS.iter().find(|l| l.1 == name)?;
        let bit: u16 = bit.parse().ok()?;
        let value = match value {
            "0" => false,
            "1" => true,
            _ => return None,
        };
        (bit < bits).then_some(StuckAt { loc, bit, value })
    }

    fn text(self) -> String {
        let name = StuckAt::LOCS.iter().find(|l| l.0 == self.loc).unwrap().1;
        format!("{name}.{}.{}", self.bit, u8::from(self.value))
    }

    /// Request bytes 12..15: location and value, then the bit (little-endian).
    fn encode(self) -> [u8; 3] {
        let loc = StuckAt::LOCS.iter().position(|l| l.0 == self.loc).unwrap() as u8;
        let bit = self.bit.to_le_bytes();
        [loc | (u8::from(self.value) << 4), bit[0], bit[1]]
    }

    fn decode(q: [u8; 3]) -> StuckAt {
        StuckAt {
            loc: StuckAt::LOCS[usize::from(q[0] & 0x0f) % 4].0,
            bit: u16::from_le_bytes([q[1], q[2]]),
            value: q[0] >> 4 & 1 == 1,
        }
    }

    /// Apply to a 64-bit register at `loc` (the input or the result).
    fn force(this: Option<StuckAt>, loc: Loc, x: u64) -> u64 {
        match this {
            Some(s) if s.loc == loc => set_bit(x, u32::from(s.bit), s.value),
            _ => x,
        }
    }
}

// ---- voter <-> replica protocol: 16-byte requests, 16-byte responses
//
// Request: tick (8 bytes, little-endian), flags, upset bit, upset word (2),
// stuck-at location and value, stuck-at bit (2), one spare byte.

const UPSET_FIRST: u8 = 1; // flip a bit in working memory during the (first) run
const SELF_CHECK: u8 = 2; // compute twice and return both results
const UPSET_SECOND: u8 = 4; // the same flip during the second run too (common mode)
const STUCK: u8 = 8; // persistent fault: return a fixed wrong value
const HANG: u8 = 16; // persistent fault: stop answering
const STUCK_BIT: u8 = 32; // persistent fault: one bit held at 0 or 1 (bytes 12..15)

/// Known-answer test input; its result is computed once at start-up.
const KAT_INPUT: u64 = 0;

fn replica_main(limits: Limits) -> ! {
    limits.apply();
    let mut input = std::io::stdin().lock();
    let mut output = std::io::stdout().lock();
    let mut block = Box::new([0u64; BLOCK_WORDS]); // working memory, allocated once
    let mut q = [0u8; 16];
    while input.read_exact(&mut q).is_ok() {
        let (flags, bit) = (q[8], u32::from(q[9]));
        let word = usize::from(u16::from_le_bytes([q[10], q[11]]));
        let stuck = (flags & STUCK_BIT != 0).then(|| StuckAt::decode([q[12], q[13], q[14]]));
        if flags & HANG != 0 {
            loop {
                std::thread::sleep(Duration::from_secs(3600));
            }
        }
        // a stuck input bit changes the input both runs see
        let tick = StuckAt::force(
            stuck,
            Loc::In,
            u64::from_le_bytes(q[..8].try_into().unwrap()),
        );
        let at = (word, bit);
        let upset = |f| (flags & f != 0).then_some(at);
        let mut first = compute(&mut block, tick, upset(UPSET_FIRST), stuck);
        let mut second = if flags & SELF_CHECK != 0 {
            compute(&mut block, tick, upset(UPSET_SECOND), stuck)
        } else {
            0
        };
        if flags & STUCK != 0 {
            first = 0xdead_beef;
            second = 0xdead_beef;
        }
        // a stuck result bit is forced last, after the STUCK value: it is in
        // the register the replica answers from
        first = StuckAt::force(stuck, Loc::Out, first);
        second = StuckAt::force(stuck, Loc::Out, second);
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
    stuck_bit: Option<StuckAt>,
    /// A scheduled corrupt fault waiting for the replica's next computation
    /// (in self-check mode the fault tick can be one without a computation).
    corrupt_pending: bool,
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
            stuck_bit: None,
            corrupt_pending: false,
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
        let mut q = [0u8; 16];
        if let Some(s) = self.stuck_bit {
            flags |= STUCK_BIT;
            q[12..15].copy_from_slice(&s.encode());
        }
        q[..8].copy_from_slice(&tick.to_le_bytes());
        q[8] = flags;
        q[9] = bit;
        q[10..12].copy_from_slice(&word.to_le_bytes());
        self.to.write_all(&q).and_then(|_| self.to.flush()).ok()?;
        let mut r = [0u8; 16];
        read_exact_timeout(&mut self.from, &mut r, timeout)?;
        Some((
            u64::from_le_bytes(r[..8].try_into().unwrap()),
            u64::from_le_bytes(r[8..].try_into().unwrap()),
        ))
    }

    /// Run the known-answer test and add one to `tests`. The request carries
    /// the replica's persistent faults, so the test sees the same hardware.
    fn known_answer_ok(&mut self, expected: u64, timeout: Duration, tests: &mut u64) -> bool {
        *tests += 1;
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
    /// Hold one bit at 0 or 1 from the fault tick on.
    StuckBit(StuckAt),
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
    checkpoint: Option<String>,
    checkpoint_every: u64,
}

fn usage() -> ! {
    eprintln!(
        "usage: quorum [--policy shrink|tmr] [--ticks N] [--seed S]\n\
         \x20             [--upset-rate R] [--common-mode C] [--kat-period P]\n\
         \x20             [--fault kill|corrupt|stuck|hang|sensor:I@T] ... [--faults-file F]\n\
         \x20             [--fault stuckbit:I@T/LOC.BIT.V] (LOC in.0-63, block.0-32767,\n\
         \x20              acc.0-63 or out.0-63; V 0 or 1)\n\
         \x20             [--max-memory-mb M] [--max-cpu-seconds S] [--cpu-percent P]\n\
         \x20             [--max-seconds S] [--reply-timeout-ms T]\n\
         \x20             [--sensor-mb M] [--sensor-every N]\n\
         \x20             [--log file.csv] [--progress-every N] [--summary file.txt]\n\
         \x20             [--checkpoint file] [--checkpoint-every N]"
    );
    std::process::exit(2)
}

fn parse_fault(s: &str) -> Option<Fault> {
    let (kind, rest) = s.split_once(':')?;
    let (replica, tick) = rest.split_once('@')?;
    // only a stuck-at bit takes parameters, after a '/'
    let (tick, params) = match tick.split_once('/') {
        Some((t, p)) => (t, Some(p)),
        None => (tick, None),
    };
    let kind = match (kind, params) {
        ("kill", None) => FaultKind::Kill,
        ("corrupt", None) => FaultKind::Corrupt,
        ("stuck", None) => FaultKind::Stuck,
        ("hang", None) => FaultKind::Hang,
        ("sensor", None) => FaultKind::Sensor,
        ("stuckbit", Some(p)) => FaultKind::StuckBit(StuckAt::parse(p)?),
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
        checkpoint: None,
        checkpoint_every: 100,
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
            "--checkpoint" => o.checkpoint = Some(next()),
            "--checkpoint-every" => o.checkpoint_every = next().parse().unwrap_or_else(|_| usage()),
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

// ---- checkpoint: the run's state on disk, to resume after a reset or power cut

/// Everything a run needs to carry on where it stopped. The replicas are new
/// processes after a reset; which slots are dead, stuck or hung is hardware
/// state, so it is saved and applied to them.
#[derive(Default)]
struct Checkpoint {
    /// The options that decide the results; a checkpoint resumes only the same run.
    run: String,
    /// Last tick completed.
    tick: u64,
    useful: u64,
    detected: u64,
    wrong: u64,
    last_useful: u64,
    rng: u64,
    alive: [bool; REPLICAS],
    stuck: [bool; REPLICAS],
    hung: [bool; REPLICAS],
    stuck_bit: [Option<StuckAt>; REPLICAS],
    corrupt_pending: [bool; REPLICAS],
    strikes: [[u64; quorum::STRIKES]; REPLICAS],
    strike_count: [usize; REPLICAS],
    fault_at: [Option<u64>; REPLICAS],
    faults_injected: u64,
    caught: u64,
    latency_sum: u64,
    latency_max: u64,
    kat_tests: u64,
    sensor_scans: u64,
    sensor_flips: u64,
    resumes: u64,
}

fn run_identity(o: &Options) -> String {
    let faults: Vec<String> = o
        .faults
        .iter()
        .map(|f| {
            let kind = match f.kind {
                FaultKind::Kill => "kill",
                FaultKind::Corrupt => "corrupt",
                FaultKind::Stuck => "stuck",
                FaultKind::Hang => "hang",
                FaultKind::Sensor => "sensor",
                FaultKind::StuckBit(s) => {
                    return format!("stuckbit:{}@{}/{}", f.replica, f.tick, s.text())
                }
            };
            format!("{kind}:{}@{}", f.replica, f.tick)
        })
        .collect();
    format!(
        "{:?}/{}/{}/{}/{}/{}/{}/{}",
        o.policy,
        o.ticks,
        o.seed,
        o.upset_rate,
        o.common_mode,
        o.kat_period,
        o.sensor_mb,
        faults.join(",")
    )
}

impl Checkpoint {
    fn to_text(&self) -> String {
        let list = |v: &[bool]| {
            v.iter()
                .map(|&b| u8::from(b).to_string())
                .collect::<Vec<_>>()
                .join(",")
        };
        let strikes: Vec<String> = self.strikes.iter().flatten().map(u64::to_string).collect();
        let counts: Vec<String> = self.strike_count.iter().map(usize::to_string).collect();
        let fault_at: Vec<String> = self
            .fault_at
            .iter()
            .map(|t| t.map_or("-".into(), |t| t.to_string()))
            .collect();
        let stuck_bit: Vec<String> = self
            .stuck_bit
            .iter()
            .map(|s| s.map_or("-".into(), StuckAt::text))
            .collect();
        format!(
            "run={}\ntick={}\nuseful={}\ndetected={}\nwrong={}\nlast_useful={}\nrng={}\n\
             alive={}\nstuck={}\nhung={}\nstuckbit={}\ncorrupt_pending={}\n\
             strikes={}\nstrike_count={}\nfault_at={}\n\
             faults_injected={}\ncaught={}\nlatency_sum={}\nlatency_max={}\nkat_tests={}\n\
             sensor_scans={}\nsensor_flips={}\nresumes={}\n",
            self.run,
            self.tick,
            self.useful,
            self.detected,
            self.wrong,
            self.last_useful,
            self.rng,
            list(&self.alive),
            list(&self.stuck),
            list(&self.hung),
            stuck_bit.join(","),
            list(&self.corrupt_pending),
            strikes.join(","),
            counts.join(","),
            fault_at.join(","),
            self.faults_injected,
            self.caught,
            self.latency_sum,
            self.latency_max,
            self.kat_tests,
            self.sensor_scans,
            self.sensor_flips,
            self.resumes
        )
    }

    fn from_text(text: &str) -> Option<Checkpoint> {
        let get = |key: &str| {
            text.lines()
                .find_map(|l| l.strip_prefix(key)?.strip_prefix('='))
                .map(str::to_string)
        };
        let num = |key: &str| get(key)?.parse::<u64>().ok();
        let items = |key: &str| -> Option<Vec<String>> {
            Some(get(key)?.split(',').map(str::to_string).collect())
        };
        let flags = |key: &str| -> Option<[bool; REPLICAS]> {
            let v = items(key)?;
            let mut out = [false; REPLICAS];
            for i in 0..REPLICAS {
                out[i] = v.get(i)? == "1";
            }
            Some(out)
        };
        let strikes = items("strikes")?;
        let counts = items("strike_count")?;
        let fault_at = items("fault_at")?;
        let stuck_bit = items("stuckbit")?;
        let mut c = Checkpoint {
            run: get("run")?,
            tick: num("tick")?,
            useful: num("useful")?,
            detected: num("detected")?,
            wrong: num("wrong")?,
            last_useful: num("last_useful")?,
            rng: num("rng")?,
            alive: flags("alive")?,
            stuck: flags("stuck")?,
            hung: flags("hung")?,
            corrupt_pending: flags("corrupt_pending")?,
            faults_injected: num("faults_injected")?,
            caught: num("caught")?,
            latency_sum: num("latency_sum")?,
            latency_max: num("latency_max")?,
            kat_tests: num("kat_tests")?,
            sensor_scans: num("sensor_scans")?,
            sensor_flips: num("sensor_flips")?,
            resumes: num("resumes")?,
            ..Checkpoint::default()
        };
        for i in 0..REPLICAS {
            for k in 0..quorum::STRIKES {
                c.strikes[i][k] = strikes.get(i * quorum::STRIKES + k)?.parse().ok()?;
            }
            c.strike_count[i] = counts.get(i)?.parse().ok()?;
            let t = fault_at.get(i)?;
            c.fault_at[i] = if t == "-" {
                None
            } else {
                Some(t.parse().ok()?)
            };
            let s = stuck_bit.get(i)?;
            c.stuck_bit[i] = if s == "-" {
                None
            } else {
                Some(StuckAt::parse(s)?)
            };
        }
        Some(c)
    }

    /// Write atomically: a power cut leaves either the old file or the new one.
    fn save(&self, path: &str) {
        let tmp = format!("{path}.tmp");
        let written = File::create(&tmp).and_then(|mut f| {
            f.write_all(self.to_text().as_bytes())?;
            f.sync_all()
        });
        if let Err(e) = written.and_then(|_| std::fs::rename(&tmp, path)) {
            eprintln!("could not write checkpoint {path}: {e}");
            return;
        }
        // make the rename itself durable
        let dir = std::path::Path::new(path)
            .parent()
            .filter(|d| !d.as_os_str().is_empty())
            .unwrap_or(std::path::Path::new("."));
        if let Ok(d) = File::open(dir) {
            let _ = d.sync_all();
        }
    }
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

    // resume from a checkpoint of the same run, if there is one
    let identity = run_identity(&o);
    let resumed = o.checkpoint.as_ref().and_then(|p| {
        let text = std::fs::read_to_string(p).ok()?;
        match Checkpoint::from_text(&text) {
            Some(c) if c.run == identity => Some(c),
            _ => {
                eprintln!("checkpoint {p} is not from this run; refusing to overwrite it");
                std::process::exit(2);
            }
        }
    });
    let mut cp = resumed.unwrap_or_default();
    let resuming = cp.tick > 0 || cp.resumes > 0 || !cp.run.is_empty();
    if resuming {
        cp.resumes += 1;
    } else {
        cp.alive = [true; REPLICAS];
        cp.rng = o.seed.wrapping_mul(0x9E37_79B9_7F4A_7C15).wrapping_add(7);
    }
    cp.run = identity;

    // shared by the event closure and the checkpoint, which flushes it
    let log = std::cell::RefCell::new(o.log.as_ref().map(|p| {
        let f = std::fs::OpenOptions::new()
            .create(true)
            .append(resuming)
            .write(true)
            .truncate(!resuming)
            .open(p)
            .expect("log file");
        BufWriter::new(f)
    }));
    if let Some(l) = log.borrow_mut().as_mut() {
        if resuming {
            let _ = writeln!(l, "{},resumed,{}", cp.tick, cp.resumes);
        } else {
            let _ = writeln!(l, "tick,event,detail");
        }
    }
    let event = |tick: u64, what: &str, detail: String| {
        if let Some(l) = log.borrow_mut().as_mut() {
            let _ = writeln!(l, "{tick},{what},{detail}");
        }
    };

    let mut replicas: Vec<Option<Replica>> = (0..REPLICAS)
        .map(|i| {
            cp.alive[i].then(|| {
                let mut r = Replica::spawn(o.limits);
                r.stuck = cp.stuck[i];
                r.hung = cp.hung[i];
                r.stuck_bit = cp.stuck_bit[i];
                r.corrupt_pending = cp.corrupt_pending[i];
                r
            })
        })
        .collect();
    let mut health = Health::from_raw(cp.strikes, cp.strike_count);
    let mut sensor = (o.sensor_mb > 0).then(|| {
        let mut s = Sensor::new(o.sensor_mb);
        s.scans = cp.sensor_scans;
        s.flips = cp.sensor_flips;
        s
    });
    let kat = truth(KAT_INPUT);
    let timeout = o.reply_timeout;
    let mut rng = cp.rng;

    let (mut useful, mut detected, mut wrong, mut halted_at) =
        (cp.useful, cp.detected, cp.wrong, None);
    let mut last_useful = cp.last_useful; // tick of the last correct result

    // detection latency: ticks from a scheduled corrupt, stuck or hang fault to
    // the first tick the voter reacts to that replica (outvote, caught
    // disagreement, loss or retirement). Kills are excluded: the voter does them.
    let mut fault_at = cp.fault_at;
    let mut faults_injected = cp.faults_injected;
    let (mut caught, mut latency_sum, mut latency_max) =
        (cp.caught, cp.latency_sum, cp.latency_max);
    let mut kat_tests = cp.kat_tests; // known-answer tests run, of every kind
    let mut stopped = "completed";
    let start = Instant::now();
    let mut last_tick = cp.tick;
    let save = |cp: &mut Checkpoint,
                tick: u64,
                counts: (u64, u64, u64, u64),
                rng: u64,
                replicas: &[Option<Replica>],
                health: &Health,
                detection: ([Option<u64>; REPLICAS], u64, u64, u64, u64, u64),
                sensor: &Option<Sensor>| {
        let Some(path) = &o.checkpoint else { return };
        cp.tick = tick;
        (cp.useful, cp.detected, cp.wrong, cp.last_useful) = counts;
        cp.rng = rng;
        for i in 0..REPLICAS {
            cp.alive[i] = replicas[i].is_some();
            cp.stuck[i] = replicas[i].as_ref().is_some_and(|r| r.stuck);
            cp.hung[i] = replicas[i].as_ref().is_some_and(|r| r.hung);
            cp.stuck_bit[i] = replicas[i].as_ref().and_then(|r| r.stuck_bit);
            cp.corrupt_pending[i] = replicas[i].as_ref().is_some_and(|r| r.corrupt_pending);
        }
        (cp.strikes, cp.strike_count) = health.to_raw();
        (
            cp.fault_at,
            cp.faults_injected,
            cp.caught,
            cp.latency_sum,
            cp.latency_max,
            cp.kat_tests,
        ) = detection;
        if let Some(s) = sensor {
            (cp.sensor_scans, cp.sensor_flips) = (s.scans, s.flips);
        }
        // the log first, so after a reset it holds every event up to the checkpoint
        if let Some(l) = log.borrow_mut().as_mut() {
            let _ = l.flush();
            let _ = l.get_ref().sync_data();
        }
        cp.save(path);
    };
    let progress = |useful: u64, detected: u64, wrong: u64, alive: usize| {
        format!("useful={useful} detected={detected} wrong={wrong} alive={alive}")
    };

    for tick in cp.tick + 1..=o.ticks {
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
        let mut flagged = [false; REPLICAS];
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
            if f.kind != FaultKind::Kill && fault_at[f.replica].is_none() {
                faults_injected += 1;
                fault_at[f.replica] = Some(tick);
            }
            match f.kind {
                FaultKind::Kill => {
                    replicas[f.replica].take().unwrap().retire();
                    event(tick, "killed", f.replica.to_string());
                }
                // taken at the replica's next computation, which in
                // self-check mode is the next even tick
                FaultKind::Corrupt => r.corrupt_pending = true,
                FaultKind::Stuck => {
                    r.stuck = true;
                    event(tick, "stuck", f.replica.to_string());
                }
                FaultKind::Hang => {
                    r.hung = true;
                    event(tick, "hang", f.replica.to_string());
                }
                FaultKind::StuckBit(s) => {
                    r.stuck_bit = Some(s);
                    event(tick, "stuckbit", format!("{}/{}", f.replica, s.text()));
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
            let upset = std::mem::take(&mut r.corrupt_pending)
                || (o.upset_rate > 0.0 && uniform(&mut rng) < o.upset_rate);
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
                    flagged[i] = true;
                    event(tick, "lost", i.to_string());
                }
            }
        }

        let verdict = decide(o.policy, &replies);
        let mut outcome = verdict.outcome;
        for i in 0..REPLICAS {
            flagged[i] |= verdict.dissent[i]
                || (replies[i] != Reply::Absent && verdict.outcome == Outcome::Detected);
        }

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
                    .is_some_and(|r| r.known_answer_ok(kat, timeout, &mut kat_tests));
                if ok {
                    health.clear(i);
                    event(tick, "cleared", i.to_string());
                } else if let Some(r) = replicas[i].take() {
                    r.retire();
                    flagged[i] = true;
                    event(tick, "retired", i.to_string());
                }
            }
            // two replicas disagree: the known-answer test tells which failed
            if verdict.diagnose {
                for i in 0..REPLICAS {
                    let failed = replicas[i]
                        .as_mut()
                        .is_some_and(|r| !r.known_answer_ok(kat, timeout, &mut kat_tests));
                    if failed {
                        replicas[i].take().unwrap().retire();
                        flagged[i] = true;
                        event(tick, "retired", i.to_string());
                    }
                }
            }
            // on one replica, a periodic known-answer test catches a stuck fault
            if verdict.mode == Mode::SelfCheck && tick % o.kat_period.max(1) == 0 {
                for i in 0..REPLICAS {
                    let failed = replicas[i]
                        .as_mut()
                        .is_some_and(|r| !r.known_answer_ok(kat, timeout, &mut kat_tests));
                    if failed {
                        replicas[i].take().unwrap().retire();
                        flagged[i] = true;
                        event(tick, "retired", i.to_string());
                        outcome = Outcome::Detected;
                    }
                }
            }
        }

        for i in 0..REPLICAS {
            if let (Some(at), true) = (fault_at[i], flagged[i]) {
                caught += 1;
                latency_sum += tick - at;
                latency_max = latency_max.max(tick - at);
                fault_at[i] = None;
            }
        }

        match outcome {
            Outcome::Deliver(v) if v == truth(tick) => {
                useful += 1;
                last_useful = tick;
            }
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

        if o.checkpoint_every > 0 && tick % o.checkpoint_every == 0 {
            save(
                &mut cp,
                tick,
                (useful, detected, wrong, last_useful),
                rng,
                &replicas,
                &health,
                (
                    fault_at,
                    faults_injected,
                    caught,
                    latency_sum,
                    latency_max,
                    kat_tests,
                ),
                &sensor,
            );
        }

        // CPU share: sleep so that work takes at most cpu_percent of wall time
        if o.cpu_percent > 0.0 && o.cpu_percent < 100.0 {
            let work = tick_start.elapsed();
            std::thread::sleep(work.mul_f64((100.0 - o.cpu_percent) / o.cpu_percent));
        }
    }

    // a stopped run keeps its checkpoint so it can resume; a finished one removes it
    if let Some(path) = &o.checkpoint {
        if stopped == "signal" || stopped == "time-limit" {
            save(
                &mut cp,
                last_tick,
                (useful, detected, wrong, last_useful),
                rng,
                &replicas,
                &health,
                (
                    fault_at,
                    faults_injected,
                    caught,
                    latency_sum,
                    latency_max,
                    kat_tests,
                ),
                &sensor,
            );
        } else {
            let _ = std::fs::remove_file(path);
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
            "useful={useful} detected={detected} wrong={wrong} halted_at={} alive_at_end={alive_at_end} stopped={stopped} last_useful={last_useful} kat_tests={kat_tests}",
            halted_at.map_or("never".to_string(), |t| t.to_string())
        ),
        format!(
            "footprint: voter_max_rss={voter_kib}KiB replica_max_rss={replica_kib}KiB cpu={:.3}s wall={wall:.3}s",
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
    if faults_injected > 0 {
        let (mean, max) = if caught > 0 {
            (
                format!("{:.1}", latency_sum as f64 / caught as f64),
                latency_max.to_string(),
            )
        } else {
            (none(), none())
        };
        summary.push(format!(
            "detection: faults={faults_injected} caught={caught} latency_mean={mean} latency_max={max} (ticks)"
        ));
    }
    if o.checkpoint.is_some() {
        summary.push(format!(
            "checkpoint: every={} resumes={}",
            o.checkpoint_every, cp.resumes
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

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn stuck_at_text_and_bytes_round_trip() {
        for s in [
            "in.0.0",
            "in.63.1",
            "block.0.1",
            "block.32767.0",
            "acc.5.1",
            "out.17.0",
        ] {
            let a = StuckAt::parse(s).unwrap();
            assert_eq!(a.text(), s);
            assert_eq!(StuckAt::decode(a.encode()), a);
        }
    }

    #[test]
    fn stuck_at_out_of_range_or_malformed_is_refused() {
        for s in [
            "",
            "out",
            "out.1",
            "out.64.1",
            "in.64.0",
            "acc.64.0",
            "block.32768.0",
            "out.1.2",
            "out.1.1.1",
            "pc.1.1",
        ] {
            assert_eq!(StuckAt::parse(s), None, "{s}");
        }
        assert!(parse_fault("stuckbit:2@500").is_none());
        assert!(parse_fault("stuck:2@500/x").is_none());
        assert!(parse_fault("stuckbit:2@500/out.3.1").is_some());
    }

    #[test]
    fn set_bit_forces_one_bit() {
        assert_eq!(set_bit(0, 3, true), 8);
        assert_eq!(set_bit(u64::MAX, 63, false), u64::MAX >> 1);
        assert_eq!(set_bit(5, 0, true), 5);
    }

    #[test]
    fn healthy_compute_is_unchanged_by_a_stuck_bit_elsewhere() {
        // a stuck input or result bit is applied by the replica, not by compute
        let mut b = [0; BLOCK_WORDS];
        for s in ["in.3.1", "out.3.1"] {
            assert_eq!(compute(&mut b, 7, None, StuckAt::parse(s)), truth(7));
        }
        assert_ne!(
            compute(&mut b, 7, None, StuckAt::parse("acc.5.1")),
            truth(7)
        );
    }
}
