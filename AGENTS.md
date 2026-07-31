# AGENTS.md

## Scope

This repository contains the firmware for PPUC IO Boards used to drive pinball
machines.

- Treat `src/`, `platformio.ini`, `library.json`, and the checked-in
  documentation as the project.
- Ignore any `*.ino` files in the repository root. They are legacy and must not
  influence code changes, reviews, or testing.
- The active hardware target is the RP2040-based IO board built with PlatformIO
  and the Earle Philhower Arduino core.
- `main` is the baseline. Feature branches such as `switch_matrix_refactoring`
  are work in progress and are not part of a pinned build.

This repo is the **source of truth for the RS485 wire protocol**. The host-side
counterpart is `../libppuc`, which copies the shared headers
(`PPUCProtocolV2.h`, `PPUCTimings.h`, `PPUCPlatforms.h`,
`EventDispatcher/Event.h`) into its own `third-party/include/io-boards/` at
dependency-staging time, pinned by `IO_BOARDS_SHA` in
`../libppuc/platforms/config.sh`. A protocol change here only reaches a host
build after that pin is bumped.

## Project Overview

The firmware runs on a dual-core RP2040.

- Core 0 owns `IOBoardController`, `EventDispatcher`, and the RS485 bus.
- Core 1 runs `EffectsController`.
- The two cores exchange events through `MultiCoreCrossLink`.
- The board address is read from the analog DIP/resistor ladder on GPIO 28 in
  `IOBoardController`.
- `main.cpp` sets `SYS_CLK_MHZ 200`, maps the UART explicitly, starts core 1,
  and enables USB serial only when the debug DIP bit is set.

Main files:

- `src/main.cpp`: boot staging, built-in-LED status patterns, RS485 startup,
  watchdog, core split.
- `src/PPUCProtocolV2.h`: `v2` wire protocol constants, frame types, payload
  structs, CRC helpers.
- `src/EventDispatcher/*`: frame parsing/emission, mapping tables, switch token
  chain, epoch/session handling, bridging of `v2` bitmaps into internal
  `Event`/`ConfigEvent` objects, `V2DBG` counters.
- `src/IOBoardController.*`: board address, local hardware registration, config
  topic handling for switches, switch matrix, and PWM outputs.
- `src/IODevices/*`: dedicated switches (`SwitchesPIO/*.pio`), switch matrix
  (`SwitchMatrixPIO/*.pio`, `SwitchMatrix8x16.pio`), PWM outputs including
  fast-flip safety.
- `src/EffectDevices/*`, `src/Effects/*`, `src/EffectsController.*`: the core-1
  effect engine.
- `src/PPUC.h`: firmware version macros, parsed by CI to validate release tags.

## Build And Validation

- Default build target: `platformio.ini` env `IO_16_8_1`.
- `pio run` to build, `pio run --target upload` to flash. If port
  auto-detection fails, use `pio device list` and `pio run --target upload
  --device <port>`.
- Library dependencies come from `platformio.ini`: `mkalkbrenner/WavePWM`,
  `PPUC/Adafruit_NeoPixel` (pinned), `PPUC/WS2812FX` (pinned), `Bounce2`,
  `RPI_PICO_TimerInterrupt`.
- CI (`.github/workflows/io-boards.yml`) builds the `IO_16_8_1` matrix entry,
  runs nightly, and fails a `vX.Y.Z` tag that does not match
  `FIRMWARE_VERSION_*` in `src/PPUC.h`.
- There are **no automated tests**. CI only proves that the firmware compiles.
  Adding a host-side protocol conformance test against `PPUCProtocolV2.h` is a
  stabilization goal.
- When validating protocol work, prefer firmware-level checks first: `pio run`,
  then targeted inspection of the `V2DBG` counters over USB debug mode.
- Careful with watchdog behavior: when USB debug is disabled, high-power outputs
  are shut off if polling stalls.
- USB debug mode changes timing enough to mask hardware and reset problems. A
  healthy debug-mode run is not proof that non-debug startup is healthy.

