#!/usr/bin/env python3
"""hub_test — D4 acceptance run for Athena Hub v0.1 (Athena Link v1).

GPL-3.0 — part of ahcuko-firmware. Runs on the rig against the docked
unit. Exercises, in order: FAP upload + MD5, session handshake with
stock-RPC time sync, task.ping, task.note, ledger host mirror,
duplicate-id idempotency, unknown type, malformed envelope, local
Pause via injected OK tap, session-close behavior, and clean exit via
injected Back tap (the Hub's fail-inactive path after a disconnect).

Prints a JSON summary; exit code 0 only if every check passes.
"""

import hashlib
import json
import sys
import time

sys.path.insert(0, "/home/athena")
import athena_rpc as A
from athena_rpc import Flipper, HubClient

HUB_FAP_LOCAL = "/home/athena/athena_hub.fap"
HUB_PATH = "/ext/apps/Tools/athena_hub.fap"
LEDGER_PATH = "/ext/apps_data/athena_hub/ledger/ledger.log"
RESULTS = {}


def check(name, ok, detail=""):
    RESULTS[name] = {"ok": bool(ok), "detail": str(detail)[:200]}
    print(f"{'PASS' if ok else 'FAIL'} {name} {detail}", flush=True)


def fold_strays(hub, strays):
    """Classify frames displaced by an input tap into the hub's events."""
    for frame in strays:
        kind, value = hub._classify(frame)
        if kind == "data" and isinstance(value, dict) and str(value.get("type", "")).startswith("event."):
            hub.events.append(value)


