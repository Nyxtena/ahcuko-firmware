# Phase 1.5 Annex — Flipper Zero RPC Wire Protocol

> Read from **official source code only** (github.com/flipperdevices: `flipperzero-firmware`
> and `flipperzero-protobuf`), Phase 1.5, authorized by Jacob Oct 21, 2026.
> Firmware code was read at the **`1.4.3` tag** unless a section says otherwise — the same
> release line the target device (Ahcuko) runs: official 1.4.3, API 87.1, protobuf 0.25.
> Claims are marked **VERIFIED-FROM-CODE** or **INFERRED**. Line numbers cite the raw-file
> web view as rendered on 2026-10-21; function/symbol names are the stable anchors.

---

## 1. Serial framing (USB / VCP)

**Scheme: nanopb "delimited" protobuf — a varint length prefix before each `PB_Main`
message. No delimiter bytes, no escaping, no checksum at the RPC layer.**

- **VERIFIED-FROM-CODE** — `flipperzero-firmware@1.4.3`, `applications/services/rpc/rpc.c`,
  `rpc_send()`: every outbound message is encoded with
  `pb_encode_ex(&ostream, &PB_Main_msg, message, PB_ENCODE_DELIMITED)` (sizing pass and
  real pass), then handed raw to the transport's send-bytes callback.
- **VERIFIED-FROM-CODE** — same file, `rpc_session_worker()`: every inbound message is
  decoded with `pb_decode_ex(&istream, &PB_Main_msg, ..., PB_DECODE_DELIMITED)`, where the
  istream reads from the session's `FuriStreamBuffer` via `rpc_pb_stream_read()`.
- **VERIFIED-FROM-CODE** — same file, the worker's own comment states the design verbatim:
  *"Protobuf can't determine start and end of message. Handle this by adding varint at
  beginning of a message (PB_ENCODE_DELIMITED)."* So the prefix is a **protobuf varint of
  the payload length**, followed immediately by exactly that many payload bytes.
- **VERIFIED-FROM-CODE** — the RPC stream buffer is `RPC_BUFFER_SIZE` = **1024 bytes**
  (`applications/services/rpc/rpc.h`, `#define RPC_BUFFER_SIZE (1024)` @1.4.3).
- **Client boundary detection (consequence of the above):** read a varint, then read exactly
  that many bytes, decode one `PB_Main`, repeat. There is no magic/sync word to scan for.
