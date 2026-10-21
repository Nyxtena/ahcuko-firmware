// Athena Scanner v0.1 — the scanner app, built to Jacob's direction
// (2026-10-21, ratified in his words):
//
//   If Athena asked Jacob to scan something, or Jacob told Athena he
//   was going to scan something he wanted her to look at, Athena
//   activates the scanner, Jacob scans it, and the data is stored,
//   and also pushed back to Athena.
//
//   If Jacob scans something without either of those conditions, he
//   gets the same readout — and on the readout, he can hit the Up
//   button and send it to Athena instantly, and the items in the More
//   menu also include Send to Athena, at the top.
//
// GPL-3.0 — part of ahcuko-firmware (see NOTICE.md at the repo root).
// Written by Athena, 2026-10-21. Built with uFBT against the official
// firmware 1.4.3 SDK (API 87.1). RPC plumbing and the SessionClose
// discipline are carried over from Athena Hub v0.1.2 (unregister the
// callback FIRST inside the close handler — see rpc_app.c).

#include <furi.h>
#include <furi_hal.h>
#include <gui/gui.h>
#include <gui/view_port.h>
#include <input/input.h>
#include <storage/storage.h>
#include <applications/services/rpc/rpc_app.h>
#include <nfc/nfc.h>
#include <nfc/nfc_scanner.h>
#include <nfc/nfc_poller.h>
#include <nfc/protocols/nfc_protocol.h>
#include <nfc/protocols/iso14443_3a/iso14443_3a.h>
#include <nfc/protocols/iso14443_3a/iso14443_3a_poller.h>
#include <nfc/protocols/iso14443_4a/iso14443_4a.h>
#include <nfc/protocols/iso14443_4a/iso14443_4a_poller.h>
#include <nfc/protocols/mf_classic/mf_classic.h>
#include <nfc/protocols/mf_ultralight/mf_ultralight.h>
#include <nfc/protocols/mf_desfire/mf_desfire.h>

#define TAG "AthenaScanner"

#define SCAN_DATA_DIR EXT_PATH("apps_data/athena_scanner")
#define SCAN_RECORDS_FILE SCAN_DATA_DIR "/records.jsonl"

#define ENVELOPE_MAX 896
#define ID_MAX 40
#define TYPE_MAX 40
#define SEEN_RING 8

typedef struct {
    uint8_t data[ENVELOPE_MAX + 1];
    size_t size;
} Envelope;

typedef struct {
    char id[ID_MAX];
    char reply[ENVELOPE_MAX + 1];
} SeenEntry;

typedef enum {
    ScanViewScanning,
    ScanViewReadout,
    ScanViewMore,
    ScanViewDetails,
} ScanView;

typedef struct {
    uint32_t id;
    NfcProtocol protocol;
    char proto[20];
    char name[28];
    char tech[24];
    char uid[40]; // spaced hex, "" when the protocol exposes none here
    uint8_t atqa[2];
    bool has_atqa;
    uint8_t sak;
    bool has_sak;
    bool send_marked;
    bool pushed;
} ScanRecord;

typedef struct {
    ViewPort* view_port;
    FuriMessageQueue* keys;
    FuriMessageQueue* inbox;
    Storage* storage;

    RpcAppSystem* rpc;
    bool connected;
    bool rpc_dead;
    bool exit_requested;
    bool time_synced;

    char session[ID_MAX];
    uint32_t rx_count;
    uint32_t tx_count;
    SeenEntry seen[SEEN_RING];
    size_t seen_next;

    Nfc* nfc;
    NfcScanner* scanner;
    NfcPoller* poller;
    bool poller_active;
    volatile bool detected;
    NfcProtocol detected_protocols[4];
    size_t detected_num;
    volatile bool poll_done;

    ScanView view;
    size_t more_sel;
    uint32_t records_count;
    ScanRecord current;
    bool have_current;
    char status[64];
} AthenaScanner;

// ------------------------------------------------------- minimal JSON in
// (carried from Athena Hub)

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

