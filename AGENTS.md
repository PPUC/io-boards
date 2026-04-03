# AGENTS.md

## Scope

This repository contains the firmware for PPUC IO Boards used to drive pinball machines.

- Treat `src/`, `platformio.ini`, `library.json`, and the checked-in documentation as the project.
- Ignore any `*.ino` files in the repository root. They are not part of this firmware and must not influence code changes, reviews, or testing.
- The active hardware target is the RP2040-based IO board built with PlatformIO and the Earle Philhower Arduino core.

## Project Overview

The firmware runs on a dual-core RP2040 board.

- Core 0 owns `IOBoardController` and the RS485 bus.
- Core 1 runs `EffectsController`.
- The two controllers exchange events through `MultiCoreCrossLink`.
- The board ID is read from the analog DIP/resistor ladder on GPIO 28 in `IOBoardController`.
- `main.cpp` initializes RS485 on `Serial1`, starts core 1, and enables USB serial only when the debug DIP bit is set.

Main files:

- `src/main.cpp`: board boot, RS485 startup, watchdog, core split.
- `src/IOBoardController.*`: local hardware registration and config handling for switches, switch matrix, and PWM outputs.
- `src/EventDispatcher/*`: event routing, RS485 protocol handling, multicore forwarding.
- `src/PPUCProtocolV2.h`: `v2` wire protocol constants and helpers.
- `src/IODevices/*`: physical switch, switch-matrix, and PWM device implementations.

## Build And Validation

- Default build target: `platformio.ini` env `IO_16_8_1`.
- Baud rate for the new protocol is `250000` (`ppuc::v2::kBaudRate`), not the legacy `115200`.
- When validating protocol work, prefer firmware-level checks first:
  - `pio run`
  - targeted inspection of `EventDispatcher` debug counters over USB debug mode
- Be careful with watchdog behavior: when USB debug is disabled, high-power outputs are shut off if polling stalls.

## Working Rules

- Preserve the current event-driven architecture. Most device logic still expects legacy `Event` / `ConfigEvent` objects even on `v2`.
- When changing protocol code, inspect both `src/PPUCProtocolV2.h` and `src/EventDispatcher/EventDispatcher.cpp`.
- The firmware currently bridges `v2` bitmap frames back into legacy events for listeners. Do not remove that compatibility layer without updating the rest of the firmware.
- The intended host-side counterpart is `../libppuc`. For `v2` protocol work, verify both repos together because the wire protocol is now `v2` only.

## V2 Communication Protocol

Compared to `main`, branch `v2` replaces the old 7-byte event packets with framed RS485 messages defined in `src/PPUCProtocolV2.h`.

### Transport

- RS485 UART on `Serial1`
- Baud: `250000`
- Sync byte: `0xA5`
- Header size: 4 bytes
- CRC: 16-bit CCITT over header + payload

Frame header layout:

1. `sync`
2. `typeAndFlags`
3. `nextBoard`
4. `sequence`

Important constants:

- `kNoBoard = 0xFF`
- `kMaxBoards = 8`
- current hard limits in code: up to 256 coil bits, 256 lamp bits, 256 switch bits
- practical target for this project: 64 coil slots should be enough, with higher logical coil numbers mapped into that dense 64-slot bitmap by `MappingFrame`

### Frame Types

- `kFrameOutputState (0x01)`: full output snapshot, coils followed by lamps
  and packed GI levels
- `kFrameSwitchState (0x02)`: full switch bitmap sent by one board
- `kFrameHeartbeat (0x03)`: reserved/no payload
- `kFrameError (0x04)`: reserved/no payload
- `kFrameSetup (0x05)`: announces runtime bitmap sizes
- `kFrameMapping (0x06)`: maps dense bitmap indexes to logical coil/lamp/switch numbers
- `kFrameReset (0x07)`: resets boards
- `kFrameConfig (0x08)`: carries legacy config tuples `(boardId, topic, index, key, value)`
- `kFrameSwitchNoChange (0x09)`: token response when no switch bitmap changed
- `kFrameConfigAck (0x0A)`: addressed-board acknowledgment for config frames

Defined flags:

- `kFlagKeyframe = 0x10`
- `kFlagDelta = 0x20`
- `kFlagError = 0x80`

Right now the firmware consumes full snapshots. `kFlagDelta` exists in the protocol definition but is not implemented in the board logic.

### Runtime Model

