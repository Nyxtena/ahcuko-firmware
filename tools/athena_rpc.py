#!/usr/bin/env python3
"""athena_rpc — Athena's minimal Flipper Zero RPC driver (USB CDC).

GPL-3.0 — part of ahcuko-firmware (see NOTICE.md at the repo root).
Written by Athena, 2026-10-21, against the official sources:
  - flipperzero-protobuf (field numbers, verified from the .proto files)
  - flipperzero-firmware 1.4.3, applications/services/rpc/rpc_storage.c
    (chunked-write semantics: one command_id, has_next chains frames,
    single response when has_next clears)

Wire format (per the Phase 1.5 annex): every PB_Main message is sent as
a varint payload-length prefix followed by exactly that many bytes of
nanopb/protobuf encoding. There is no handshake and no resynchronisation:
a decode error kills the session, so this driver never guesses.

Deliberately stdlib-only. Speaks just enough protobuf by hand:
  System.DeviceInfo, Storage.Write (chunked), Storage.Md5sum,
  App.Start, Gui.StartScreenStream / ScreenFrame / SendInputEvent,
  App state notifications, StopSession.
"""

import argparse
import hashlib
import json
import os
import select
import struct
import sys
import termios
import time
import zlib

# ---- PB_Main content field numbers (flipper.proto) ----
F_EMPTY = 4
F_STOP_SESSION = 19
F_APP_START = 16
F_APP_STATE = 58
F_GUI_STREAM_START = 20
F_GUI_STREAM_STOP = 21
F_GUI_FRAME = 22
F_GUI_INPUT = 23
F_DEVINFO_REQ = 32
F_DEVINFO_RESP = 33
F_STORAGE_LIST_REQ = 7
F_STORAGE_LIST_RESP = 8
F_STORAGE_WRITE = 11
F_STORAGE_MKDIR = 13
F_STORAGE_MD5_REQ = 14
F_STORAGE_MD5_RESP = 15

STATUS_OK = 0
STATUS_NAMES = {
    # PB_CommandStatus, transcribed from assets/protobuf/flipper.proto (1.4.3).
    0: "OK", 1: "ERROR", 2: "ERROR_DECODE", 3: "ERROR_NOT_IMPLEMENTED",
    4: "ERROR_BUSY", 14: "ERROR_CONTINUOUS_COMMAND_INTERRUPTED",
    15: "ERROR_INVALID_PARAMETERS",
    5: "ERROR_STORAGE_NOT_READY", 6: "ERROR_STORAGE_EXIST",
    7: "ERROR_STORAGE_NOT_EXIST", 8: "ERROR_STORAGE_INVALID_PARAMETER",
    9: "ERROR_STORAGE_DENIED", 10: "ERROR_STORAGE_INVALID_NAME",
    11: "ERROR_STORAGE_INTERNAL", 12: "ERROR_STORAGE_NOT_IMPLEMENTED",
    13: "ERROR_STORAGE_ALREADY_OPEN", 18: "ERROR_STORAGE_DIR_NOT_EMPTY",
    16: "ERROR_APP_CANT_START", 17: "ERROR_APP_SYSTEM_LOCKED",
    21: "ERROR_APP_NOT_RUNNING", 22: "ERROR_APP_CMD_ERROR",
    19: "ERROR_VIRTUAL_DISPLAY_ALREADY_STARTED",
    20: "ERROR_VIRTUAL_DISPLAY_NOT_STARTED",
}


# ---- minimal protobuf codec ----
def enc_varint(value):
    out = bytearray()
    while True:
        b = value & 0x7F
        value >>= 7
        if value:
            out.append(b | 0x80)
        else:
            out.append(b)
            return bytes(out)


def field_varint(num, value):
    return enc_varint((num << 3) | 0) + enc_varint(value)


def field_bytes(num, data):
    return enc_varint((num << 3) | 2) + enc_varint(len(data)) + data