## Working Rules

- Preserve the current event-driven architecture. Most device logic still
  expects legacy `Event` / `ConfigEvent` objects even on `v2`; the firmware
  bridges `v2` bitmap frames back into those events for listeners. Do not remove
  that compatibility layer without updating the rest of the firmware.
- When changing protocol code, inspect both `src/PPUCProtocolV2.h` and
  `src/EventDispatcher/EventDispatcher.cpp`.
- For `v2` protocol work, verify against `../libppuc` as well — the wire
  protocol is `v2` only and both sides must move together.
- New board-local config topics need matching config generation in `../libppuc`
  and matching UI/export in `../config-tool`.
- Format with `.clang-format` before committing.

## V2 Communication Protocol

### Transport

- RS485 UART on `Serial1`, TX `GPIO0`, RX `GPIO1`, DE `GPIO2` (explicit mux
  setup in `main.cpp` — keep it).
- Baud: `115200` (`ppuc::v2::kBaudRate`). Planned follow-up: `250000` once
  restart/reset behavior is trustworthy.
- Sync byte `0xA5`, 5-byte header, 16-bit CCITT CRC over header + payload.

Header layout: `sync`, `typeAndFlags`, `nextBoard`, `sequence`, `epoch`.

Key constants:

- `kNoBoard = 0xFF`, `kMaxBoards = 8`
- `kMaxCoilBits = 64`, `kMaxLampBits = 256`, `kMaxSwitchBits = 256`
- defaults announced before `SetupFrame`: 24 coil bits, 64 lamp bits,
  64 switch bits
- `kGiStrings = 5`, `kGiLevelBits = 4`, `kMaxGiLevel = 8`

### Frame Types

- `kFrameOutputState (0x01)`: full output snapshot — coils, lamps, packed GI
- `kFrameSwitchState (0x02)`: full switch bitmap from one board, with status
  byte
- `kFrameHeartbeat (0x03)`: reserved, not implemented
- `kFrameError (0x04)`: reserved, not implemented
- `kFrameSetup (0x05)`: announces runtime bitmap sizes
- `kFrameMapping (0x06)`: binds dense bitmap indexes to logical numbers
- `kFrameReset (0x07)`: hard reboot
- `kFrameConfig (0x08)`: config tuple `(boardId, topic, index, key, value)`
- `kFrameSwitchNoChange (0x09)`: token response when nothing changed
- `kFrameConfigAck (0x0A)`: addressed-board acknowledgment
- `kFrameRestart (0x0B)`: soft restart, clears board-local config/runtime state
  without reboot
- `kFrameTrigger (0x0C)`: host-injected runtime event, delivered to listeners as
  an ordinary `Event(source, number, value)` — this is how host Lua rules reach
  board-local effects (`EVENT_SOURCE_EFFECT`)
- `kFrameSwitchRefresh (0x0D)`: zero-payload command that reuses the switch
  token chain and forces a full switch-state reply

Flags: `kFlagKeyframe = 0x10`, `kFlagDelta = 0x20`, `kFlagError = 0x80`.
`kFlagDelta` is defined but unused — all state transfer is full-snapshot.

Switch status flags returned with switch replies: `kStatusInSync`,
`kStatusNeedsSetup`, `kStatusMappingIncomplete`, `kStatusSequenceGap`,
`kStatusParserResynced`, `kStatusSwitchOverflow`. The host uses these to decide
whether a session resync is needed.

### Runtime Model

The `v2` protocol is bitmap-based.

- Coils, lamps, and switches are addressed by dense bitmap index on the wire.
- `SetupFrame` defines how many bits are active per domain for the current game.
- `MappingFrame` binds each dense index to the logical number used by the rest
  of the firmware; `EventDispatcher` stores `coilIndexToNumber`,
  `lampIndexToNumber`, `switchIndexToNumber`.
- Wire bitmap size does not need to match the highest logical number. Logical
  numbers may be sparse and high; coils specifically are mapped into a dense
  64-slot block.
