#!/usr/bin/env python3
"""Draw the README chart from two real runs of the quorum program.

Both policies run with the same seed, upsets and faults (the scenario in the
README's Results section). The program's own event log, with a progress line
every 100 ticks, gives the running count of correct results. Writes
docs/chart-light.svg and docs/chart-dark.svg; with --png, also renders each to
a PNG at twice the size with Chrome (set CHROME to its path if needed).

Run from the repository root: python3 scripts/chart.py --png
"""
import csv, os, subprocess, sys, tempfile

QUORUM = os.environ.get('QUORUM_BIN', 'target/release/quorum')
TICKS, SEED, RATE, EVERY = 20000, 1, 0.001, 100
FAULTS = [('stuck', 1, 4000), ('kill', 0, 9000)]


def run(policy):
    """Return ([(tick, useful)], {event: [ticks]}) for one run."""
    with tempfile.TemporaryDirectory() as d:
        log = os.path.join(d, 'log.csv')
        args = [QUORUM, '--policy', policy, '--ticks', str(TICKS), '--seed', str(SEED),
                '--upset-rate', str(RATE), '--progress-every', str(EVERY), '--log', log]
        for kind, replica, tick in FAULTS:
            args += ['--fault', f'{kind}:{replica}@{tick}']
        subprocess.run(args, check=True, stdout=subprocess.DEVNULL)
        points, events = [(0, 0)], {}
        for row in csv.DictReader(open(log)):
            tick = int(row['tick'])
            if row['event'] == 'progress':
                fields = dict(f.split('=') for f in row['detail'].split())
                points.append((tick, int(fields['useful'])))
            else:
                events.setdefault(row['event'], []).append(tick)
        return points, events


THEMES = {
    # mru.space tokens; series colours from a palette validated for each surface
    'light': dict(bg='#fafaf8', ink='#16161a', soft='#44444c', faint='#6f6f78', rule='#e3e3dd',
                  mru='#2a78d6', tmr='#eb6834'),
    'dark': dict(bg='#16161a', ink='#ecece8', soft='#c0c0bc', faint='#8f8f8a', rule='#2e2e33',
                 mru='#3987e5', tmr='#d95926'),
}
SERIF = "Charter,'Source Serif 4',Georgia,serif"
DISPLAY = "Futura,Jost,'Century Gothic',sans-serif"
W, H = 1200, 660
M = 56
PX0, PX1, PY0, PY1 = M + 64, W - M - 150, 232, 566   # plot box; room right for end labels