- **Resynchronisation: there is none — by design.** **VERIFIED-FROM-CODE** — `rpc.c`,
  `rpc_session_worker()` decode-failure path: on any decode failure the device resets the
  stream buffer, sends an empty `PB_Main` with `command_id = 0` and
  `command_status = ERROR_DECODE`, fires the session-closed callback (the companion also
  gets a session-closed indication), and — if the session owner is BLE — disconnects BLE
  and restores the default BT profile (*"we can't be sure next bytes are varint for next
  message, so the only way to close session"*). A client that loses framing must abandon
  the session and start a new one.
- **VERIFIED-FROM-CODE** — edge case, same worker: a **zero-length delimited message**
  decodes as an empty `Main` with `which_content == 0`, which the code explicitly treats
  as a **decode error** (there is no content field with tag 0). Do not send empty frames
  as keepalives.
- **INFERRED** — no handshake, version negotiation, or auth exists at the RPC layer: the
  worker loop goes straight to delimited decode, and `rpc_send()` goes straight to
  delimited encode. (Version info is available *inside* the protocol instead:
  `Main.system_protobuf_version_request/response`, `flipper.proto` fields 39/40.)

## 2. Session start — `start_rpc_session` (USB)

- **VERIFIED-FROM-CODE** — `rpc.c` @1.4.3, `rpc_on_system_start()`: the RPC service
  registers the CLI command **`start_rpc_session`** (flag `CliCommandFlagParallelSafe`)
  bound to `rpc_cli_command_start_session`.
- **VERIFIED-FROM-CODE** — `applications/services/rpc/rpc_cli.c` @1.4.3,
  `rpc_cli_command_start_session()`, the whole flow:
  1. Client sends the text command line `start_rpc_session` to the CLI like any other
     CLI command (the command is consumed by the normal CLI parser; the device-side code
     does not define the line terminator — see Unestablished).
  2. Device calls `furi_hal_usb_lock()` (holds the USB configuration for the session's
     lifetime), then `rpc_session_open(rpc, RpcOwnerUsb)`.
  3. On failure only, the device prints the plain-text line `Session start error\r\n`
     and unlocks USB. **On success the device sends nothing** — no banner, no prompt,
     no acknowledgement bytes.
  4. The CLI thread then pumps the pipe into the session: chunks of at most
     `CLI_READ_BUFFER_SIZE` = **64 bytes**, `rpc_session_feed(..., timeout 3000)`,
     until the pipe closes or the session-close flag is set.
  5. Session output is written back to the same pipe verbatim
     (`rpc_cli_send_bytes_callback()` → `pipe_send()`): delimited protobuf only.
- **What the client sends/expects, byte-exact:** after the command line, the client
  immediately speaks §1 framing (varint length + `PB_Main`). The first message is
  ordinarily a request (e.g. `system_ping_request`); the device will not speak first.
  **VERIFIED-FROM-CODE** (no device-initiated bytes exist in the start path above).
- **Session end:** **VERIFIED-FROM-CODE** — `rpc.c`: a handler is registered for
  `PB_Main_stop_session_tag`; `rpc_close_session_process()` replies with an empty `OK`
  message, then invokes the transport's closed callback. In `rpc_cli.c` that sets
  `session_close_request`, the pump loop breaks, `rpc_session_close()` runs, USB is
  unlocked, and the CLI returns to its text prompt. (`Main.stop_session` is field **19**
  in `flipper.proto`.)

## 3. BLE transport

The BLE path feeds the **same** `RpcSession`/delimited framing (§1); only the transport
differs. **VERIFIED-FROM-CODE** — `targets/f7/ble_glue/services/serial_service.h` @1.4.3
states it in its header comment: *"Serial service. Implements RPC over BLE, with flow
control."*

### Profile and services
- **VERIFIED-FROM-CODE** — `targets/f7/ble_glue/profiles/serial_profile.c` @1.4.3:
  the default Serial profile starts three services — Device Info, Battery, and the
  **Serial service** (`ble_svc_serial_start()`).
- **VERIFIED-FROM-CODE** — same file: the **advertised** GAP service UUID is 16-bit
  **0x3080, OR'd with the hardware colour code** (`adv_service.Service_UUID_16 = 0x3080`,
  then `|= furi_hal_version_get_hw_color()`). This is the advertisement UUID, *not* the
  GATT service UUID below.
- Connection parameters in the same file: interval min 0x06 (7.5 ms), max 0x24 (45 ms),
  slave latency 0, bonding on, PIN-code pairing.

### Serial GATT service — UUIDs
**VERIFIED-FROM-CODE** — `targets/f7/ble_glue/services/serial_service_uuid.inc` @1.4.3
defines the 128-bit UUIDs as byte arrays in STM32 (little-endian) storage order. Raw
arrays are quoted exactly; the canonical string form is the byte-reversed rendering
(standard for ST UUID storage):

| Item | Raw byte array (as in source) | Canonical UUID |
|---|---|---|
| Serial service | `00 00 fe 60 cc 7a 48 2a 98 4a 7f 2e d5 b3 e5 8f` | `8fe5b3d5-2e7f-4a98-2a48-7acc60fe0000` |
| TX characteristic (device → client) | `00 00 fe 61 8e 22 45 41 9d 4c 21 ed ae 82 ed 19` | `19ed82ae-ed21-4c9d-4145-228e61fe0000` |
| RX characteristic (client → device) | `00 00 fe 62 8e 22 45 41 9d 4c 21 ed ae 82 ed 19` | `19ed82ae-ed21-4c9d-4145-228e62fe0000` |
| Flow control characteristic | `00 00 fe 63 8e 22 45 41 9d 4c 21 ed ae 82 ed 19` | `19ed82ae-ed21-4c9d-4145-228e63fe0000` |
| RPC status characteristic | `00 00 fe 64 8e 22 45 41 9d 4c 21 ed ae 82 ed 19` | `19ed82ae-ed21-4c9d-4145-228e64fe0000` |