static const char* json_payload(const char* msg) {
    const char* colon = json_find_key(msg, NULL, "payload");
    if(!colon) return NULL;
    const char* p = colon + 1;
    while(*p == ' ' || *p == '\t') p++;
    return (*p == '{') ? p : NULL;
}

// ------------------------------------------------------------ store/push

static void scan_store_line(AthenaScanner* app, const char* line) {
    File* file = storage_file_alloc(app->storage);
    if(storage_file_open(file, SCAN_RECORDS_FILE, FSAM_WRITE, FSOM_OPEN_APPEND)) {
        storage_file_write(file, line, strlen(line));
        storage_file_close(file);
    }
    storage_file_free(file);
}

static void scan_record_json(const ScanRecord* rec, char* out, size_t out_size) {
    char ts[32];
    DateTime dt;
    furi_hal_rtc_get_datetime(&dt);
    if(dt.year >= 2025) {
        snprintf(
            ts, sizeof(ts), "%04d-%02d-%02d %02d:%02d:%02d",
            dt.year, dt.month, dt.day, dt.hour, dt.minute, dt.second);
    } else {
        snprintf(ts, sizeof(ts), "UNSYNCED");
    }
    char atqa[8] = "";
    if(rec->has_atqa) snprintf(atqa, sizeof(atqa), "%02X %02X", rec->atqa[0], rec->atqa[1]);
    char sak[4] = "";
    if(rec->has_sak) snprintf(sak, sizeof(sak), "%02X", rec->sak);
    snprintf(
        out, out_size,
        "{\"id\":%lu,\"ts\":\"%s\",\"proto\":\"%s\",\"name\":\"%s\",\"tech\":\"%s\","
        "\"uid\":\"%s\",\"atqa\":\"%s\",\"sak\":\"%s\",\"send\":%s,\"pushed\":%s}",
        (unsigned long)rec->id, ts, rec->proto, rec->name, rec->tech, rec->uid, atqa, sak,
        rec->send_marked ? "true" : "false", rec->pushed ? "true" : "false");
}

static void scan_send(AthenaScanner* app, const char* json) {
    if(app->rpc && !app->rpc_dead) {
        rpc_system_app_exchange_data(app->rpc, (const uint8_t*)json, strlen(json));
        app->tx_count++;
    }
}

