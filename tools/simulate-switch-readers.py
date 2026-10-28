#!/usr/bin/env python3
"""Runs the dedicated-switch PIO readers, as assembled, against modelled pins.

    pio run -e IO_16_8_1          # generates src/IODevices/SwitchesPIO/*.pio.h
    python3 tools/simulate-switch-readers.py

Nothing else tests the PIO side, and its failures are quiet ones. Until 0.3.4
the CPU handed the readers a baseline with bits set above their pins, no read
ever matched it, and every reader reported on every pass: an endless stream of
interrupts that looked like working switches. On IO_16_8_1 that stream was even
what kept inputs 13-16 alive, because the GPIO 15-18 reset only ran when the
reader reported.

So this checks, on the instruction words pioasm produced:

- every reader reports each change of its inputs exactly once, and nothing
  while they stay still;
- IO_16_8_1's reader resets GPIO 15-18 before every read, with the pulse that
  works on the machines (low 2 cycles, high 6, low 2), reads 3 cycles after
  releasing them, and resets at least every few microseconds while idle;
- Opto_16's reader never drives a pin - there, GPIO 15-18 are comparator
  outputs.

The CPU is modelled as Switches::handleSwitchChanges() behaves: it takes the
pushed word and puts it back masked to the reader's width. What it cannot
model is the hardware - in particular how inputs 13-16 on IO_16_8_1 latch. That
still needs a board.

Only the instructions the readers use are decoded. Anything else raises, so a
program change that outgrows this model fails here rather than passing
unchecked.
"""

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
PIO_DIR = ROOT / 'src' / 'IODevices' / 'SwitchesPIO'

CLOCK_HZ = 200_000_000  # SYS_CLK_MHZ in main.cpp
CPU_LATENCY = 400  # cycles from a push to the CPU's reply, about 2 us
STATEFUL_BASE = 15  # GPIO 15-18, the set pins of the 16-input reader
SWITCHES_BASE = 3  # SWITCHES_BASE_PIN

# The GPIO 15-18 pulse IO_16_8_1 works with, one level per cycle from the
# instruction that turns the pins to outputs up to the one that releases them.
RESET_PULSE = [0, 0, 0, 1, 1, 1, 1, 1, 1, 0, 0]
READ_AFTER_RELEASE = 3
MAX_IDLE_RESET_GAP_US = 5.0


class Unsupported(Exception):
    pass


class Program:
    def __init__(self, header: str, name: str):
        path = PIO_DIR / header
        if not path.exists():
            sys.exit(f'{path.relative_to(ROOT)} not found - run "pio run -e IO_16_8_1" first, '
                     'it generates the .pio.h headers')
        src = path.read_text()
        body = re.search(name + r'_program_instructions\[\] = \{(.*?)\};', src, re.S)
        self.words = [int(w, 16) for w in re.findall(r'0x([0-9a-f]{4})', body.group(1))]
        self.wrap_target = int(re.search(rf'#define {name}_wrap_target (\d+)', src).group(1))
        self.wrap = int(re.search(rf'#define {name}_wrap (\d+)', src).group(1))
        sideset = re.search(r'sm_config_set_sideset\(&c, (\d+), (true|false)', src)
        self.sideset_bits = int(sideset.group(1)) if sideset else 0
        self.sideset_opt = bool(sideset) and sideset.group(2) == 'true'
        self.name = name


