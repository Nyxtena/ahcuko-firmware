// Athena Hub v0.1 — the Athena Link v1 device-side endpoint.
//
// GPL-3.0 — part of ahcuko-firmware (see NOTICE.md at the repo root).
// Written by Athena, 2026-10-21. Built with uFBT against the official
// firmware 1.4.3 SDK (API 87.1).
//
// Design: docs/fork-design-v0.1-2026-10-21.md §5.2 (Hub), §5.3 (Link
// contract), §5.4 (handshake/time), §5.5 (ledger). Roadmap stage D4.
//
// The Hub is a normal foreground FAP. Launched over RPC (App Start), it
// receives the RpcAppSystem handle in its args ("RPC %08lX", injected by
// the device-side RPC app system) and exchanges Athena Link v1 JSON
// envelopes via App Data Exchange. Launched by hand it simply shows a
// NO SESSION screen and exits on Back.
//
// v0.1 task set: hello (session handshake), task.ping, task.note,
// task.ledger_status. Unknown types return an error; malformed
// envelopes return an error; duplicate task ids return the cached
// reply without re-executing (idempotency). A local Pause (OK button)
// makes task.* fail with a "paused" error until resumed.

#include <furi.h>
#include <furi_hal.h>
#include <gui/gui.h>
#include <gui/view_port.h>
#include <input/input.h>
#include <storage/storage.h>
#include <applications/services/rpc/rpc_app.h>

#define TAG "AthenaHub"

#define HUB_DATA_DIR EXT_PATH("apps_data/athena_hub")
#define HUB_LEDGER_DIR HUB_DATA_DIR "/ledger"
#define HUB_TASKS_DIR HUB_DATA_DIR "/tasks"
#define HUB_EXPORTS_DIR HUB_DATA_DIR "/exports"
#define HUB_LEDGER_FILE HUB_LEDGER_DIR "/ledger.log"

#define ENVELOPE_MAX 896
#define ID_MAX 40
#define TYPE_MAX 40
#define TEXT_MAX 96
#define SEEN_RING 8

typedef struct {
    uint8_t data[ENVELOPE_MAX + 1];
    size_t size;
} Envelope;

typedef struct {
    char id[ID_MAX];
    char reply[ENVELOPE_MAX + 1];
} SeenEntry;

typedef struct {
    ViewPort* view_port;
    FuriMessageQueue* keys;
    FuriMessageQueue* inbox; // Envelope* items, allocated by the RPC callback
    Storage* storage;

    RpcAppSystem* rpc;
    bool connected;
    bool rpc_dead;
    bool exit_requested;
    bool paused;
    bool time_synced;
    bool hello_done;

    char session[ID_MAX];
    uint32_t rx_count;
    uint32_t tx_count;
    int32_t last_seq;

    char last_task[TYPE_MAX];
    char last_line[64];
    uint32_t ledger_lines;

    SeenEntry seen[SEEN_RING];
    size_t seen_next;
} AthenaHub;

// ---------------------------------------------------------------- ledger

static void hub_format_ts(char* out, size_t out_size, AthenaHub* app) {
    DateTime dt;
    furi_hal_rtc_get_datetime(&dt);
    if(app->time_synced && dt.year >= 2025) {
        snprintf(
            out,
            out_size,
            "%04d-%02d-%02d %02d:%02d:%02d",
            dt.year,
            dt.month,
            dt.day,
            dt.hour,
            dt.minute,
            dt.second);
    } else {
        snprintf(out, out_size, "UNSYNCED");
    }
}

static void hub_ledger_append(AthenaHub* app, const char* event, const char* detail) {
    File* file = storage_file_alloc(app->storage);
    if(storage_file_open(file, HUB_LEDGER_FILE, FSAM_WRITE, FSOM_OPEN_APPEND)) {
        char ts[32];
        hub_format_ts(ts, sizeof(ts), app);
        char line[256];
        int n = snprintf(
            line,
            sizeof(line),
            "ts=%s session=%s rx=%lu event=%s %s\n",
            ts,
            app->session[0] ? app->session : "-",
            (unsigned long)app->rx_count,
            event,
            detail ? detail : "");
        if(n > 0) {
            storage_file_write(file, line, MIN((size_t)n, sizeof(line) - 1));
        }
        storage_file_close(file);
        app->ledger_lines++;
    }
    storage_file_free(file);
}