def parse_fields(buf):
    """Parse a protobuf message into {field_no: [values]}.

    Varint values come back as ints, length-delimited as bytes."""
    fields = {}
    i = 0
    while i < len(buf):
        key, i = read_varint(buf, i)
        num, wt = key >> 3, key & 7
        if wt == 0:
            val, i = read_varint(buf, i)
        elif wt == 2:
            ln, i = read_varint(buf, i)
            val = buf[i:i + ln]
            i += ln
        elif wt == 5:
            val = buf[i:i + 4]
            i += 4
        elif wt == 1:
            val = buf[i:i + 8]
            i += 8
        else:
            raise ValueError(f"unsupported wire type {wt}")
        fields.setdefault(num, []).append(val)
    return fields


def read_varint(buf, i):
    result = 0
    shift = 0
    while True:
        b = buf[i]
        i += 1
        result |= (b & 0x7F) << shift
        if not (b & 0x80):
            return result, i
        shift += 7


def build_main(command_id, content_field, content=b"", has_next=False):
    msg = field_varint(1, command_id)
    if has_next:
        msg += field_varint(3, 1)
    if content_field is not None:
        msg += field_bytes(content_field, content)
    return msg


# ---- serial transport with nanopb framing ----
class Flipper:
    def __init__(self, port="/dev/ttyACM0"):
        self.fd = os.open(port, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
        a = termios.tcgetattr(self.fd)
        a[0] = 0
        a[1] = 0
        a[2] = termios.B230400 | termios.CS8 | termios.CREAD | termios.CLOCAL
        a[3] = 0
        a[4] = termios.B230400
        a[5] = termios.B230400
        a[6][termios.VMIN] = 0
        a[6][termios.VTIME] = 0
        termios.tcsetattr(self.fd, termios.TCSANOW, a)
        self.buf = bytearray()
        self.next_id = 1

    def _pump(self, timeout):
        r, _, _ = select.select([self.fd], [], [], timeout)
        if r:
            try:
                chunk = os.read(self.fd, 65536)
            except BlockingIOError:
                chunk = b""
            if chunk:
                self.buf += chunk
                return True
        return False

    def _read_exact_frame(self, timeout=10.0):
        deadline = time.time() + timeout
        while True:
            # try to parse a frame out of the buffer
            if self.buf:
                try:
                    ln, hdr = read_varint(bytes(self.buf), 0)
                except IndexError:
                    ln = None
                if ln is not None and len(self.buf) >= hdr + ln:
                    frame = bytes(self.buf[hdr:hdr + ln])
                    del self.buf[:hdr + ln]
                    return parse_fields(frame)
            if time.time() > deadline:
                raise TimeoutError("no frame from device")
            self._pump(0.25)

    def send_frame(self, payload):
        os.write(self.fd, enc_varint(len(payload)) + payload)

    def request(self, content_field, content=b"", collect_frames=None):
        """Send one command; read frames until has_next clears.

        Returns (status, list_of_parsed_frames). Unsolicited frames whose
        command_id differs are handed to collect_frames if given."""
        cid = self.next_id
        self.next_id += 1
        self.send_frame(build_main(cid, content_field, content))
        frames = []
        deadline = time.time() + 30
        while time.time() < deadline:
            frame = self._read_exact_frame(timeout=10)
            if frame.get(1, [None])[0] != cid:
                if collect_frames is not None:
                    collect_frames.append(frame)
                continue
            frames.append(frame)
            if not frame.get(3, [0])[0]:
                status = frame.get(2, [0])[0]
                return status, frames
        raise TimeoutError("response never completed")

    def enter_rpc(self):
        # CRITICAL: terminate with CR only, never CRLF. The CLI submits on
        # CR; a trailing LF (0x0A) is left at the head of the RPC stream,
        # where nanopb reads it as a 10-byte length prefix and the session
        # desyncs into silence/ERROR_DECODE. Found live, 2026-10-21.
        os.write(self.fd, b"\r")
        time.sleep(0.6)
        while self._pump(0.2):
            pass
        self.buf.clear()
        os.write(self.fd, b"start_rpc_session\r")
        time.sleep(1.0)
        # discard any CLI text preamble; no frames exist before we ask
        while self._pump(0.2):
            pass
        self.buf.clear()

    def stop_session(self):
        cid = self.next_id
        self.next_id += 1
        self.send_frame(build_main(cid, F_STOP_SESSION, b""))
        time.sleep(0.5)
        while self._pump(0.2):
            pass

    def close(self):
        try:
            os.close(self.fd)
        except OSError:
            pass


# ---- operations ----
def op_device_info(fl):
    status, frames = fl.request(F_DEVINFO_REQ)
    info = {}
    for frame in frames:
        resp = frame.get(F_DEVINFO_RESP, [b""])[0]
        kv = parse_fields(resp)
        key = kv.get(1, [b""])[0].decode(errors="replace")
        val = kv.get(2, [b""])[0].decode(errors="replace")
        if key:
            info[key] = val
    return status, info


def op_write_file(fl, data, dest_path):
    cid = fl.next_id
    fl.next_id += 1
    chunks = [data[i:i + 512] for i in range(0, len(data), 512)] or [b""]
    for idx, chunk in enumerate(chunks):
        last = idx == len(chunks) - 1
        file_msg = field_bytes(4, chunk)  # PB_Storage.File.data = 4
        write_req = field_bytes(1, dest_path.encode()) + field_bytes(2, file_msg)
        fl.send_frame(build_main(cid, F_STORAGE_WRITE, write_req, has_next=not last))
    frame = fl._read_exact_frame(timeout=20)
    if frame.get(1, [None])[0] != cid:
        raise RuntimeError(f"write response id mismatch: {frame.get(1)}")
    return frame.get(2, [0])[0]


def op_list(fl, path):
    req = field_bytes(1, path.encode())
    status, frames = fl.request(F_STORAGE_LIST_REQ, req)
    entries = []
    for frame in frames:
        resp = frame.get(F_STORAGE_LIST_RESP, [b""])[0]
        if not resp:
            continue
        for raw in parse_fields(resp).get(1, []):
            f = parse_fields(raw)
            entries.append({
                "type": f.get(1, [0])[0],
                "name": f.get(2, [b""])[0].decode(errors="replace"),
                "size": f.get(3, [0])[0],
            })
    return status, entries


def op_mkdir(fl, path):
    req = field_bytes(1, path.encode())
    status, _ = fl.request(F_STORAGE_MKDIR, req)
    return status


def op_md5(fl, path):
    req = field_bytes(1, path.encode())
    status, frames = fl.request(F_STORAGE_MD5_REQ, req)
    digest = ""
    for frame in frames:
        resp = frame.get(F_STORAGE_MD5_RESP, [b""])[0]
        if resp:
            digest = parse_fields(resp).get(1, [b""])[0].decode(errors="replace")
    return status, digest


def op_app_start(fl, name):
    req = field_bytes(1, name.encode())
    status, _ = fl.request(F_APP_START, req)
    return status


def op_send_input(fl, key, itype):
    req = field_varint(1, key) + field_varint(2, itype)
    status, _ = fl.request(F_GUI_INPUT, req)
    return status


# Input key/type constants (gui.proto / input.h)
KEY_OK, KEY_BACK = 4, 5
TYPE_PRESS, TYPE_RELEASE, TYPE_SHORT, TYPE_LONG = 0, 1, 2, 3


def op_tap(fl, key):
    """Inject a full tap: Press -> Short -> Release.

    The GUI's input path (applications/services/gui/gui.c, gui_input) drops
    any non-Press/Release event that is not part of a complementary
    Press...Release sequence ("non-complementary input, discarding").
    Hardware keys pass because the input service emits Press, Short, Release
    in order; a bare injected Short is discarded before it reaches any app.
    Verified live 2026-10-21: bare Short never exited athena_hello; the full
    sequence exits it immediately."""
    for itype in (TYPE_PRESS, TYPE_SHORT, TYPE_RELEASE):
        status = op_send_input(fl, key, itype)
        if status != 0:
            return status
        time.sleep(0.35)
    return 0


def op_stream_frames(fl, seconds):
    status, _ = fl.request(F_GUI_STREAM_START)
    frames = []
    deadline = time.time() + seconds
    while time.time() < deadline:
        try:
            frame = fl._read_exact_frame(timeout=1.5)
        except TimeoutError:
            break
        if F_GUI_FRAME in frame:
            data = parse_fields(frame[F_GUI_FRAME][0]).get(1, [b""])[0]
            if len(data) == 1024:
                frames.append((time.time(), data))
    fl.request(F_GUI_STREAM_STOP)
    return status, frames


def frame_to_png(data, path, scale=4):
    """Flipper framebuffer: 128x64, byte = x + (y//8)*128, bit y%8 (LSB top)."""
    w, h = 128, 64
    rows = []
    for y in range(h):
        row = bytearray()
        for x in range(w):
            byte = data[x + (y // 8) * w]
            on = (byte >> (y % 8)) & 1
            row += bytes([0 if on else 255]) * scale
        for _ in range(scale):
            rows.append(bytes(row))
    raw = b"".join(b"\x00" + r for r in rows)

    def chunk(typ, payload):
        c = struct.pack(">I", len(payload)) + typ + payload
        return c + struct.pack(">I", zlib.crc32(typ + payload) & 0xFFFFFFFF)

    png = b"\x89PNG\r\n\x1a\n"
    png += chunk(b"IHDR", struct.pack(">IIBBBBB", w * scale, h * scale, 8, 0, 0, 0, 0))
    png += chunk(b"IDAT", zlib.compress(raw))
    png += chunk(b"IEND", b"")
    with open(path, "wb") as fh:
        fh.write(png)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", default="/dev/ttyACM0")
    ap.add_argument("--fap", required=True, help="local .fap file to upload")
    ap.add_argument("--dest", default="/ext/apps/Tools/athena_hello.fap")
    ap.add_argument("--frames-dir", default="/home/athena/rpc-frames")
    args = ap.parse_args()

    with open(args.fap, "rb") as fh:
        fap_data = fh.read()
    local_md5 = hashlib.md5(fap_data).hexdigest()
    os.makedirs(args.frames_dir, exist_ok=True)

    fl = Flipper(args.port)
    result = {"local_md5": local_md5, "fap_size": len(fap_data)}
    try:
        fl.enter_rpc()

        status, info = op_device_info(fl)
        result["device_info_status"] = STATUS_NAMES.get(status, status)
        result["device"] = {k: info.get(k, "") for k in
                            ("hardware_name", "firmware_version", "firmware_commit")}

        dest_dir = args.dest.rsplit("/", 1)[0]
        result["mkdir_status"] = STATUS_NAMES.get(op_mkdir(fl, dest_dir),
                                                  "?")
        _, listing = op_list(fl, "/ext/apps")
        result["apps_dir"] = [e["name"] for e in listing]

        status = op_write_file(fl, fap_data, args.dest)
        result["write_status"] = STATUS_NAMES.get(status, status)

        status, remote_md5 = op_md5(fl, args.dest)
        result["md5_status"] = STATUS_NAMES.get(status, status)
        result["remote_md5"] = remote_md5
        result["md5_match"] = remote_md5.lower() == local_md5.lower()

        status = op_app_start(fl, args.dest)
        result["app_start_status"] = STATUS_NAMES.get(status, status)
        time.sleep(2.0)

        status, frames = op_stream_frames(fl, 3.0)
        result["stream_status"] = STATUS_NAMES.get(status, status)
        result["frames_captured"] = len(frames)
        saved = []
        seen = set()
        for ts, data in frames:
            digest = hashlib.md5(data).hexdigest()
            if digest in seen:
                continue
            seen.add(digest)
            out = os.path.join(args.frames_dir, f"frame_{len(saved)}.png")
            frame_to_png(data, out)
            saved.append(out)
        result["frames_saved"] = saved

        # Ask the app to close with a full tap (Press -> Short -> Release);
        # a bare Short is discarded by the GUI as non-complementary input.
        # Then VERIFY the exit: re-stream and confirm none of the app's
        # frames are still on screen. (Phase 4 assumed the exit; the app
        # was in fact still running an hour later. Never assume again.)
        result["back_status"] = STATUS_NAMES.get(op_tap(fl, KEY_BACK), "?")
        time.sleep(1.0)
        _, after = op_stream_frames(fl, 2.5)
        after_hashes = {hashlib.md5(d).hexdigest() for _, d in after}
        result["app_exited"] = not (after_hashes & seen)

        fl.stop_session()
    finally:
        fl.close()
    print(json.dumps(result, indent=2))


if __name__ == "__main__":
    main()