class Run:
    """One state machine plus the CPU's side of the FIFOs."""

    def __init__(self, program: Program, inputs: int, levels):
        self.p = program
        self.inputs = inputs
        self.levels = levels  # cycle -> pin levels from SWITCHES_BASE up
        self.mask = (1 << inputs) - 1
        self.reports = []  # (cycle, raw)
        self.driven = []  # (cycle, level) for every cycle GPIO 15-18 are outputs
        self.releases = []  # cycle GPIO 15-18 went back to input
        self.reads = []  # cycle of every `in pins`
        self.drove_pins = False

    def run(self, cycles: int):
        p = self.p
        delay_bits = 5 - p.sideset_bits
        pc = x = y = isr = osr = 0
        rx, tx = [], []
        pindirs = outs = 0
        reply_at = None
        t = 0
        while t < cycles:
            if rx and reply_at is None:
                reply_at = t + CPU_LATENCY
            if reply_at is not None and t >= reply_at:
                raw = rx.pop(0)
                self.reports.append((t, raw))
                tx.append(raw & self.mask)
                reply_at = None

            w = p.words[pc]
            op = w >> 13
            delay_side = (w >> 8) & 0x1F
            delay = delay_side & ((1 << delay_bits) - 1)
            if p.sideset_bits:
                side = delay_side >> delay_bits
                if not p.sideset_opt:
                    outs = side
                elif side >> (p.sideset_bits - 1):
                    outs = side & ((1 << (p.sideset_bits - 1)) - 1)
            nxt = p.wrap_target if pc == p.wrap else pc + 1
            stall = False

            if op == 0:  # JMP
                cond, addr = (w >> 5) & 7, w & 0x1F
                if cond == 0:
                    take = True
                elif cond == 1:
                    take = x == 0
                elif cond == 2:
                    take = x != 0
                    x = (x - 1) & 0xFFFFFFFF
                elif cond == 3:
                    take = y == 0
                elif cond == 4:
                    take = y != 0
                    y = (y - 1) & 0xFFFFFFFF
                elif cond == 5:
                    take = x != y
                else:
                    raise Unsupported(f'{p.name}: jmp condition {cond}')
                if take:
                    nxt = addr
            elif op == 2:  # IN
                source, count = (w >> 5) & 7, (w & 0x1F) or 32
                if source != 0:
                    raise Unsupported(f'{p.name}: in from source {source}')
                isr = ((isr << count) | (self.levels(t) & ((1 << count) - 1))) & 0xFFFFFFFF
                self.reads.append(t)
            elif op == 4:  # PUSH / PULL
                if not (w >> 5) & 1:
                    raise Unsupported(f'{p.name}: non-blocking push/pull')
                if w & 0x80:
                    if tx:
                        osr = tx.pop(0)
                    else:
                        stall = True
                else:
                    rx.append(isr)
                    isr = 0
            elif op == 5:  # MOV
                dst, oper, source = (w >> 5) & 7, (w >> 3) & 3, w & 7
                values = {1: x, 2: y, 3: 0, 6: isr, 7: osr}
                if source not in values or oper == 2:
                    raise Unsupported(f'{p.name}: mov source {source} op {oper}')
                v = values[source]
                if oper == 1:
                    v = ~v & 0xFFFFFFFF
                if dst == 1:
                    x = v
                elif dst == 2:
                    y = v
                elif dst == 6:
                    isr = v
                elif dst == 7:
                    osr = v
                else:
                    raise Unsupported(f'{p.name}: mov destination {dst}')
            elif op == 6:  # IRQ
                pass
            elif op == 7:  # SET
                dst, data = (w >> 5) & 7, w & 0x1F
                if dst == 0:
                    outs = data
                elif dst == 1:
                    x = data
                elif dst == 2:
                    y = data
                elif dst == 4:
                    if pindirs and not data:
                        self.releases.append(t)
                    pindirs = data
                    self.drove_pins = self.drove_pins or bool(data)
                else:
                    raise Unsupported(f'{p.name}: set destination {dst}')
            else:
                raise Unsupported(f'{p.name}: opcode {op}')

            if stall:
                t += 1
                continue
            for cycle in range(1 + delay):
                if pindirs:
                    self.driven.append((t + cycle, outs & 1))
            pc = nxt
            t += 1 + delay
        return self


def levels(states, active_high=False, inputs=16):
    """Pin levels over time from (from_cycle, closed_mask) steps."""
    def at(t):
        closed = 0
        for start, mask in states:
            if t >= start:
                closed = mask
        return closed if active_high else ~closed & ((1 << inputs) - 1)
    return at