static uint32_t hub_ledger_count_existing(AthenaHub* app) {
    uint32_t lines = 0;
    File* file = storage_file_alloc(app->storage);
    if(storage_file_open(file, HUB_LEDGER_FILE, FSAM_READ, FSOM_OPEN_EXISTING)) {
        char buf[128];
        size_t got;
        while((got = storage_file_read(file, buf, sizeof(buf))) > 0) {
            for(size_t i = 0; i < got; i++) {
                if(buf[i] == '\n') lines++;
            }
        }
        storage_file_close(file);
    }
    storage_file_free(file);
    return lines;
}

// ------------------------------------------------------- minimal JSON in

// Find "\"key\"" after `start`; return pointer to the ':' or NULL.
static const char* json_find_key(const char* msg, const char* start, const char* key) {
    char pat[48];
    snprintf(pat, sizeof(pat), "\"%s\"", key);
    const char* p = strstr(start ? start : msg, pat);
    if(!p) return NULL;
    p += strlen(pat);
    while(*p == ' ' || *p == '\t') p++;
    if(*p != ':') return NULL;
    return p;
}

static bool json_get_str(const char* msg, const char* start, const char* key, char* out, size_t out_size) {
    const char* colon = json_find_key(msg, start, key);
    if(!colon) return false;
    const char* p = colon + 1;
    while(*p == ' ' || *p == '\t') p++;
    if(*p != '"') return false;
    p++;
    size_t i = 0;
    while(*p && *p != '"') {
        char c = *p;
        if(c == '\\' && *(p + 1)) {
            p++;
            c = *p;
        }
        if(i + 1 < out_size) out[i++] = c;
        p++;
    }
    out[i] = '\0';
    return true;
}

static bool json_get_int(const char* msg, const char* start, const char* key, int32_t* out) {
    const char* colon = json_find_key(msg, start, key);
    if(!colon) return false;
    const char* p = colon + 1;
    while(*p == ' ' || *p == '\t') p++;
    char* end = NULL;
    long v = strtol(p, &end, 10);
    if(end == p) return false;
    *out = (int32_t)v;
    return true;
}

// Locate the payload object (flat in v0.1); returns pointer to its '{'.
static const char* json_payload(const char* msg) {
    const char* colon = json_find_key(msg, NULL, "payload");
    if(!colon) return NULL;
    const char* p = colon + 1;
    while(*p == ' ' || *p == '\t') p++;
    return (*p == '{') ? p : NULL;
}

// -------------------------------------------------------------- replies

static void hub_send(AthenaHub* app, const char* json) {
    if(app->rpc && !app->rpc_dead) {
        rpc_system_app_exchange_data(app->rpc, (const uint8_t*)json, strlen(json));
        app->tx_count++;
    }
}

static void hub_reply(
    AthenaHub* app,
    const char* in_reply,
    const char* type,
    const char* payload_json) {
    char out[ENVELOPE_MAX + 1];
    snprintf(
        out,
        sizeof(out),
        "{\"v\":1,\"id\":\"hub-%lu\",\"in_reply\":\"%s\",\"seq\":%lu,\"type\":\"%s\",\"payload\":%s}",
        (unsigned long)app->tx_count,
        in_reply,
        (unsigned long)app->tx_count,
        type,
        payload_json);
    hub_send(app, out);
    // Cache for idempotent redelivery: one slot per id, holding the
    // LATEST reply (a task's final result supersedes its ack).
    SeenEntry* slot = NULL;
    for(size_t i = 0; i < SEEN_RING; i++) {
        if(app->seen[i].id[0] && strcmp(app->seen[i].id, in_reply) == 0) {
            slot = &app->seen[i];
            break;
        }
    }
    if(!slot) {
        slot = &app->seen[app->seen_next];
        app->seen_next = (app->seen_next + 1) % SEEN_RING;
    }
    strlcpy(slot->id, in_reply, sizeof(slot->id));
    strlcpy(slot->reply, out, sizeof(slot->reply));
}