The `v2` protocol is bitmap-based.

- Coils, lamps, and switches are addressed by dense bitmap index on the wire.
- GI uses 5 fixed runtime slots, one per GI string, with packed 4-bit levels.
- `SetupFrame` defines how many bits are active for each domain for the current game.
- `MappingFrame` binds each dense index to the legacy logical number used by the rest of the firmware.
- `EventDispatcher` stores that mapping in `coilIndexToNumber`, `lampIndexToNumber`, and `switchIndexToNumber`.

Implication:

- Wire-level bitmap size does not need to match the highest logical number.
- Logical device numbers may be sparse and high, as long as they are mapped into a compact dense bitmap.
- For coils specifically, the intended direction is a dense block of 64 wire slots, not 256, even if logical coil numbers are higher.

This is the compatibility strategy:

- Incoming output bitmaps are edge-diffed against the previous snapshot.
- For each changed bit, the firmware synthesizes legacy `Event` objects such as `EVENT_SOURCE_SOLENOID` and `EVENT_SOURCE_LIGHT`.
- Local switch events are mirrored into the dense `switchStates` bitmap.
- Incoming switch bitmaps are turned back into local fast switch events for listeners.

### Bus Flow

The verified `v2` flow between `libppuc` and the boards is:

1. Host resets boards with `ResetFrame`.
2. Host sends `ConfigFrame`s to register board-local hardware behavior.
3. The addressed board acknowledges accepted config with `ConfigAck`.
4. Host sends `SetupFrame`.
5. Host sends `MappingFrame`s.
6. Host repeatedly sends `OutputStateFrame`s containing the full coil/lamp snapshot.
7. `header.nextBoard` in an output frame acts as the poll token.
8. If `nextBoard == this boardId`, the board replies once:
   - `SwitchStateFrame` if any switch changed since the last reply
   - `SwitchNoChangeFrame` otherwise
9. The reply includes the next board token in its own `header.nextBoard`.
10. The host continues reading chained replies until `nextBoard == kNoBoard`.

Important implementation detail:

- The board does not independently broadcast switches to the host.
- It only responds when selected by the token in the incoming output frame.
- `switchDirty` is the gate that decides whether the board emits a real switch bitmap or `SwitchNoChange`.
- `libppuc` restarts polling from the first registered switch board on every output cycle.

### Runtime Cycle Summary

Ignoring setup and configuration, the steady-state `v2` runtime cycle is:

1. The CPU sends one full `OutputStateFrame` containing the complete coil and lamp snapshot for the current game.
2. All boards read that frame in parallel.
3. Each board applies the full output snapshot to its local state and generates internal legacy events only for changed bits.
4. Boards that monitor switches continuously mirror local switch changes into their RAM switch bitmap.
5. The output frame selects the first switch board by placing its board number into `header.nextBoard`.
6. The selected board replies once:
   - `SwitchStateFrame` with its full current switch bitmap when something changed since its last reply
   - `SwitchNoChangeFrame` otherwise
7. That reply carries the next switch-board token in its own `header.nextBoard`.
8. The CPU and all boards consume each switch reply and update their switch state accordingly.
9. The chain continues until a reply carries `kNoBoard`.
10. Then the next CPU output frame begins a new runtime cycle.

Important clarifications:

- Only boards registered as switch boards participate in the reply chain.
- Not every board transmits during every cycle.
- The CPU always restarts the chain from the first registered switch board; it does not rotate the first token between cycles.
- Frames do not contain an explicit sender board ID, so the chain is tracked by `nextBoard` token order.

### GI Transport

General Illumination is part of the `v2` runtime output payload.

- There are 5 fixed GI string slots.
- Each GI string carries a packed 4-bit brightness value.
- Valid values are `0..8`, where `0` means off.
- Firmware unpacks GI values and emits real `EVENT_SOURCE_GI` events.

This keeps GI separate from lamps, which matters because one addressable LED string may mix lamps, GI, and flashers.

### DMA Cutover

`v2` currently has a two-stage receive path in `EventDispatcher`.

- Bootstrap/fallback path: blocking parser reads framed messages before DMA is active.
- After a valid `SetupFrame`, the board attempts to claim two DMA channels and switches to DMA-based UART RX/TX.
- If DMA setup fails, the blocking parser remains the fallback path.
- Debug counters printed in USB debug mode:
  - `cutover_ok`
  - `cutover_fail`
  - `rx`
  - `rx_crc_fail`
  - `rx_sync_fail`
  - `rx_dma_restart`
  - `rx_dma_timeout`
  - `tx`
  - `tx_nochange`
  - `tx_fallback`