def us(cycles):
    return cycles * 1_000_000 / CLOCK_HZ


failures = []


def check(ok, message):
    print(('  ok    ' if ok else '  FAIL  ') + message)
    if not ok:
        failures.append(message)


def reports_each_change_once(run, states, active_high=False):
    seen = [raw if active_high else ~raw & run.mask for _, raw in run.reports]
    expected = [mask & run.mask for _, mask in states]
    check(seen == expected,
          f'reports each change once: expected {[hex(m) for m in expected]}, got {[hex(m) for m in seen]}')


def pulses(run):
    """The driven GPIO 15-18 levels, split into one list per reset."""
    out, current, last = [], [], None
    for t, level in run.driven:
        if last is not None and t != last + 1:
            out.append(current)
            current = []
        current.append(level)
        last = t
    if current:
        out.append(current)
    return out


def main():
    ms = CLOCK_HZ // 1000
    # Input 1 closes, then input 14 (GPIO 16, one of the stateful four) closes
    # and opens again, then input 16 (GPIO 18) closes.
    states16 = [(0, 0x0000), (ms // 4, 0x0001), (ms // 2, 0x2001), (3 * ms // 4, 0x0001),
                (ms, 0x8001)]

    print('IO_16_8_1, 16 inputs')
    run = Run(Program('ActiveLow16Switches.pio.h', 'active_low_16_switches_pio'), 16,
              levels(states16)).run(5 * ms // 4)
    reports_each_change_once(run, states16)
    shapes = pulses(run)
    check(bool(shapes) and all(s == RESET_PULSE for s in shapes),
          f'GPIO 15-18 pulse is low 2, high 6, low 2 in all {len(shapes)} resets')
    first_reads = [next((r for r in run.reads if r > rel), None) for rel in run.releases]
    check(all(r is not None and r - rel == READ_AFTER_RELEASE
              for r, rel in zip(first_reads, run.releases)),
          f'every reset is followed by a read {READ_AFTER_RELEASE} cycles after the release')
    stale = [r for r in run.reads
             if not any(0 < r - rel <= 64 for rel in run.releases)]
    check(not stale, 'no read happens long after the last reset')
    gaps = [us(b - a) for a, b in zip(run.releases, run.releases[1:])]
    check(bool(gaps) and max(gaps) <= MAX_IDLE_RESET_GAP_US,
          f'resets at least every {MAX_IDLE_RESET_GAP_US} us (longest gap {max(gaps):.2f} us)')

    print('Opto_16, 16 inputs, active-high')
    states_opto = [(0, 0x0000), (ms // 4, 0x0001), (ms // 2, 0x4001), (3 * ms // 4, 0x0000)]
    run = Run(Program('Plain16Switches.pio.h', 'plain_16_switches_pio'), 16,
              levels(states_opto, active_high=True)).run(ms)
    reports_each_change_once(run, states_opto, active_high=True)
    check(not run.drove_pins, 'never drives a pin')

    for inputs, header, name in ((8, 'ActiveLow8Switches.pio.h', 'active_low_8_switches_pio'),
                                 (4, 'ActiveLow4Switches.pio.h', 'active_low_4_switches_pio')):
        print(f'{inputs} inputs, beside a switch matrix')
        states = [(0, 0x0), (ms // 4, 0x1), (ms // 2, 0x0)]
        run = Run(Program(header, name), inputs, levels(states, inputs=inputs)).run(ms)
        reports_each_change_once(run, states)
        check(not run.drove_pins, 'never drives a pin')

    if failures:
        print(f'\n{len(failures)} check(s) failed')
        return 1
    return 0


if __name__ == '__main__':
    try:
        sys.exit(main())
    except Unsupported as e:
        sys.exit(f'unsupported instruction, extend the model: {e}')