- GI uses 5 fixed runtime slots with packed 4-bit levels (`0..8`, `0` = off),
  unpacked into real `EVENT_SOURCE_GI` events. GI stays separate from lamps
  because one addressable LED string may mix lamps, GI, and flashers.
- Incoming output bitmaps are edge-diffed against the previous snapshot; only
  changed bits synthesize legacy events (`EVENT_SOURCE_SOLENOID`,
  `EVENT_SOURCE_LIGHT`).
- Local switch events are mirrored into the dense `switchStates` bitmap;
  incoming switch bitmaps from other boards are turned back into local fast
  switch events for listeners.
- Frames carrying an epoch that does not match the current session epoch are
  ignored for runtime purposes.

### Bus Flow

1. Host sends `RestartFrame` to turn outputs off and clear session/config state
   without rebooting.
2. Host sends `ConfigFrame`s to register board-local hardware behavior.
3. The addressed board acknowledges accepted config with `ConfigAck`. This is
   also how the host detects board presence.
4. Host sends `SetupFrame`, then all `MappingFrame`s.
5. Host repeatedly sends `OutputStateFrame`s with the full coil/lamp/GI
   snapshot.
6. `header.nextBoard` in an output frame acts as the poll token. If it matches
   the local board id, the board replies once:
   - `SwitchStateFrame` if any switch changed since its last reply
   - `SwitchNoChangeFrame` otherwise
7. The reply carries the next board token in its own `header.nextBoard`; the
   host reads chained replies until `nextBoard == kNoBoard`.

`SwitchRefreshFrame` uses the same token chain: the selected board re-reads its
switch inputs, restarts its local switch readers, and replies with full state.

`ResetFrame` remains available as the hard-reboot recovery path only.

Important implementation details:

- Boards never broadcast switches on their own; they only answer the token.
- `switchDirty` decides between a real bitmap and `SwitchNoChange`.
- The host restarts the chain from the first registered switch board on every
  output cycle; it does not rotate the first token.
- Frames carry no sender board ID, so chain integrity relies on token order.
- The host may synthesize `SwitchState`/`SwitchNoChange` frames on behalf of
  missing (virtual) boards. Firmware must treat those exactly like frames from
  a real board.

### Debug Counters

With USB debug enabled, `EventDispatcher` prints a `V2DBG` line once per second:

```
V2DBG board= rx= rx_crc_fail= raw= raw_a5= raw_ff= tx= tx_nochange= xcore_drop=
```

These are the fastest way to tell whether a problem is framing, CRC, token flow,
or cross-core event loss.

## Effect Stack Notes

- `EffectsController` arbitrates running, suspended, and duplicate effects by
  effect stack target.
- The default target is the physical `EffectDevice`, matching the original
  per-device behavior for PWM and non-segmented effects.
- WS2812 LED strips are segmented devices. `WS2812FXEffect` returns its segment
  number from `deviceStackScope()`, so the effective stack target is
  `EffectDevice* + segment`.
- **Preserve this:** a higher-priority effect on one LED strip segment must not
  stop, suspend, or replace an effect on another segment of the same strip.
- If a future device has independently controllable subregions, give its effect
  class a distinct `deviceStackScope()` rather than broadening the stack key
  back to the whole device.

## Fast-Flip Safety

Fast-switch driven solenoids need explicit board-local safety behavior. Real
machine testing exposed a dangerous failure mode: a stuck fast-flip switch on a
kicker or jet bumper can repeatedly refire the coil and overheat it badly enough
to risk hardware damage.

This safety must live in firmware, not only in the host — fast-flip reactions
are board-local and latency-sensitive, and the board must stay safe even if the
host is busy, delayed, or disconnected.

Semantics in `src/IODevices/PwmDevices.*`:

- A fast-switch solenoid with `MinPulseTime` ignores switch toggles during that
  protected pulse window after activation.
- If the fast switch opens during `MinPulseTime`, the coil is not turned off
  immediately; it turns off once the minimum pulse has elapsed.