These counters are the fastest way to diagnose whether the issue is framing, CRC, DMA startup, or token flow.

## Confirmed Multi-Board Runtime Finding

- Real-machine testing showed that host-side V2 switch-chain timing affects visible lamp animation quality, not only switch test.
- After loosening `libppuc` switch-reply timing and making resync less aggressive, the lamp attract-mode animation became visibly correct and much faster.
- Treat the board firmware and host timing together as one system. If switch-chain timing is too aggressive, the host can churn sessions often enough to disturb normal output updates.
- For larger games with more boards, expect the timing sweet spot to move. Do not assume a single fixed timeout is ideal for every cabinet.
- Latest real-machine result: Time Warp attract mode ran for `1h40m4s` with no communication error messages from the host.
- Treat the current non-DMA firmware path plus present host timing as the strongest known-good multi-board runtime baseline so far.
- Do not change switch-reply timing or fallback TX behavior casually while the remaining work is focused on coil test and virtualized cabinet switches.

Firmware-side implications:

- Board-to-host switch reply fallback TX timing is sensitive.
- The fallback switch reply path currently avoids `HardwareSerial::flush()` and uses a bounded wire-time delay before switching RS485 back to RX.
- Keep that behavior unless there is a proven replacement, because earlier `flush()` behavior was correlated with board freezes during switch reply.

## Known Gaps And Risks

- The `v2` protocol path exists on this branch but has not been validated end-to-end against the intended `libppuc` `v2` counterpart.
- `libppuc v2` is now available locally and confirms the current startup sequence is mixed legacy plus `v2`, not pure `v2` from power-on.
- `kFrameHeartbeat` and `kFrameError` are defined but effectively placeholders in the current firmware.
- Sequence numbers are generated and parsed but are not yet used for replay detection, loss handling, or synchronization checks.
- `kFlagDelta` is defined but not used; all practical state transfer is keyframe/full-snapshot based.
- The frame format has no sender board ID, so switch-chain validation relies on token order (`nextBoard`) rather than explicit sender identity.
- GI strings are fixed-size in the runtime payload rather than dynamically mapped like coils, lamps, and switches.
- The 16-switch PIO reader has special handling for the last four stateful inputs. Regressions there are easy to miss because the other 12 inputs can still appear healthy.

## Next Bring-Up Focus

When resuming work on `v2`, start here:

1. Verify the exact frame order and field encoding used by the `libppuc` `v2` branch.
2. Confirm endian consistency for all 16-bit and 32-bit payload fields.
3. Exercise a single-board loop first:
   - setup
   - mappings
   - config
   - output frame
   - switch reply
4. Use USB debug mode on one board and inspect `V2DBG` counters while driving known switch transitions.
5. Only after single-board traffic is stable, test multi-board token passing via `nextBoard`.
6. If the last four dedicated switch inputs on IO_16_8_1 stop reporting, inspect both:
   - `src/IODevices/Switches.cpp` registration/range checks
   - the 16-switch PIO stateful-pin reset path for GPIO 15-18
7. If multi-board switch traffic mostly works but runtime animation degrades, coordinate timing changes with `libppuc` before changing board protocol logic.

## Current Freeze Status

- Runtime freezes are still observed after tens of seconds or minutes even when startup and short tests look healthy.
- Host-side timing and resync tuning change the symptom, but do not eliminate it consistently.
- Do not assume this is purely a host issue.
- The board-side switch-chain path remains a prime suspect:
  - receiving the switch token
  - deciding whether to reply
  - transmitting `SwitchState` / `SwitchNoChange`
  - forwarding to the next board
- The next focused debugging pass should add board-side counters or sticky flags for switch-token RX and switch-reply TX so freezes can be localized to board vs host.

## Virtual Board Notes

- The first implementation slice of virtual missing boards is host-side only in `libppuc`.
- Real boards remain authoritative for physically present switches.
- Missing configured switch boards may later be virtualized by the host, with all of their switches initialized open.
- Missing configured switch boards are now synthesized by the host as ordinary `SwitchState` / `SwitchNoChange` frames in the logical switch chain.
- Firmware should treat those frames exactly like frames from a real board; it must not care whether the sender was physical or virtual.
