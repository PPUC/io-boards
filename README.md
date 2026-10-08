# PPUC I/O boards — RP2040 firmware

The firmware for the PPUC I/O boards. One RP2040 image per board type, built
with PlatformIO.

A board reads the switches it is wired to, drives the coils, lamps and LEDs it
is wired to, and answers a host over an RS485 bus. It is deliberately not where
a game lives: the host decides *what* should happen, and a board decides *how
long copper is energised*. That split is the point of the firmware, and it is
why a hung or disconnected host cannot cook a coil — the host's output bit is a
request, and the board owns the pulse envelope and the watchdog that ends it.

Flippers, slingshots and pop bumpers are the exception that proves the rule.
They run board-locally from `fastFlipSwitch`, because a flipper that waited for
a round trip would not feel like a flipper.

**This repository owns the wire protocol.** `src/PPUCProtocolV2.h` and
`src/PPUCBoardTypes.h` are the authority for frame layout, board type values
and each board's GPIO map, and both are shared verbatim with the host: libppuc
compiles against them and uses the same tables to refuse a configuration that
puts a device on a pin the board does not have. A change here reaches a host
build only when `IO_BOARDS_SHA` is bumped in libppuc, so protocol changes have
to be made as if someone else's build depends on them, because it does.

For what PPUC is as a project — the config tool, the host, game rules, the
reasons any of this exists — see [ppuc.org](https://ppuc.org) and
[PPUC/docs](https://github.com/PPUC/docs). This README is about the firmware.

## Board types

Four board types exist. The type is on the wire: a board reports it, and the
host refuses to flash an image built for a different one, because the same GPIO
is an input on one board and a coil output on another — a mismatch does not
merely misbehave, it drives an output into an input.

The environment name, the type name and the CI artefact name are all the same
string, which is how an image on disk is paired with the board that asked for
it.

| Board | Type | What it is | Hardware |
|---|---|---|---|
| `IO_16_8_1` | `0x01` | 16 inputs that double as low-power outputs, 8 high-power outputs, a 4-column switch matrix scanned on the inputs | [Hardware_IO_16_8_1](https://github.com/PPUC/Hardware_IO_16_8_1) |
| `IO_16x8_matrix` | `0x02` | 16 inputs and 8 strobe outputs, for the switch matrix of an original playfield harness | [Hardware_IO_16x8_matrix](https://github.com/PPUC/Hardware_IO_16x8_matrix) |
| `Out_8x10` | `0x03` | A lamp matrix: 8 high-side columns by 10 low-side rows | [Hardware_Out_8x10](https://github.com/PPUC/Hardware_Out_8x10) |
| `Opto_16` | `0x04` | 16 opto inputs and nothing else | [Hardware_Opto_16](https://github.com/PPUC/Hardware_Opto_16) |

Every board carries the special output, a WS2812 data line on GPIO 29, whatever
else it has or has not got. That matters when placing boards: an addressable
string does not need a board with drivers on it.

**Only `IO_16_8_1` has been validated on real hardware.** The firmware says so
itself, in `BoardTypeValidatedOnHardware()`, and the host reads it to decide
whether a board may be flashed unattended. For the other three the pin maps are
transcribed from the KiCad schematics but unverified, and the output stages for
`IO_16x8_matrix` and `Out_8x10` are not implemented yet. Treat them as declared,
not delivered.

A few board-local details are worth knowing before reading the code, because
they are not symmetric and look like mistakes if you assume they are:

* `IO_16x8_matrix`'s outputs run the opposite way to `IO_16_8_1`'s — `Out_1` is
  GPIO 27 descending to `Out_8` on GPIO 19 — and each is an NPN stage with a
  pull-up, so a strobe is active-low on the connector and active-high on the pin.
* `Out_8x10` has no PWM at all, on purpose: `Lo_5`..`Lo_10` share RP2040 PWM
  channels with `Hi_1`..`Hi_6`, so dimming one would drive the other. Its
  outputs go through the lamp matrix stage instead.
* `Opto_16`'s 16 transmitter LEDs are wired to the supply rather than to the
  RP2040, so there is nothing for the firmware to drive.

Adding a board should mean editing `src/PPUCBoardTypes.h` and adding a
PlatformIO environment, and nothing else in the firmware.

## Effect Stack

The firmware effect stack arbitrates priority per effect target. For ordinary
devices, the target is the physical `EffectDevice`.

Addressable LED strips are different because one WS2812 strip can be divided
into multiple WS2812FX segments. Each segment is treated as its own stack target:
a higher-priority effect on one segment must not terminate, suspend, or replace
an effect running on another segment of the same strip.

Implementation detail: `Effect::deviceStackScope()` defaults to `0`, preserving
the old per-device behavior for PWM, built-in LED, and other non-segmented
effects. `WS2812FXEffect` overrides it with the segment number, and
`EffectsController` keys running/suspended/replacement checks by
`EffectDevice* + deviceStackScope()`.

## Built-in LED

The board's built-in LED is the only status indicator the hardware has, so every
state it shows has to be distinguishable across a room without counting
anything. Two pieces of firmware drive it, and they hand over:

* `src/main.cpp` on core 0, from power-up until the host configures the board
* `EffectsController` on core 1, from then on

| State | Pattern | Meaning |
|---|---|---|
| Powered, no host | 1 s on, 100 ms off | The board is alive and waiting to be configured |
| Configuring | Flicker per config frame | Addressed config frames are arriving |
| Running | 200 ms steps across a 1 s cycle | Normal operation |
| Transport error | 100 ms toggle | Frames are arriving corrupt |
| Error cleared | Solid on | Transport recovered |
| Firmware update | Two 80 ms pulses, then ~900 ms dark | An image is being staged |

The configuration flicker is data-driven rather than timed: every `ConfigEvent`
whose `boardId` matches this board flips the LED, so the rate is the rate frames
are arriving. It is the one pattern that shows the host is actually talking to
*this* board rather than to its neighbours.

The firmware update pattern is the only one with a gap in it. Everything else is
either mostly on or evenly toggling, which is what makes it readable at a glance
during a transfer that takes tens of seconds and otherwise looks like an idle
board.

Error and update states override the running pattern by effect priority, so the
most important thing the board is doing is what the LED shows.

### No boot stage pattern

Earlier firmware blinked the boot stage as a pulse count - up to nine 90 ms
pulses before each pause. It was added while chasing a reset problem and it
answered that question, but nobody reading a machine can count nine blinks and
name a stage.

It was also actively misleading. A board frozen part way through a pulse group
sits with the LED lit, which is indistinguishable from a healthy board that has
finished booting. That cost real debugging time: a hung board was read as merely
idle. The freeze had a cause and it is fixed, but the ambiguity was the
pattern's own.

So there is no boot pattern. From power-up the LED shows the ready pattern, and
a board that is lit and not blinking is not booting - it is stuck.

## Homebrew and EM machines

An electro-mechanical or homebrew machine needs no ROM and no CPU board. The
boards are exactly the same as in a retrofit install — there is no separate
firmware build — and a host running

    ppuc-pinmame --game <folder>

with `Engine = script` in the game's `ppuc.ini` acts as the CPU. Players, ball
counting, scoring, tilt and attract are owned by the host; scores are rendered to
a DMD.

The division of labour is unchanged from a ROM machine: the host decides *what*
should happen, and the boards decide how long copper is energised. Flippers,
slingshots and pop bumpers still run board-locally from `fastFlipSwitch`, so the
host is not in the flip path.

One board-local feature exists specifically for this case: a configured tilt
switch (`CONFIG_TOPIC_TILT_SWITCH`). While it is closed, boards inhibit their
fast-flip outputs, which is the only way to drop a flipper the player is holding.
The host asserts it from a board declared `virtual: true` in the game YAML.

See `ppuc/docs/EM_GAMES.md` for the configuration reference and
`ppuc_games/emdemo` for a complete reference machine.

## Replacing a CPU and its drivers

WIP. See [ppuc.org](https://ppuc.org).

## V2 Switch Refresh

The v2 host can send `kFrameSwitchRefresh (0x0D)` as a zero-payload host frame.
It uses the same `header.nextBoard` token chain as normal switch polling.

When the selected board receives the refresh token it:

* re-reads local switch state,
* restarts local switch-reading state machines,
* forces a full `SwitchStateFrame` reply even if no switch edge was queued.

Dedicated switch inputs physically sample their GPIOs during refresh. Switch
matrix inputs restart their PIO readers and resend their current stable states.
The host consumes these replies like normal switch updates, so a previously
missed trough or outhole change can still reach PinMAME.

Switch refresh is intended as a safety net and is separate from host-side ball
search. Ball search is implemented in `../ppuc` and only uses coils marked in
game YAML.

## Switch Debounce Modes

Dedicated switch inputs support a configurable debounce time in milliseconds and
a separate debounce mode. The time is configured per switch. The mode defines
how that time is applied.

The host configuration generated by `../config-tool` exports these values as:

```yaml
debounce: 3
debounceMode: fastFlip
```

The host library in `../libppuc` sends `debounce` as `CONFIG_TOPIC_DEBOUNCE_TIME`
and `debounceMode` as `CONFIG_TOPIC_MODE` for `CONFIG_TOPIC_SWITCHES`.

### `standard`

Use `standard` for switches where correctness is more important than the very
first raw edge.

The firmware waits until the new physical state has survived the configured
debounce window before reporting either edge. This filters normal contact bounce
and short noise pulses.

Good defaults:

* Rollovers and lanes: `3-5 ms` for microswitches, `5-10 ms` generally.
* Cabinet buttons, including credit/start buttons: `5-10 ms`.
* Spinners: `1-2 ms`.
* Slingshots and bumpers: `2-3 ms` for microswitches, `3-8 ms` for leaf
  switches.
* Stand-up targets and optos that need fast response: `2-5 ms`.
* Most drop targets: `5-10 ms`.
* Bouncy old leaf standups or drop targets: `8-15 ms`.
* Tilt and slam tilt: `10-12 ms`, or tune on the real machine.
* Coin door and service buttons: `5-10 ms`.
* Trough switches and drop-target bank all-down/reset-position switches:
  `5-10 ms`.

For slingshots, bumpers, and similar old assemblies that should fire locally,
use `standard` debounce and configure the associated PWM output with its fast
activation switch. The debounce mode and the local PWM fast-switch reaction are
separate settings.

### `fastFlip`

Use `fastFlip` for flipper buttons and possibly magna-save buttons.

A close edge is accepted with very low latency so the flipper reacts immediately.
An open edge must survive the configured debounce window before it is reported.
This prevents contact bounce immediately after a button press from dropping the
flipper again. Micro-flips still work when the physical release lasts longer
than the configured debounce time.

Good defaults:

* Microswitch flipper buttons: `1-3 ms`.
* Leaf flipper buttons: `2-5 ms`.

Do not use `fastFlip` for slingshots or bumpers just because they use local
fast-switch coil activation. Fast-switch output behavior and debounce behavior
are separate settings.

### Microswitches vs Leaf Switches

Choose the mode from gameplay semantics first, then tune the milliseconds for
the physical switch.

Microswitches usually have sharper transitions and can use lower debounce
values. Leaf switches can flex, vibrate, and bounce longer, so they usually need
higher values. A mode that is too aggressive can cause dangerous or confusing
behavior, such as a flipper dropping immediately after a press or a slingshot
firing when another switch is hit.

## Licence

GPLv3, which does not prevent commercial use but does carry its terms with it.

`src/PPUCProtocolV2.h` and `src/PPUCBoardTypes.h` are compiled into host
software under the same licence; libppuc is GPLv3 too.

The firmware integrates modified versions of other projects with the permission
of their authors:

* [sker65/pinball-lw3](https://github.com/sker65/pinball-lw3)
* [bitfieldlabs/afterglow](https://github.com/bitfieldlabs/afterglow)
* [bitfieldlabs/aggi](https://github.com/bitfieldlabs/aggi)

Contributions are welcome.

## Setup Development Environment

Install [PlatformIO](https://platformio.org/) — the CLI alone is enough. Everything
else is pinned in `platformio.ini` and fetched on the first build: the RP2040
platform, the Arduino core, and the four libraries this firmware uses.

Nothing has to be installed per board type. One checkout builds all four.

## Compile & Upload

One environment per board type, named exactly as the board type is named:

```
pio run -e IO_16_8_1
pio run -e IO_16_8_1 --target upload
```

The other environments are `IO_16x8_matrix`, `Out_8x10` and `Opto_16`.
`IO_16_8_1` is the default, so `pio run` on its own builds that one.

Upload goes over USB, so it needs the board in reach. A board already running
PPUC firmware can instead be updated over the RS485 bus by the host, which is
how boards under a playfield are updated without unplugging anything — see
`AllowFirmwareUpdate` in the host's `ppuc.ini`.

## Tests

The device logic is also built for the host, against the shims in `test/stubs/`,
so it runs on a laptop and in CI without an RP2040:

```
pio test -e native
```

## Troubleshooting

If the auto-detection of the USB ports during `pio run --target upload` doesn't succeed you need to specify them
explicitly. Run `pio device list` to list the available ports. Force the usage of a specifc USB port during upload:
```
pio run --target upload --device XYZ
```