- A fast-switch solenoid with `MaxPulseTime` is forced off when that limit is
  reached, even if the switch is still closed.
- After such a max-pulse timeout, the coil must not refire until the switch has
  opened again and then closed again.
- The logic is keyed by logical switch number, so fast switches may live on
  other boards; the safety is not limited to same-board switch/coil pairs.

Preserve these properties unless the replacement is clearly safer and has been
validated on real hardware.

## Switch Debounce Modes

Configured per switch as `debounce` (ms) plus `debounceMode`, exported by
`../config-tool` and sent by `../libppuc` as `CONFIG_TOPIC_DEBOUNCE_TIME` and
`CONFIG_TOPIC_MODE` under `CONFIG_TOPIC_SWITCHES`.

- `standard` — report an edge only after the new state survived the debounce
  window. Default for playfield switches.
- `fastFlip` — accept the close edge immediately, require the open edge to
  survive the window. For flipper and magna-save buttons.
- `slowStable` — conservative stable-before-report. For tilt, coin door,
  trough, drop-target bank switches.

Debounce mode and local PWM fast-switch activation are **separate settings**.
A slingshot can use `standard` debounce and still fire locally via its fast
activation switch. See `README.md` for per-switch-type value recommendations.

## Board-Local Timing And Performance

- The switch token is forwarded *before* heavier runtime output/switch fanout
  work on core 0. This reduced the switch-reply margin needed on real hardware
  substantially. Keep that ordering.
- The fallback switch-reply TX path deliberately avoids
  `HardwareSerial::flush()` and uses a bounded wire-time delay before switching
  RS485 back to RX. Earlier `flush()` behavior correlated with board freezes
  during switch reply.
- Board-side reply latency and host-side timing are one system. If switch-chain
  timing is too aggressive, the host churns sessions often enough to disturb
  normal output updates and visibly degrade lamp animation.
- Best known-good evidence: Time Warp attract mode ran `1h40m4s` with no
  communication errors reported by the host.
- Protocol hardening must not cost throughput or latency. The bus carries a full
  snapshot plus the complete switch chain every 4 ms; extra per-frame bytes or
  round trips are a real cost. Measure output-frame cadence and switch-reply
  latency on hardware before and after.

## Known Gaps And Risks

- **Hard reset recovery is still not fully robust.** Soft `RestartFrame` is the
  normal path; some boards still fail to recover from a true `ResetFrame`
  without a power cycle. This is the top unresolved transport problem.
- `kFrameHeartbeat` and `kFrameError` are defined but are placeholders.
- `sequence` is generated and parsed but not used for replay detection, loss
  handling, or synchronization checks.
- `kFlagDelta` is defined but unused.
- Frames carry no sender board ID; switch-chain validation relies on token
  order.
- GI strings are fixed-size in the runtime payload rather than dynamically
  mapped like coils, lamps, and switches.
- UART DMA RX has been **removed**. The blocking framed parser is the only RX
  path. Older notes describing a DMA cutover and `cutover_ok`/`rx_dma_*`
  counters are obsolete.
- The 16-switch PIO reader has special handling for the last four stateful
  inputs (GPIO 15-18, see `src/IODevices/Switches.cpp`). Regressions there are
  easy to miss because the other 12 inputs still look healthy.
- Only `IO_16_8_1` is built, although `../config-tool` also knows `Out_8x10`.

## Next Bring-Up Focus

1. Re-test restart/reset recovery first; boards wedging across host restarts is
   the main open issue.
2. Use USB debug mode on one board and watch the `V2DBG` counters while driving
   known restart and switch-poll scenarios.
3. If the last four dedicated switch inputs stop reporting, inspect both
   `src/IODevices/Switches.cpp` registration/range checks and the 16-switch PIO
   stateful-pin reset path for GPIO 15-18.
4. If runtime animation or switch latency regresses, coordinate timing changes
   with `../libppuc` before changing board protocol logic.