### Characteristic properties
**VERIFIED-FROM-CODE** — `targets/f7/ble_glue/services/serial_service.c` @1.4.3,
`ble_svc_serial_chars[]`:
- **RX**: WRITE | WRITE_WITHOUT_RESP | READ, authenticated read/write permissions,
  max value length `BLE_SVC_SERIAL_DATA_LEN_MAX` = **486**.
- **TX**: READ | **INDICATE**, authenticated read. Outgoing data is chunked at
  `BLE_SVC_SERIAL_CHAR_VALUE_LEN_MAX` = **243** bytes per GATT update
  (`ble_svc_serial_update_tx()`); a server-confirmation event produces the
  `SerialServiceEventTypeDataSent` callback.
- **Flow control**: READ | NOTIFY, fixed `uint32`.
- **RPC status**: READ | WRITE | NOTIFY, fixed `uint32`; values
  `SerialServiceRpcStatusNotActive = 0`, `SerialServiceRpcStatusActive = 1`
  (set via `ble_svc_serial_set_rpc_active()`). If the client **writes 0**, the device
  raises `SerialServiceEventTypesBleResetRequest`.

### Flow control (byte-credit)
**VERIFIED-FROM-CODE** — `serial_service.c` @1.4.3:
- When callbacks are set (`ble_svc_serial_set_callbacks()`), the device stores
  `buff_size`, initialises `bytes_ready_to_receive = buff_size`, and publishes the value
  on the Flow Control characteristic **byte-reversed** (`REVERSE_BYTES_U32`, i.e.
  big-endian on the wire).