static void hub_error(AthenaHub* app, const char* in_reply, const char* code, const char* detail) {
    char payload[160];
    snprintf(payload, sizeof(payload), "{\"code\":\"%s\",\"detail\":\"%s\"}", code, detail);
    hub_reply(app, in_reply, "error", payload);
    char det[96];
    snprintf(det, sizeof(det), "code=%s", code);
    hub_ledger_append(app, "error", det);
    snprintf(app->last_line, sizeof(app->last_line), "error: %s", code);
}

// --------------------------------------------------------- task handling

static void hub_process(AthenaHub* app, const char* msg) {
    char id[ID_MAX] = "";
    char type[TYPE_MAX] = "";
    int32_t v = 0;
    int32_t seq = -1;

    bool has_id = json_get_str(msg, NULL, "id", id, sizeof(id));
    bool has_type = json_get_str(msg, NULL, "type", type, sizeof(type));
    json_get_int(msg, NULL, "v", &v);
    json_get_int(msg, NULL, "seq", &seq);

    if(!has_id || !has_type || v != 1) {
        hub_error(app, has_id ? id : "?", "bad_envelope", "need v=1, id, type");
        return;
    }
    app->last_seq = seq;

    // Idempotency: a re-delivered id gets the cached reply, no re-execution.
    for(size_t i = 0; i < SEEN_RING; i++) {
        if(app->seen[i].id[0] && strcmp(app->seen[i].id, id) == 0) {
            hub_send(app, app->seen[i].reply);
            hub_ledger_append(app, "duplicate", type);
            snprintf(app->last_line, sizeof(app->last_line), "duplicate %s ignored", type);
            return;
        }
    }

    strlcpy(app->last_task, type, sizeof(app->last_task));

    if(strcmp(type, "hello") == 0) {
        const char* payload = json_payload(msg);
        char session[ID_MAX] = "";
        if(payload) json_get_str(msg, payload, "session", session, sizeof(session));
        if(session[0]) strlcpy(app->session, session, sizeof(app->session));
        DateTime dt;
        furi_hal_rtc_get_datetime(&dt);
        app->time_synced = (dt.year >= 2025);
        app->hello_done = true;
        hub_ledger_append(app, "hello", app->session);
        char payload_out[128];
        snprintf(
            payload_out,
            sizeof(payload_out),
            "{\"hub\":\"athena_hub\",\"hub_version\":\"0.1.2\",\"time_synced\":%s}",
            app->time_synced ? "true" : "false");
        hub_reply(app, id, "ack", payload_out);
        snprintf(app->last_line, sizeof(app->last_line), "hello, session %s", app->session);
        return;
    }

    if(app->paused && strncmp(type, "task.", 5) == 0) {
        hub_error(app, id, "paused", "hub is paused locally");
        return;
    }

    if(strcmp(type, "task.ping") == 0) {
        hub_reply(app, id, "ack", "{}");
        char payload_out[128];
        snprintf(
            payload_out,
            sizeof(payload_out),
            "{\"pong\":true,\"uptime_ms\":%lu,\"ledger_lines\":%lu}",
            (unsigned long)furi_get_tick(),
            (unsigned long)app->ledger_lines);
        hub_reply(app, id, "result", payload_out);
        hub_ledger_append(app, "task.ping", "result ok");
        snprintf(app->last_line, sizeof(app->last_line), "ping -> pong");
        return;
    }

    if(strcmp(type, "task.note") == 0) {
        const char* payload = json_payload(msg);
        char text[TEXT_MAX] = "";
        if(payload) json_get_str(msg, payload, "text", text, sizeof(text));
        // Sanitize for the one-line ledger: no newlines, no quotes.
        for(size_t i = 0; text[i]; i++) {
            if(text[i] == '\n' || text[i] == '\r' || text[i] == '"') text[i] = ' ';
        }
        hub_reply(app, id, "ack", "{}");
        char det[128];
        snprintf(det, sizeof(det), "note %s", text);
        hub_ledger_append(app, "task.note", det);
        char payload_out[96];
        snprintf(
            payload_out,
            sizeof(payload_out),
            "{\"recorded\":true,\"ledger_lines\":%lu}",
            (unsigned long)app->ledger_lines);
        hub_reply(app, id, "result", payload_out);
        snprintf(app->last_line, sizeof(app->last_line), "note recorded");
        return;
    }

    if(strcmp(type, "task.ledger_status") == 0) {
        hub_reply(app, id, "ack", "{}");
        char payload_out[96];
        snprintf(
            payload_out,
            sizeof(payload_out),
            "{\"ledger_lines\":%lu,\"paused\":%s}",
            (unsigned long)app->ledger_lines,
            app->paused ? "true" : "false");
        hub_reply(app, id, "result", payload_out);
        snprintf(app->last_line, sizeof(app->last_line), "ledger status sent");
        return;
    }

    hub_error(app, id, "unknown_type", type);
}