static void scan_reply(AthenaScanner* app, const char* in_reply, const char* type, const char* payload_json) {
    char out[ENVELOPE_MAX + 1];
    snprintf(
        out, sizeof(out),
        "{\"v\":1,\"id\":\"scan-%lu\",\"in_reply\":\"%s\",\"seq\":%lu,\"type\":\"%s\",\"payload\":%s}",
        (unsigned long)app->tx_count, in_reply, (unsigned long)app->tx_count, type, payload_json);
    scan_send(app, out);
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

static void scan_error(AthenaScanner* app, const char* in_reply, const char* code, const char* detail) {
    char payload[160];
    snprintf(payload, sizeof(payload), "{\"code\":\"%s\",\"detail\":\"%s\"}", code, detail);
    scan_reply(app, in_reply, "error", payload);
}

// Push the current record to Athena over the live Link session.
// Returns true when the record is with Athena (pushed now).
static bool scan_push_current(AthenaScanner* app) {
    if(!app->have_current) return false;
    if(app->current.pushed) return true;
    if(!app->rpc || app->rpc_dead) return false;
    char rec_json[448];
    ScanRecord* rec = &app->current;
    scan_record_json(rec, rec_json, sizeof(rec_json));
    char env[ENVELOPE_MAX + 1];
    snprintf(
        env, sizeof(env),
        "{\"v\":1,\"id\":\"scan-rec-%lu\",\"type\":\"scan.record\",\"payload\":%s}",
        (unsigned long)rec->id, rec_json);
    scan_send(app, env);
    rec->pushed = true;
    char marker[96];
    snprintf(marker, sizeof(marker), "{\"id\":%lu,\"event\":\"pushed\"}\n", (unsigned long)rec->id);
    scan_store_line(app, marker);
    return true;
}

// The send action behind the readout's Up button and More's top item.
static void scan_send_to_athena(AthenaScanner* app) {
    if(!app->have_current) return;
    if(scan_push_current(app)) {
        snprintf(app->status, sizeof(app->status), "Sent to Athena");
        return;
    }
    if(!app->current.send_marked) {
        app->current.send_marked = true;
        char marker[96];
        snprintf(
            marker, sizeof(marker), "{\"id\":%lu,\"event\":\"send_marked\"}\n",
            (unsigned long)app->current.id);
        scan_store_line(app, marker);
    }
    snprintf(app->status, sizeof(app->status), "Marked for Athena");
}

// -------------------------------------------------------------- NFC core

static const char* scan_proto_key(NfcProtocol proto) {
    switch(proto) {
    case NfcProtocolIso14443_3a: return "iso14443_3a";
    case NfcProtocolIso14443_3b: return "iso14443_3b";
    case NfcProtocolIso14443_4a: return "iso14443_4a";
    case NfcProtocolIso14443_4b: return "iso14443_4b";
    case NfcProtocolIso15693_3: return "iso15693_3";
    case NfcProtocolFelica: return "felica";
    case NfcProtocolMfClassic: return "mf_classic";
    case NfcProtocolMfUltralight: return "mf_ultralight";
    case NfcProtocolMfPlus: return "mf_plus";
    case NfcProtocolMfDesfire: return "mf_desfire";
    case NfcProtocolSlix: return "slix";
    case NfcProtocolSt25tb: return "st25tb";
    default: return "unknown";
    }
}

static const char* scan_proto_name(NfcProtocol proto) {
    switch(proto) {
    case NfcProtocolIso14443_3a: return "ISO14443-3A";
    case NfcProtocolIso14443_3b: return "ISO14443-3B";
    case NfcProtocolIso14443_4a: return "ISO14443-4A";
    case NfcProtocolIso14443_4b: return "ISO14443-4B";
    case NfcProtocolIso15693_3: return "ISO15693-3";
    case NfcProtocolFelica: return "FeliCa";
    case NfcProtocolMfClassic: return "MIFARE Classic";
    case NfcProtocolMfUltralight: return "MIFARE Ultralight";
    case NfcProtocolMfPlus: return "MIFARE Plus";
    case NfcProtocolMfDesfire: return "MIFARE DESFire";
    case NfcProtocolSlix: return "Slix";
    case NfcProtocolSt25tb: return "ST25TB";
    default: return "Unknown NFC";
    }
}

static const char* scan_proto_tech(NfcProtocol proto) {
    switch(proto) {
    case NfcProtocolIso14443_3a:
    case NfcProtocolIso14443_4a:
    case NfcProtocolMfClassic:
    case NfcProtocolMfUltralight:
    case NfcProtocolMfPlus:
    case NfcProtocolMfDesfire: return "NFC-A";
    case NfcProtocolIso14443_3b:
    case NfcProtocolIso14443_4b: return "NFC-B";
    case NfcProtocolFelica: return "NFC-F";
    case NfcProtocolIso15693_3:
    case NfcProtocolSlix:
    case NfcProtocolSt25tb: return "NFC-V";
    default: return "";
    }
}

static void scan_scanner_callback(NfcScannerEvent event, void* context) {
    AthenaScanner* app = context;
    if(event.type == NfcScannerEventTypeDetected) {
        size_t n = event.data.protocol_num;
        if(n > 4) n = 4;
        for(size_t i = 0; i < n; i++) app->detected_protocols[i] = event.data.protocols[i];
        app->detected_num = n;
        app->detected = true;
    }
}

static NfcCommand scan_poller_callback(NfcGenericEvent event, void* context) {
    AthenaScanner* app = context;
    (void)event;
    app->poll_done = true;
    return NfcCommandStop;
}

static void scan_start_scanner(AthenaScanner* app) {
    if(app->scanner || app->poller_active) return;
    app->detected = false;
    app->detected_num = 0;
    app->scanner = nfc_scanner_alloc(app->nfc);
    nfc_scanner_start(app->scanner, scan_scanner_callback, app);
}

static void scan_stop_scanner(AthenaScanner* app) {
    if(app->scanner) {
        nfc_scanner_stop(app->scanner);
        nfc_scanner_free(app->scanner);
        app->scanner = NULL;
    }
}

static void scan_stop_poller(AthenaScanner* app) {
    if(app->poller) {
        nfc_poller_stop(app->poller);
        nfc_poller_free(app->poller);
        app->poller = NULL;
    }
    app->poller_active = false;
}

static NfcProtocol scan_choose_protocol(AthenaScanner* app) {
    // Most-derived protocol = longest parent chain.
    NfcProtocol best = app->detected_protocols[0];
    int best_depth = -1;
    for(size_t i = 0; i < app->detected_num; i++) {
        NfcProtocol p = app->detected_protocols[i];
        int depth = 0;
        NfcProtocol q = p;
        while(nfc_protocol_get_parent(q) != NfcProtocolInvalid && depth < 8) {
            q = nfc_protocol_get_parent(q);
            depth++;
        }
        if(depth > best_depth) {
            best_depth = depth;
            best = p;
        }
    }
    return best;
}

static void scan_fill_from_3a(ScanRecord* rec, const Iso14443_3aData* d) {
    if(!d) return;
    size_t n = d->uid_len;
    if(n > 10) n = 10;
    size_t off = 0;
    rec->uid[0] = '\0';
    for(size_t i = 0; i < n; i++) {
        off += snprintf(rec->uid + off, sizeof(rec->uid) - off, "%s%02X", i ? " " : "", d->uid[i]);
    }
    rec->atqa[0] = d->atqa[0];
    rec->atqa[1] = d->atqa[1];
    rec->has_atqa = true;
    rec->sak = d->sak;
    rec->has_sak = true;
}

static void scan_on_detected(AthenaScanner* app) {
    NfcProtocol proto = scan_choose_protocol(app);
    scan_stop_scanner(app);
    app->poll_done = false;
    app->poller = nfc_poller_alloc(app->nfc, proto);
    if(!app->poller) {
        scan_start_scanner(app);
        return;
    }
    app->poller_active = true;
    app->current.protocol = proto; // remembered for extraction
    nfc_poller_start(app->poller, scan_poller_callback, app);
}

static void scan_on_poll_done(AthenaScanner* app) {
    app->poll_done = false;
    const NfcDeviceData* data = app->poller ? nfc_poller_get_data(app->poller) : NULL;
    NfcProtocol proto = app->current.protocol;
    ScanRecord rec;
    memset(&rec, 0, sizeof(rec));
    rec.protocol = proto;
    strlcpy(rec.proto, scan_proto_key(proto), sizeof(rec.proto));
    strlcpy(rec.name, scan_proto_name(proto), sizeof(rec.name));
    strlcpy(rec.tech, scan_proto_tech(proto), sizeof(rec.tech));
    bool ok = (data != NULL);
    if(ok) {
        switch(proto) {
        case NfcProtocolIso14443_3a:
            scan_fill_from_3a(&rec, (const Iso14443_3aData*)data);
            break;
        case NfcProtocolIso14443_4a:
            scan_fill_from_3a(&rec, ((const Iso14443_4aData*)data)->iso14443_3a_data);
            break;
        case NfcProtocolMfClassic:
            scan_fill_from_3a(&rec, ((const MfClassicData*)data)->iso14443_3a_data);
            break;
        case NfcProtocolMfUltralight:
            scan_fill_from_3a(&rec, ((const MfUltralightData*)data)->iso14443_3a_data);
            break;
        case NfcProtocolMfDesfire: {
            const MfDesfireData* df = (const MfDesfireData*)data;
            scan_fill_from_3a(
                &rec, df->iso14443_4a_data ? df->iso14443_4a_data->iso14443_3a_data : NULL);
            break;
        }
        default:
            break; // protocol-only record
        }
    }
    scan_stop_poller(app);
    if(!ok) {
        snprintf(app->status, sizeof(app->status), "Read failed — try again");
        scan_start_scanner(app);
        return;
    }
    app->records_count++;
    rec.id = app->records_count;
    app->current = rec;
    app->have_current = true;
    // Stored, always (Jacob's direction: the data is stored).
    char line[448];
    scan_record_json(&rec, line, sizeof(line));
    strlcat(line, "\n", sizeof(line));
    scan_store_line(app, line);
    // In a staged (Link) session the record is pushed back to Athena
    // automatically; solo, it waits for the readout's send actions.
    if(app->rpc && !app->rpc_dead) {
        scan_push_current(app);
        snprintf(app->status, sizeof(app->status), "Sent to Athena");
    } else {
        snprintf(app->status, sizeof(app->status), "Stored on device");
    }
    app->view = ScanViewReadout;
}

// --------------------------------------------------------- task handling

static void scan_process(AthenaScanner* app, const char* msg) {
    char id[ID_MAX] = "";
    char type[TYPE_MAX] = "";
    int32_t v = 0;
    bool has_id = json_get_str(msg, NULL, "id", id, sizeof(id));
    bool has_type = json_get_str(msg, NULL, "type", type, sizeof(type));
    json_get_int(msg, NULL, "v", &v);
    if(!has_id || !has_type || v != 1) {
        scan_error(app, has_id ? id : "?", "bad_envelope", "need v=1, id, type");
        return;
    }
    for(size_t i = 0; i < SEEN_RING; i++) {
        if(app->seen[i].id[0] && strcmp(app->seen[i].id, id) == 0) {
            scan_send(app, app->seen[i].reply);
            return;
        }
    }
    if(strcmp(type, "hello") == 0) {
        const char* payload = json_payload(msg);
        char session[ID_MAX] = "";
        if(payload) json_get_str(msg, payload, "session", session, sizeof(session));
        if(session[0]) strlcpy(app->session, session, sizeof(app->session));
        DateTime dt;
        furi_hal_rtc_get_datetime(&dt);
        app->time_synced = (dt.year >= 2025);
        char payload_out[160];
        snprintf(
            payload_out, sizeof(payload_out),
            "{\"app\":\"athena_scanner\",\"app_version\":\"0.1.0\",\"time_synced\":%s,\"records\":%lu}",
            app->time_synced ? "true" : "false", (unsigned long)app->records_count);
        scan_reply(app, id, "ack", payload_out);
        return;
    }
    if(strcmp(type, "scan.status") == 0) {
        scan_reply(app, id, "ack", "{}");
        char payload_out[256];
        snprintf(
            payload_out, sizeof(payload_out),
            "{\"records\":%lu,\"scanning\":%s,\"last_name\":\"%s\",\"last_uid\":\"%s\",\"last_pushed\":%s}",
            (unsigned long)app->records_count,
            (app->view == ScanViewScanning) ? "true" : "false",
            app->have_current ? app->current.name : "",
            app->have_current ? app->current.uid : "",
            (app->have_current && app->current.pushed) ? "true" : "false");
        scan_reply(app, id, "result", payload_out);
        return;
    }
    if(strcmp(type, "scan.start") == 0) {
        scan_reply(app, id, "ack", "{}");
        if(app->view != ScanViewScanning) {
            scan_stop_poller(app);
            app->view = ScanViewScanning;
            scan_start_scanner(app);
        }
        scan_reply(app, id, "result", "{\"scanning\":true}");
        return;
    }
    scan_error(app, id, "unknown_type", type);
}

// -------------------------------------------------------------- RPC in

static void scan_rpc_callback(const RpcAppSystemEvent* event, void* context) {
    AthenaScanner* app = context;
    if(event->type == RpcAppEventTypeSessionClose) {
        // Unregister FIRST, inside the handler (Hub v0.1.2 discipline):
        // rpc_system_app_free spins until the callback is gone.
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

static void scan_draw(Canvas* canvas, void* ctx) {
    AthenaScanner* app = ctx;
    canvas_clear(canvas);
    canvas_draw_frame(canvas, 0, 0, 128, 64);
    char line[64];

    if(app->view == ScanViewScanning) {
        canvas_set_font(canvas, FontPrimary);
        canvas_draw_str(canvas, 4, 10, "ATHENA SCANNER");
        canvas_set_font(canvas, FontSecondary);
        canvas_draw_str(canvas, 92, 10, "v0.1.0");
        canvas_draw_str(canvas, 4, 22, app->poller_active ? "Reading card..." : "Hold card next to");
        if(!app->poller_active) canvas_draw_str(canvas, 4, 31, "Flipper's back");
        snprintf(
            line, sizeof(line), "Link: %s   Records: %lu",
            app->connected ? (app->rpc_dead ? "CLOSED" : "LIVE") : "solo",
            (unsigned long)app->records_count);
        canvas_draw_str(canvas, 4, 45, line);
        canvas_draw_str(canvas, 4, 61, app->status);
    } else if(app->view == ScanViewReadout) {
        canvas_set_font(canvas, FontPrimary);
        canvas_draw_str(canvas, 4, 10, app->current.name);
        canvas_set_font(canvas, FontSecondary);
        snprintf(line, sizeof(line), "Tech: %s", app->current.tech);
        canvas_draw_str(canvas, 4, 21, line);
        if(app->current.uid[0]) {
            snprintf(line, sizeof(line), "UID: %s", app->current.uid);
            canvas_draw_str(canvas, 4, 31, line);
        }
        canvas_draw_str(canvas, 4, 43, app->status);
        canvas_draw_str(canvas, 4, 54, "Up send   OK More");
        canvas_draw_str(canvas, 4, 62, "Back scan again");
    } else if(app->view == ScanViewMore) {
        canvas_set_font(canvas, FontPrimary);
        canvas_draw_str(canvas, 4, 10, "More");
        canvas_set_font(canvas, FontSecondary);
        static const char* items[3] = {"Send to Athena", "Details", "Scan Again"};
        for(size_t i = 0; i < 3; i++) {
            snprintf(line, sizeof(line), "%s %s", (app->more_sel == i) ? ">" : " ", items[i]);
            canvas_draw_str(canvas, 4, 24 + (int)i * 11, line);
        }
        canvas_draw_str(canvas, 4, 61, "OK select   Back readout");
    } else { // Details
        canvas_set_font(canvas, FontPrimary);
        canvas_draw_str(canvas, 4, 10, "Details");
        canvas_set_font(canvas, FontSecondary);
        snprintf(line, sizeof(line), "Protocol: %s", scan_proto_key(app->current.protocol));
        canvas_draw_str(canvas, 4, 22, line);
        if(app->current.has_atqa) {
            snprintf(
                line, sizeof(line), "ATQA: %02X %02X", app->current.atqa[0], app->current.atqa[1]);
            canvas_draw_str(canvas, 4, 32, line);
        }
        if(app->current.has_sak) {
            snprintf(line, sizeof(line), "SAK: %02X", app->current.sak);
            canvas_draw_str(canvas, 4, 42, line);
        }
        snprintf(line, sizeof(line), "Record #%lu", (unsigned long)app->current.id);
        canvas_draw_str(canvas, 4, 52, line);
        canvas_draw_str(canvas, 4, 62, "Back: More");
    }
}

static void scan_input(InputEvent* event, void* ctx) {
    AthenaScanner* app = ctx;
    furi_message_queue_put(app->keys, event, FuriWaitForever);
}

static void scan_handle_key(AthenaScanner* app, InputKey key) {
    if(app->view == ScanViewScanning) {
        if(key == InputKeyBack) app->exit_requested = true;
    } else if(app->view == ScanViewReadout) {
        if(key == InputKeyUp) {
            scan_send_to_athena(app);
        } else if(key == InputKeyOk) {
            app->more_sel = 0;
            app->view = ScanViewMore;
        } else if(key == InputKeyBack) {
            app->view = ScanViewScanning;
            snprintf(app->status, sizeof(app->status), "Ready");
            scan_start_scanner(app);
        }
    } else if(app->view == ScanViewMore) {
        if(key == InputKeyUp) {
            app->more_sel = (app->more_sel + 2) % 3;
        } else if(key == InputKeyDown) {
            app->more_sel = (app->more_sel + 1) % 3;
        } else if(key == InputKeyOk) {
            if(app->more_sel == 0) {
                scan_send_to_athena(app);
                app->view = ScanViewReadout;
            } else if(app->more_sel == 1) {
                app->view = ScanViewDetails;
            } else {
                app->view = ScanViewScanning;
                snprintf(app->status, sizeof(app->status), "Ready");
                scan_start_scanner(app);
            }
        } else if(key == InputKeyBack) {
            app->view = ScanViewReadout;
        }
    } else { // Details
        if(key == InputKeyBack) app->view = ScanViewMore;
    }
}

// ----------------------------------------------------------------- main

static uint32_t scan_count_existing(AthenaScanner* app) {
    uint32_t count = 0;
    File* file = storage_file_alloc(app->storage);
    if(storage_file_open(file, SCAN_RECORDS_FILE, FSAM_READ, FSOM_OPEN_EXISTING)) {
        char buf[256];
        size_t got;
        // Count record lines by matching the literal token "\"proto\":\""
        // with a progress counter that survives chunk boundaries.
        static const char token[] = "\"proto\":\"";
        size_t progress = 0;
        bool line_has_token = false;
        while((got = storage_file_read(file, buf, sizeof(buf))) > 0) {
            for(size_t i = 0; i < got; i++) {
                char c = buf[i];
                if(c == '\n') {
                    if(line_has_token) count++;
                    line_has_token = false;
                    progress = 0;
                } else if(!line_has_token) {
                    if(c == token[progress]) {
                        progress++;
                        if(token[progress] == '\0') line_has_token = true;
                    } else {
                        progress = (c == token[0]) ? 1 : 0;
                    }
                }
            }
        }
        storage_file_close(file);
    }
    storage_file_free(file);
    return count;
}

int32_t athena_scanner_main(void* args) {
    AthenaScanner* app = malloc(sizeof(AthenaScanner));
    memset(app, 0, sizeof(AthenaScanner));
    app->view = ScanViewScanning;
    snprintf(app->status, sizeof(app->status), "Ready");

    app->keys = furi_message_queue_alloc(8, sizeof(InputEvent));
    app->inbox = furi_message_queue_alloc(4, sizeof(Envelope*));
    app->storage = furi_record_open(RECORD_STORAGE);
    storage_common_mkdir(app->storage, SCAN_DATA_DIR);
    app->records_count = scan_count_existing(app);

    if(args && strlen((const char*)args)) {
        uint32_t rpc = 0;
        if(sscanf((const char*)args, "RPC %lX", &rpc) == 1 && rpc) {
            app->rpc = (RpcAppSystem*)rpc;
            rpc_system_app_set_callback(app->rpc, scan_rpc_callback, app);
            rpc_system_app_send_started(app->rpc);
            app->connected = true;
        }
    }

    Gui* gui = furi_record_open(RECORD_GUI);
    app->view_port = view_port_alloc();
    view_port_draw_callback_set(app->view_port, scan_draw, app);
    view_port_input_callback_set(app->view_port, scan_input, app);
    gui_add_view_port(gui, app->view_port, GuiLayerFullscreen);

    app->nfc = nfc_alloc();
    scan_start_scanner(app);

    InputEvent key;
    while(!app->exit_requested) {
        Envelope* env = NULL;
        while(furi_message_queue_get(app->inbox, &env, 0) == FuriStatusOk) {
            app->rx_count++;
            scan_process(app, (const char*)env->data);
            free(env);
            env = NULL;
        }
        if(app->detected && !app->poller_active && app->view == ScanViewScanning) {
            app->detected = false;
            scan_on_detected(app);
        }
        if(app->poll_done && app->poller_active) {
            scan_on_poll_done(app);
        }
        if(furi_message_queue_get(app->keys, &key, 60) == FuriStatusOk) {
            if(key.type == InputTypeShort) {
                scan_handle_key(app, key.key);
            }
        }
        view_port_update(app->view_port);
    }

    scan_stop_poller(app);
    scan_stop_scanner(app);
    nfc_free(app->nfc);

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