- Every RX write decrements `bytes_ready_to_receive` by the written length (with a
  logged warning if a client overruns its credit: *"Received %d, while was ready to
  receive %d bytes. Can lead to buffer overflow!"*).
- When the RPC input buffer becomes empty, the RPC layer's buffer-is-empty callback
  reaches `ble_svc_serial_notify_buffer_is_empty()`: if the credit counter is 0, it is
  reset to the full `buff_size` and the Flow Control characteristic is updated again —
  that notification is the client's signal it may send another `buff_size` bytes.
- **INFERRED** — the credit quantum is the RPC session's available stream-buffer space;
  the RPC stream buffer itself is 1024 bytes (§1). The exact `buff_size` argument passed
  by the BT service call site was not read (see Unestablished).

## 4. Screen streaming & input

### Message names (protocol definitions)
**VERIFIED-FROM-CODE** — `flipperzero-protobuf` (dev branch), `flipper.proto`, `Main`
oneof content fields:
- `gui_start_screen_stream_request` = 20 (`PB_Gui.StartScreenStreamRequest`)
- `gui_stop_screen_stream_request` = 21 (`PB_Gui.StopScreenStreamRequest`)
- `gui_screen_frame` = 22 (`PB_Gui.ScreenFrame`) — device → client stream frames
- `gui_send_input_event_request` = 23 (`PB_Gui.SendInputEventRequest`)
- `gui_start_virtual_display_request` = 26 (`PB_Gui.StartVirtualDisplayRequest`)
- `gui_stop_virtual_display_request` = 27 (`PB_Gui.StopVirtualDisplayRequest`)
- (`stop_session` = 19, §2.)

**VERIFIED-FROM-CODE** — same repo, `gui.proto`:
- `ScreenFrame { bytes data = 1; ScreenOrientation orientation = 2; }`
- `SendInputEventRequest { InputKey key = 1; InputType type = 2; }`, with
  `InputKey ∈ {UP, DOWN, RIGHT, LEFT, OK, BACK}` and
  `InputType ∈ {PRESS, RELEASE, SHORT, LONG, REPEAT}`.
- `StartVirtualDisplayRequest { ScreenFrame first_frame = 1; bool send_input = 2; }`
  — an *inbound* (client-rendered) display, the inverse of the screen stream.

### Firmware behaviour
**VERIFIED-FROM-CODE** — `flipperzero-firmware@1.4.3`,
`applications/services/rpc/rpc_gui.c`:
- **Frames are event-driven, not polled:** start-stream registers a GUI *framebuffer
  callback* (`gui_add_framebuffer_callback()`); each framebuffer update copies the frame
  into a single reused `PB_Main` (`which_content = PB_Main_gui_screen_frame_tag`) and
  flags a dedicated transmit thread (`GuiRpcWorker`). No fixed FPS exists in the code —
  cadence follows the device UI's own redraw rate.
- **Pacing:** after each `rpc_send()`, the transmit thread applies a "guaranteed
  bandwidth reserve" delay of `transmit_time / 20` ticks, capped at **500** ticks,
  before the next frame.
- Frame payload size is the GUI framebuffer size (`gui_get_framebuffer_size()`); a code
  comment in the virtual-display path notes the framebuffer/XBM buffer is **1024 bytes**
  (128×64, 1 bpp).
- **Input:** `rpc_system_gui_send_input_event_request_process()` validates key/type
  against `InputKeyMAX`/`InputTypeMAX`, stamps a **software sequence counter**
  (per-key counters incremented on PRESS, cleared on RELEASE), publishes the event to
  the input pubsub, and replies with an empty `OK`.
- **Virtual display input forwarding:** if `send_input` was set, physical key events on
  the virtual-display viewport are sent *to the client* as
  `gui_send_input_event_request` messages with `command_id = 0`.

## 5. Version pinning

- **VERIFIED-FROM-CODE** — All firmware behaviour above was read at the
  **`1.4.3` tag** of `flipperzero-firmware` — the release the target device runs
  (its own `device_info`: firmware 1.4.3, API 87.1, protobuf 0.25, clean tree).
- **VERIFIED-FROM-CODE** — The same framing code was then checked at **dev HEAD**
  (`flipperzero-firmware`, branch `dev`): `rpc.c` there uses the identical
  `pb_decode_ex(..., PB_DECODE_DELIMITED)` in the session worker and the identical
  `pb_encode_ex(..., PB_ENCODE_DELIMITED)` pair in `rpc_send()`, with the same
  decode-failure comment and close-session behaviour. **No framing-relevant difference
  between the 1.4.3 tag and dev HEAD was identified.**
- **Caveat (proto files):** `flipper.proto`/`gui.proto` were read from the
  `flipperzero-protobuf` repo's **dev branch**, not from the exact submodule commit
  pinned inside firmware 1.4.3 (see Unestablished). The GUI field numbers (20–23,
  26–27) were cross-checked against the 1.4.3 firmware's generated-tag usage
  (`PB_Main_gui_screen_frame_tag`, `PB_Main_gui_send_input_event_request_tag`, … in
  `rpc_gui.c` @1.4.3) and are consistent. Protobuf field numbers are append-only by
  project practice; treat message *names/numbers* above as reliable for 1.4.3 and
  re-pull the pinned `.proto` set before generating client bindings.

## Unestablished

1. The exact `flipperzero-protobuf` commit pinned as the submodule in firmware 1.4.3,
   and whether its `gui.proto`/`flipper.proto` differ in any detail from the dev-branch
   copies read here.
2. The exact line terminator the CLI parser expects after `start_rpc_session`
   (the RPC/CLI source read does not specify it; the command is consumed by the general
   CLI layer, which was not read in this pass).
3. The exact `buff_size` value the BT service passes to
   `ble_svc_serial_set_callbacks()` (the `bt_service` call site was not read); it is
   bounded by the 1024-byte RPC stream buffer.
4. BLE MTU negotiation specifics for the serial profile beyond the characteristic
   length caps (486/243 bytes) visible in `serial_service.h`.
5. Whether any framing-relevant change exists in firmware releases *after* 1.4.3 other
   than dev HEAD (intermediate tags were not diffed).

---

*Sources (all github.com/flipperdevices): flipperzero-firmware @1.4.3 —
applications/services/rpc/{rpc.c, rpc.h, rpc_cli.c, rpc_gui.c},
targets/f7/ble_glue/profiles/serial_profile.c,
targets/f7/ble_glue/services/{serial_service.c, serial_service.h, serial_service_uuid.inc};
flipperzero-firmware @dev — applications/services/rpc/rpc.c (framing comparison);
flipperzero-protobuf @dev — flipper.proto, gui.proto.*
