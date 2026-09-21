# PPUC - Pinball Power-Up Controller

The Pinball Power-Up Controller family is designed to enhance the capabilities of classic pinball machines of the 80s
and 90s and to drive the hardware of home brew pinball machines.

In existing machines, the controller is able to monitor all playfield switches, lights, and solenoids and to trigger and
distribute corresponding *events* to attached sub-systems.
In combination with PIN2DMD and TiltAudio it is possible to monitor DMD and sound commands, too.

One sub-system is the built-in EffectController which is able to drive additional LEDs, motors, and coils.
Other sub-systems could be video players or audio systems. The additional effects are bundled per pinball machine in
so-called *Pinball Power-Ups* (PPUs).

For homebrew and electro-mechanical machines the host software can act as the "CPU" itself, running the game logic aka rules and
communicating with the controllers. (WIP)

A special variation of that "CPU" will be suitable as replacement for a broken CPU of an existing machine.
The development happens as part of the [PinMAME project](https://github.com/mkalkbrenner/pinmame/tree/master/src/ppuc).

## Motivation

We want to enable people to be creative and to modernize old pinball machines using today's technology. Our goal is to
establish an open and affordable platform for that. Ideally people will publish their game-specific PPUs so others could
leverage and potentially improve them. We want to see a growing library of PPUs and a vital homebrew pinball community.

## Concept

### Enhancing / Modding an existing machine

The Pinball Power-Up Controllers consist of multiple micro controllers to perform several tasks in parallel. The entire
system is modular, so you can choose what you really need. The basic setup consists of a controller to capture a
pinball's events and another independent one to run effects.

We will provide several integrated boards and vendor specific adaptor boards (currently in development: Williams WPC,
Data East, Stern SAM and Whitestar).

These controllers integrate modified versions of other projects with the permission of their authors:
* https://github.com/sker65/pinball-lw3
* https://github.com/bitfieldlabs/afterglow
* https://github.com/bitfieldlabs/aggi

The Effect Controller should be able to drive hundreds (or thousands?) of LEDs, PWM devices, ... in parallel in a
non-blocking way.

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

### Homebrew machines

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

### Replacing a CPU (and drivers)

WIP, see [PPUC.org](https://ppuc.org).

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

The code is licenced under GPLv3. Be aware of the fact that your own *Pinball Power-Ups* (PPUs) need to be licenced
under a compatible licence.
That doesn't prevent any commercial use, but you need to respect the terms and conditions of GPLv3!

We would appreciate contributions to PPUC itself or as game-specific PPUs.

## Setup Development Environment

todo

## Compile & Upload

Example:
```
cd ppu/STTNG/InputController
pio run
pio run --target upload
cd ../EffectController
pio run
pio run --target upload
```

## Troubleshooting

If the auto-detection of the USB ports during `pio run --target upload` doesn't succeed you need to specify them
explicitly. Run `pio device list` to list the available ports. Force the usage of a specifc USB port during upload:
```
pio run --target upload --device XYZ
```