// -------------------------------------------------------------- RPC in

static void hub_rpc_callback(const RpcAppSystemEvent* event, void* context) {
    AthenaHub* app = context;
    if(event->type == RpcAppEventTypeSessionClose) {
        // v0.1.2: unregister the callback FIRST, from inside this
        // handler, exactly like the stock rpc_debug_app. Reason (found
        // in rpc_app.c, rpc_system_app_free): session teardown invokes
        // this callback and then SPINS on `while(rpc_app->callback)`
        // until the app unregisters. v0.1.0/v0.1.1 deferred the
        // unregistration to the exit path, whose rpc_dead guard then
        // skipped it — the teardown thread spun forever, the serial
        // never returned to the CLI, and only a hardware reset cleared
        // it (four wedges, 2026-10-21). The close is still recorded in
        // the ledger by the main loop before the exit completes.
        rpc_system_app_set_callback(app->rpc, NULL, NULL);
        app->rpc = NULL;
        app->rpc_dead = true;
        app->exit_requested = true;
    } else if(event->type == RpcAppEventTypeAppExit) {
        rpc_system_app_confirm(app->rpc, true);
        app->exit_requested = true;
    } else if(event->type == RpcAppEventTypeDataExchange) {
        Envelope* env = malloc(sizeof(Envelope));
        size_t n = event->data.bytes.size;
        if(n > ENVELOPE_MAX) n = ENVELOPE_MAX;
        memcpy(env->data, event->data.bytes.ptr, n);
        env->data[n] = '\0';
        env->size = n;
        if(furi_message_queue_put(app->inbox, &env, 0) == FuriStatusOk) {
            rpc_system_app_confirm(app->rpc, true);
        } else {
            free(env);
            rpc_system_app_confirm(app->rpc, false);
        }
    } else {
        rpc_system_app_confirm(app->rpc, false);
    }
}

// ------------------------------------------------------------------ UI

static void hub_draw(Canvas* canvas, void* ctx) {
    AthenaHub* app = ctx;
    canvas_clear(canvas);
    canvas_draw_frame(canvas, 0, 0, 128, 64);

    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 4, 10, "ATHENA HUB");
    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str(canvas, 88, 10, "v0.1.2");

    char line[64];
    snprintf(
        line,
        sizeof(line),
        "Link: %s",
        app->rpc_dead ? "SESSION CLOSED" : (app->connected ? "CONNECTED" : "NO SESSION"));
    canvas_draw_str(canvas, 4, 21, line);

    DateTime dt;
    furi_hal_rtc_get_datetime(&dt);
    if(app->time_synced && dt.year >= 2025) {
        snprintf(
            line,
            sizeof(line),
            "Time: %04d-%02d-%02d %02d:%02d SYNCED",
            dt.year,
            dt.month,
            dt.day,
            dt.hour,
            dt.minute);
    } else {
        snprintf(line, sizeof(line), "Time: UNSYNCED (yr %d)", dt.year);
    }
    canvas_draw_str(canvas, 4, 31, line);

    snprintf(
        line, sizeof(line), "Task: %s", app->last_task[0] ? app->last_task : "idle");
    canvas_draw_str(canvas, 4, 41, line);

    snprintf(line, sizeof(line), "Ledger: %lu lines", (unsigned long)app->ledger_lines);
    canvas_draw_str(canvas, 4, 51, line);

    if(app->paused) {
        canvas_draw_str(canvas, 76, 51, "[PAUSED]");
    }
    canvas_draw_str(canvas, 4, 61, app->last_line);
}