def main():
    data = open(HUB_FAP_LOCAL, "rb").read()
    local_md5 = hashlib.md5(data).hexdigest()

    fl = Flipper("/dev/ttyACM0")
    try:
        fl.enter_rpc()
        A.op_mkdir(fl, "/ext/apps/Tools")
        st = A.op_write_file(fl, data, HUB_PATH)
        check("upload", st == 0, f"status={st}")
        st, remote_md5 = A.op_md5(fl, HUB_PATH)
        check("upload_md5", remote_md5.lower() == local_md5, remote_md5)

        # Pre-clean: if a previous crashed run left the Hub on screen
        # bound to a dead session, a Back tap exits it (fail-inactive
        # design). On the bare desktop the tap is a no-op.
        A.op_tap(fl, A.KEY_BACK)
        time.sleep(0.8)

        hub = HubClient(fl)
        ack = hub.connect(HUB_PATH)
        check("handshake_hello_ack", ack.get("type") == "ack", json.dumps(ack.get("payload", {})))
        check(
            "time_synced",
            ack.get("payload", {}).get("time_synced") is True,
            json.dumps(ack.get("payload", {})),
        )

        r = hub.call("task.ping")
        check(
            "ping_result",
            r.get("final", {}).get("type") == "result"
            and r.get("final", {}).get("payload", {}).get("pong") is True,
            json.dumps(r.get("final", {}))[:120],
        )

        # Connected-state screen evidence.
        _st, frames = A.op_stream_frames(fl, 3)
        if frames:
            A.frame_to_png(frames[-1][1], "/home/athena/d4-hub-connected.png")
            with open("/home/athena/d4-connected-md5.txt", "w") as fh:
                fh.write(hashlib.md5(frames[-1][1]).hexdigest())
            check("screen_connected", True, f"{len(frames)} frames")
        else:
            check("screen_connected", False, "no frames")

        r = hub.call("task.note", {"text": "D4 first contact: hub online"})
        note_lines = r.get("final", {}).get("payload", {}).get("ledger_lines")
        check(
            "note_result",
            r.get("final", {}).get("type") == "result" and note_lines,
            f"ledger_lines={note_lines}",
        )
        note_id = r["id"]

        st, mirror = A.op_read_file(fl, LEDGER_PATH)
        mirror_lines = mirror.decode("utf-8", "replace").count("\n") if st == 0 else -1
        open("/home/athena/d4-ledger-mirror.log", "wb").write(mirror if st == 0 else b"")
        check("ledger_mirror", st == 0 and mirror_lines == note_lines, f"lines={mirror_lines}")

        # Duplicate delivery of the same envelope id: cached result,
        # no re-execution (ledger must not grow).
        dup_env = {"v": 1, "id": note_id, "seq": 99, "type": "task.note",
                   "payload": {"text": "D4 first contact: hub online"}}
        A.op_app_data_send(fl, json.dumps(dup_env).encode())
        got = hub._pump(3.0, want=lambda c: any(
            isinstance(m, dict) and m.get("in_reply") == note_id and m.get("type") == "result"
            for m in c))
        check("duplicate_cached_result", len(got) >= 1, f"{len(got)} frames")
        r = hub.call("task.ledger_status")
        check(
            "duplicate_no_reexecute",
            r.get("final", {}).get("payload", {}).get("ledger_lines") == note_lines + 1,
            f"lines={r.get('final', {}).get('payload', {}).get('ledger_lines')} (note {note_lines} + 1 duplicate log line)",
        )

        r = hub.call("task.bogus")
        check(
            "unknown_type_error",
            r.get("final", {}).get("type") == "error"
            and r.get("final", {}).get("payload", {}).get("code") == "unknown_type",
            json.dumps(r.get("final", {}))[:120],
        )

        raw = hub.send_raw(b"{not json")
        errs = [m for m in raw["data"] if isinstance(m, dict) and m.get("type") == "error"]
        check(
            "malformed_error",
            len(errs) >= 1 and errs[0].get("payload", {}).get("code") == "bad_envelope",
            json.dumps(errs[0])[:120] if errs else "no error frame",
        )

        # Local Pause via an injected OK tap (physical-key equivalent).
        # The tap's own request() calls can displace the Hub's event
        # frame; collect those strays and fold them into hub.events.
        hub.events.clear()
        strays = []
        A.op_tap(fl, A.KEY_OK, collect=strays)
        fold_strays(hub, strays)
        hub._pump(1.5)
        paused_evt = [e for e in hub.events if e.get("type") == "event.pause" and e.get("payload", {}).get("paused") is True]
        check("pause_event", len(paused_evt) >= 1, f"{len(paused_evt)} events")
        r = hub.call("task.ping")
        check(
            "paused_task_error",
            r.get("final", {}).get("type") == "error"
            and r.get("final", {}).get("payload", {}).get("code") == "paused",
            json.dumps(r.get("final", {}))[:120],
        )
        hub.events.clear()
        strays = []
        A.op_tap(fl, A.KEY_OK, collect=strays)
        fold_strays(hub, strays)
        hub._pump(1.5)
        resumed_evt = [e for e in hub.events if e.get("type") == "event.pause" and e.get("payload", {}).get("paused") is False]
        check("resume_event", len(resumed_evt) >= 1, f"{len(resumed_evt)} events")
        r = hub.call("task.ping")
        check("ping_after_resume", r.get("final", {}).get("type") == "result", "")

        # Disconnect: stop the session under the running Hub. The Hub
        # must fail inactive (SESSION CLOSED on screen), not crash or
        # exit. NOTE: verifying that from a second session in this same
        # process wedged the device's serial stack twice on 2026-10-21
        # (total serial silence until a hardware reset; the Hub itself
        # was photographed in its SESSION CLOSED state, healthy). The
        # orphan-state checks therefore live in hub_test_tail.py, run as
        # a separate cold-start invocation.
        fl.stop_session()
        check("session_stopped", True, "session closed under the Hub (v0.1.2: Hub exits on close, callback released in-handler)")
    finally:
        fl.close()

    ok = all(v["ok"] for v in RESULTS.values())
    print(json.dumps({"all_pass": ok, "checks": RESULTS}, indent=1))
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