def svg(theme, shrink, tmr, retired_at):
    t = THEMES[theme]
    sx = lambda tick: PX0 + (PX1 - PX0) * tick / TICKS
    sy = lambda n: PY1 - (PY1 - PY0) * n / TICKS
    s = [f'<svg xmlns="http://www.w3.org/2000/svg" width="{W}" height="{H}" viewBox="0 0 {W} {H}">',
         f'<style>text{{font-family:{SERIF};fill:{t["ink"]}}} .soft{{fill:{t["soft"]}}} '
         f'.faint{{fill:{t["faint"]}}} .display{{font-family:{DISPLAY};font-weight:500}}</style>',
         f'<rect width="{W}" height="{H}" fill="{t["bg"]}"/>']
    add = s.append

    # header
    add(f'<text class="display" x="{M}" y="{M + 20}" font-size="34">Same hardware, same faults. Mru keeps delivering.</text>')
    add(f'<text x="{M}" y="{M + 58}" font-size="20" class="soft">Correct results from two runs of the quorum program, '
        f'with the same random upsets and the same faults.</text>')

    # legend
    def key(x, colour, text, weight=400):
        add(f'<line x1="{x}" x2="{x + 30}" y1="{M + 99}" y2="{M + 99}" stroke="{colour}" stroke-width="5" stroke-linecap="round"/>')
        add(f'<text x="{x + 42}" y="{M + 106}" font-size="20" font-weight="{weight}">{text}</text>')
    key(M, t['mru'], 'Mru shrinking quorum', 700)
    key(M + 290, t['tmr'], 'Fixed triple redundancy (TMR)')

    # axes: recessive grid, labels in the faint ink
    for n in (0, 5000, 10000, 15000, 20000):
        add(f'<line x1="{PX0}" x2="{PX1}" y1="{sy(n):.1f}" y2="{sy(n):.1f}" stroke="{t["rule"]}" stroke-width="1.5"/>')
        add(f'<text x="{PX0 - 12}" y="{sy(n) + 6:.1f}" font-size="17" text-anchor="end" class="faint">{f"{n // 1000}k" if n else 0}</text>')
    for tick in (0, 5000, 10000, 15000):
        add(f'<text x="{sx(tick):.1f}" y="{PY1 + 28}" font-size="17" text-anchor="middle" class="faint">{f"{tick // 1000}k" if tick else 0}</text>')
    add(f'<text x="{sx(TICKS) - 14:.1f}" y="{PY1 + 28}" font-size="17" class="faint">20k ticks</text>')
    add(f'<text x="{M}" y="{PY0 - 16}" font-size="17" class="faint">correct results</text>')

    # fault markers
    for tick, label in ((4000, 'replica 1 stuck'), (9000, 'replica 0 lost')):
        add(f'<line x1="{sx(tick):.1f}" x2="{sx(tick):.1f}" y1="{PY0 - 8}" y2="{PY1}" stroke="{t["faint"]}" stroke-width="1.5" stroke-dasharray="2 5"/>')
        add(f'<text x="{sx(tick):.1f}" y="{PY0 - 16}" font-size="17" text-anchor="middle" class="faint">{label}</text>')

    # series: TMR first, Mru on top with a surface ring
    def line(points, colour, ring):
        d = ' '.join(f'{"M" if i == 0 else "L"}{sx(x):.1f},{sy(y):.1f}' for i, (x, y) in enumerate(points))
        if ring:
            add(f'<path d="{d}" fill="none" stroke="{t["bg"]}" stroke-width="8" stroke-linejoin="round" stroke-linecap="round"/>')
        add(f'<path d="{d}" fill="none" stroke="{colour}" stroke-width="3" stroke-linejoin="round" stroke-linecap="round"/>')
    line(tmr, t['tmr'], False)
    line(shrink, t['mru'], True)

    # end labels (direct), in text ink beside a coloured end marker
    for points, colour, name, weight, dy in ((shrink, t['mru'], 'Mru', 700, 0), (tmr, t['tmr'], 'TMR', 400, 0)):
        x, y = points[-1]
        add(f'<circle cx="{sx(x):.1f}" cy="{sy(y):.1f}" r="5" fill="{colour}" stroke="{t["bg"]}" stroke-width="2"/>')
        add(f'<text x="{sx(x) + 14:.1f}" y="{sy(y) + 7 + dy:.1f}" font-size="20" font-weight="{weight}">{name} {y:,}</text>')

    # what happened, beside each line
    add(f'<text x="{sx(10300):.1f}" y="{sy(tmr[-1][1]) + 30:.1f}" font-size="17" class="soft">TMR compares a good replica with the stuck one</text>')
    add(f'<text x="{sx(10300):.1f}" y="{sy(tmr[-1][1]) + 52:.1f}" font-size="17" class="soft">and rejects every result from here on</text>')
    add(f'<text x="{sx(10300):.1f}" y="{sy(17600):.1f}" font-size="17" class="soft">Mru retired the stuck replica at tick {retired_at:,}</text>')
    add(f'<text x="{sx(10300):.1f}" y="{sy(17600) + 22:.1f}" font-size="17" class="soft">and self-checks on the last one, at half rate</text>')

    add(f'<text x="{sx(4300):.1f}" y="{sy(13200):.1f}" font-size="17" class="soft">Both lines are the same</text>')
    add(f'<text x="{sx(4300):.1f}" y="{sy(13200) + 22:.1f}" font-size="17" class="soft">while TMR can still vote</text>')

    # footer
    add(f'<text x="{M}" y="{H - 26}" font-size="16" class="faint">One run, seed {SEED}, upset rate {RATE}: illustrative, not a reliability estimate. '
        "Made by scripts/chart.py from the program's own log.</text>")
    add('</svg>')
    return '\n'.join(s)


def main():
    shrink, ev = run('shrink')
    tmr, _ = run('tmr')
    retired_at = min(ev.get('retired', [0]))
    paths = []
    for theme in THEMES:
        path = f'docs/chart-{theme}.svg'
        with open(path, 'w') as f:
            f.write(svg(theme, shrink, tmr, retired_at))
        paths.append(path)
    print(f'shrink={shrink[-1][1]} tmr={tmr[-1][1]} retired_at={retired_at}')
    if '--png' in sys.argv:
        chrome = os.environ.get('CHROME', '/Applications/Google Chrome.app/Contents/MacOS/Google Chrome')
        for path in paths:
            subprocess.run([chrome, '--headless=new', '--hide-scrollbars', '--force-device-scale-factor=2',
                            f'--window-size={W},{H}', f'--screenshot={os.path.abspath(path[:-4])}.png',
                            'file://' + os.path.abspath(path)], check=True,
                           stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            print(path[:-4] + '.png')


if __name__ == '__main__':
    main()