static void hub_input(InputEvent* event, void* ctx) {
    AthenaHub* app = ctx;
    furi_message_queue_put(app->keys, event, FuriWaitForever);
}

// ----------------------------------------------------------------- main

int32_t athena_hub_main(void* args) {
    AthenaHub* app = malloc(sizeof(AthenaHub));
    memset(app, 0, sizeof(AthenaHub));
    app->last_seq = -1;
    strlcpy(app->last_line, "OK: Pause   Back: Exit", sizeof(app->last_line));

    app->keys = furi_message_queue_alloc(8, sizeof(InputEvent));
    app->inbox = furi_message_queue_alloc(4, sizeof(Envelope*));
    app->storage = furi_record_open(RECORD_STORAGE);

    storage_common_mkdir(app->storage, HUB_DATA_DIR);
    storage_common_mkdir(app->storage, HUB_LEDGER_DIR);
    storage_common_mkdir(app->storage, HUB_TASKS_DIR);
    storage_common_mkdir(app->storage, HUB_EXPORTS_DIR);
    app->ledger_lines = hub_ledger_count_existing(app);

    // RPC handle: injected by the device RPC app system when the host
    // starts this app via App Start ("RPC %08lX"). Absent on hand launch.
    if(args && strlen((const char*)args)) {
        uint32_t rpc = 0;
        if(sscanf((const char*)args, "RPC %lX", &rpc) == 1 && rpc) {
            app->rpc = (RpcAppSystem*)rpc;
            rpc_system_app_set_callback(app->rpc, hub_rpc_callback, app);
            rpc_system_app_send_started(app->rpc);
            app->connected = true;
        }
    }

    Gui* gui = furi_record_open(RECORD_GUI);
    app->view_port = view_port_alloc();
    view_port_draw_callback_set(app->view_port, hub_draw, app);
    view_port_input_callback_set(app->view_port, hub_input, app);
    gui_add_view_port(gui, app->view_port, GuiLayerFullscreen);

    hub_ledger_append(app, "boot", app->connected ? "rpc" : "local");

    InputEvent key;
    while(!app->exit_requested) {
        // Drain RPC inbox first.
        Envelope* env = NULL;
        while(furi_message_queue_get(app->inbox, &env, 0) == FuriStatusOk) {
            app->rx_count++;
            hub_process(app, (const char*)env->data);
            free(env);
            env = NULL;
        }
        if(app->rpc_dead && app->connected) {
            app->connected = false;
            hub_ledger_append(app, "session_close", "");
            snprintf(app->last_line, sizeof(app->last_line), "host session closed");
        }
        if(furi_message_queue_get(app->keys, &key, 100) == FuriStatusOk) {
            if(key.type == InputTypeShort) {
                if(key.key == InputKeyBack) break;
                if(key.key == InputKeyOk) {
                    app->paused = !app->paused;
                    hub_ledger_append(app, app->paused ? "pause" : "resume", "local");
                    if(app->connected && !app->rpc_dead) {
                        char evt[128];
                        snprintf(
                            evt,
                            sizeof(evt),
                            "{\"v\":1,\"id\":\"evt-%lu\",\"type\":\"event.pause\",\"payload\":{\"paused\":%s}}",
                            (unsigned long)app->tx_count,
                            app->paused ? "true" : "false");
                        hub_send(app, evt);
                    }
                    snprintf(
                        app->last_line,
                        sizeof(app->last_line),
                        app->paused ? "PAUSED locally" : "resumed");
                }
            }
        }
        view_port_update(app->view_port);
    }

    hub_ledger_append(app, "exit", "");

    if(app->rpc && !app->rpc_dead) {
        rpc_system_app_set_callback(app->rpc, NULL, NULL);
        rpc_system_app_send_exited(app->rpc);
    }

    gui_remove_view_port(gui, app->view_port);
    view_port_free(app->view_port);
    furi_record_close(RECORD_GUI);
    furi_record_close(RECORD_STORAGE);
    furi_message_queue_free(app->keys);
    furi_message_queue_free(app->inbox);
    free(app);
    return 0;
}
