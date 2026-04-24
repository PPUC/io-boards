# RS485 Firmware Updater Plan

## Goal

Add safe firmware updates for RP2040 IO boards over the existing RS485 bus.

Key constraints:

- Updates are only allowed immediately after a power cycle.
- Boards boot much earlier than the host computer and therefore must stay in
  bootloader mode until the host explicitly selects `runtime` or `update`.
- No normal board configuration may be applied before update intent is decided.
- Outputs must stay off while a board is in bootloader or update mode.
- The flash layout may be changed. An A/B application layout is preferred.

## Non-Goals For The First Slice

- No firmware transfer implementation yet.
- No flash layout change yet.
- No host-side updater binary yet.
- No runtime protocol changes yet.

The first implementation slice only establishes a clean version source of truth
in firmware so later handshake frames can report an authoritative version.

## Proposed Architecture

### 1. Bootloader-First Startup

After every power cycle, the RP2040 starts in a small bootloader image.

Bootloader responsibilities:

- read board identity
- initialize RS485 UART and CRC framing support
- keep all outputs off
- wait indefinitely for a host command
- accept only boot/admin protocol frames
- branch into one of two modes:
  - `runtime`: jump into the active application slot
  - `update`: stay in updater mode and accept a new firmware image

This matches the cabinet startup reality where boards are alive long before the
host OS and `ppuc` stack are ready.

### 2. Separate Boot/Admin Protocol

Do not overload the existing runtime V2 traffic for discovery and updates.

Add a small boot/admin protocol with explicit board identity in payloads.

Reason:

- the runtime switch-chain protocol can infer sender identity from token order
- boot discovery and update traffic cannot
- discovery must not cause reply collisions on a shared RS485 bus

Recommended host behavior:

- poll board IDs one by one
- do not broadcast a query that makes multiple boards answer at once

### 3. A/B Application Layout

Preferred flash layout:

- bootloader region
- slot A application
- slot B application
- metadata/state page

Metadata should track:

- active slot
- pending slot
- image size
- image CRC
- firmware version
- image valid flag
- boot attempt / rollback state

Update flow:

1. Bootloader enters `update` mode.
2. Host uploads a full image into the inactive slot.
3. Bootloader verifies image CRC and metadata.
4. Bootloader marks the new slot pending.
5. Bootloader reboots into the pending slot.
6. Application confirms a healthy boot.
7. Bootloader marks the slot committed.

This gives power-loss tolerance and rollback behavior that the current
single-image layout cannot provide safely.

## Proposed Protocol

### Boot/Admin Discovery

New frame family, likely in a dedicated boot protocol header:

- `Hello`
- `HelloAck`
- `UpdateBegin`
- `UpdateChunk`
- `UpdateChunkAck`
- `UpdateCommit`
- `UpdateResult`

Boot handshake rules:

1. Host opens serial.
2. Host sends `Hello(boardId, intent=runtime|update)` for each target board.
3. The addressed board responds with `HelloAck`.
4. For `runtime`, host learns board presence and versions before normal startup.
5. For `update`, host switches that board into firmware transfer mode.

`HelloAck` should report at least:

- board ID
- bootloader version
- application firmware version
- protocol version
- active slot
- capabilities flags
- current mode

### Runtime Handoff

Only after a successful `runtime` decision should the bootloader jump into the
normal application.

Then the current runtime startup sequence continues:

1. `RestartFrame`
2. `ConfigFrame` + `ConfigAck`
3. `SetupFrame`
4. `MappingFrame`s
5. runtime output/switch loop

This preserves the current architecture while making boot-time discovery and
update intent explicit.

## Host-Side Split

### io-boards

Responsibilities:

- bootloader image
- boot/admin protocol definitions
- flash layout
- firmware version reporting
- application confirmation after successful boot

### libppuc

Responsibilities:

- serial transport helpers reused for boot/admin traffic
- board inventory API
- updater/session API separate from `PPUC::Connect()`

Do not bury update behavior inside the runtime thread.

Recommended shape:

- keep `RS485Comm` for runtime transport
- add a separate admin/updater transport object

### ppuc

Responsibilities:

- normal runtime binary may print detected board versions
- dedicated updater binary should own firmware flashing

Recommended new executable:

- `ppuc-updater`

Likely commands:

- `inventory --serial ...`
- `update --serial ... --board N --image firmware.bin`

## Rollout Plan

### Phase 1

- unify firmware version source of truth in `io-boards`
- document updater architecture

### Phase 2

- add boot/admin protocol header
- add host-side board inventory handshake
- report board firmware/bootloader versions before runtime startup

### Phase 3

- introduce RP2040 bootloader project
- define flash layout and metadata format
- add application boot confirmation path

### Phase 4

- add chunked image upload with CRC verification
- add inactive-slot install and slot switch
- add `ppuc-updater`

### Phase 5

- integrate version compatibility checks into normal startup
- add rollback and interrupted-update recovery tests

## First Implementation Slice

The first slice implemented together with this document is:

- create a dedicated firmware version header in `io-boards`
- make existing firmware headers include that single source of truth
- align the repo metadata version with the reported firmware version

This prepares the codebase for boot-time `HelloAck` version reporting without
yet changing protocol behavior.
