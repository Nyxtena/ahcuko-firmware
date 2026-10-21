// SPDX-License-Identifier: GPL-3.0-only
//
// House Census v0.1 — Athena's household emitter inventory for Ahcuko.
//
// Design: docs/fork-design-v0.1-2026-10-21.md §6.1; test card:
// docs/d5-house-census-test-card-2026-10-21.md. Receive/inventory-first:
// the CC1101 is only ever started in async RX; this file contains no
// transmit call path at all (source-auditable, per the design rule that
// transmission is never a side effect of taking census).
//
// v0.1 scope: the census core (record store at
// /ext/apps_data/house_census/census.jsonl, friendly-name sidecar,
// human-readable export) + the Sub-GHz receive collector (stock
// protocol decoders, three listening frequencies). NFC/RFID and IR
// collectors are the v0.2 increments on the same test card.

#include <furi.h>
#include <furi_hal.h>
#include <gui/gui.h>
#include <input/input.h>
#include <storage/storage.h>

#include <lib/subghz/types.h>
#include <lib/subghz/environment.h>
#include <lib/subghz/receiver.h>
#include <lib/subghz/subghz_worker.h>
#include <lib/subghz/subghz_protocol_registry.h>
#include <lib/subghz/protocols/base.h>
#include <lib/subghz/devices/cc1101_int/cc1101_int_interconnect.h>

/* subghz_devices_* are exported in API 87.1 (targets/f7/api_symbols.csv);
 * their declaring header lib/subghz/devices/devices.h is not shipped in
 * the SDK, so the prototypes used here are declared locally, verbatim. */
void subghz_devices_init(void);
void subghz_devices_deinit(void);
const SubGhzDevice* subghz_devices_get_by_name(const char* name);
bool subghz_devices_begin(const SubGhzDevice* device);
void subghz_devices_end(const SubGhzDevice* device);
void subghz_devices_idle(const SubGhzDevice* device);
void subghz_devices_load_preset(
    const SubGhzDevice* device,
    FuriHalSubGhzPreset preset,
    uint8_t* preset_data);
uint32_t subghz_devices_set_frequency(const SubGhzDevice* device, uint32_t frequency);
void subghz_devices_flush_rx(const SubGhzDevice* device);
void subghz_devices_start_async_rx(const SubGhzDevice* device, void* callback, void* context);
void subghz_devices_stop_async_rx(const SubGhzDevice* device);
float subghz_devices_get_rssi(const SubGhzDevice* device);

#define TAG "HouseCensus"
#define CENSUS_DIR "/ext/apps_data/house_census"
#define CENSUS_PATH CENSUS_DIR "/census.jsonl"
#define NAMES_PATH CENSUS_DIR "/names.txt"
#define EXPORT_DIR CENSUS_DIR "/exports"
#define EXPORT_PATH EXPORT_DIR "/census.md"

#define MAX_RECORDS 64
#define DETAIL_LEN 72

typedef struct {
    char protocol[24];
    uint32_t hash;
    uint32_t freq_hz;
    int count;
    char first[20];
    char last[20];
    int rssi;
    char detail[DETAIL_LEN];
    char name[28];
} CensusRecord;

typedef struct {
    Gui* gui;
    ViewPort* view_port;
    FuriMessageQueue* queue;
    Storage* storage;
    FuriMutex* mutex;

    CensusRecord records[MAX_RECORDS];
    int record_count;
    int session_obs;
    bool dirty;
    uint32_t last_flush_tick;

    bool listening;
    int freq_index;
    // radio chain (valid while listening)
    SubGhzEnvironment* environment;
    SubGhzReceiver* receiver;
    SubGhzWorker* worker;
    const SubGhzDevice* device;

    char last_line[40];
    bool exit_requested;
} CensusApp;

static const struct {
    uint32_t hz;
    const char* label;
} FREQ_TABLE[] = {
    {433920000, "433.92"},
    {315000000, "315.00"},
    {915000000, "915.00"},
};
#define FREQ_COUNT 3

static void fmt_now(char* out, size_t len) {
    DateTime dt;
    furi_hal_rtc_get_datetime(&dt);
    char tmp[40];
    snprintf(
        tmp,
        sizeof(tmp),
        "%04d-%02d-%02d %02d:%02d:%02d",
        dt.year,
        dt.month,
        dt.day,
        dt.hour,
        dt.minute,
        dt.second);
    size_t i = 0;
    while(tmp[i] && i + 1 < len) {
        out[i] = tmp[i];
        i++;
    }
    out[i] = '\0';
}

static uint32_t fnv1a(const char* s) {
    uint32_t h = 2166136261u;
    while(*s) {
        h ^= (uint8_t)*s++;
        h *= 16777619u;
    }
    return h;
}

// ---------------------------------------------------------------- radio

static void census_rx_callback(
    SubGhzReceiver* receiver,
    SubGhzProtocolDecoderBase* decoder,
    void* context) {
    UNUSED(receiver);
    CensusApp* app = context;

    FuriString* text = furi_string_alloc();
    if(!subghz_protocol_decoder_base_get_string(decoder, text)) {
        furi_string_free(text);
        return;
    }
    // Flatten the multi-line decode text into one sanitized detail line.
    char detail[DETAIL_LEN];
    size_t w = 0;
    const char* s = furi_string_get_cstr(text);
    // Skip the first line (it repeats the protocol name).
    const char* nl = strchr(s, '\n');
    s = nl ? nl + 1 : s;
    for(size_t i = 0; s[i] && w < DETAIL_LEN - 1; i++) {
        char c = s[i];
        if(c == '\n' || c == '\r') c = ';';
        if(c == '"' || c == '\\') c = ' ';
        if(c == ';' && w + 1 < DETAIL_LEN - 1) {
            detail[w++] = ';';
            detail[w++] = ' ';
            while(s[i + 1] == ' ') i++;
        } else {
            detail[w++] = c;
        }
    }
    detail[w] = '\0';
    furi_string_free(text);

    const char* proto = decoder->protocol->name;
    uint32_t hash = fnv1a(detail);
    int rssi = (int)subghz_devices_get_rssi(app->device);
    char now[20];
    fmt_now(now, sizeof(now));

    furi_mutex_acquire(app->mutex, FuriWaitForever);
    CensusRecord* rec = NULL;
    for(int i = 0; i < app->record_count; i++) {
        if(app->records[i].hash == hash && strcmp(app->records[i].protocol, proto) == 0) {
            rec = &app->records[i];
            break;
        }
    }
    if(rec) {
        rec->count++;
        snprintf(rec->last, sizeof(rec->last), "%s", now);
        rec->rssi = rssi;
        rec->freq_hz = FREQ_TABLE[app->freq_index].hz;
    } else if(app->record_count < MAX_RECORDS) {
        rec = &app->records[app->record_count++];
        memset(rec, 0, sizeof(*rec));
        snprintf(rec->protocol, sizeof(rec->protocol), "%s", proto);
        rec->hash = hash;
        rec->freq_hz = FREQ_TABLE[app->freq_index].hz;
        rec->count = 1;
        snprintf(rec->first, sizeof(rec->first), "%s", now);
        snprintf(rec->last, sizeof(rec->last), "%s", now);
        rec->rssi = rssi;
        snprintf(rec->detail, sizeof(rec->detail), "%s", detail);
    }
    app->session_obs++;
    app->dirty = true;
    snprintf(app->last_line, sizeof(app->last_line), "%s #%08lx", proto, (unsigned long)hash);
    furi_mutex_release(app->mutex);
}

static bool radio_start(CensusApp* app) {
    app->environment = subghz_environment_alloc();
    subghz_environment_load_keystore(app->environment, SUBGHZ_KEYSTORE_DIR_NAME);
    subghz_environment_load_keystore(app->environment, SUBGHZ_KEYSTORE_DIR_USER_NAME);
    subghz_environment_set_came_atomo_rainbow_table_file_name(
        app->environment, SUBGHZ_CAME_ATOMO_DIR_NAME);
    subghz_environment_set_alutech_at_4n_rainbow_table_file_name(
        app->environment, SUBGHZ_ALUTECH_AT_4N_DIR_NAME);
    subghz_environment_set_nice_flor_s_rainbow_table_file_name(
        app->environment, SUBGHZ_NICE_FLOR_S_DIR_NAME);
    subghz_environment_set_protocol_registry(
        app->environment, (void*)&subghz_protocol_registry);

    app->receiver = subghz_receiver_alloc_init(app->environment);
    subghz_receiver_set_filter(app->receiver, SubGhzProtocolFlag_Decodable);
    subghz_receiver_set_rx_callback(app->receiver, census_rx_callback, app);

    app->worker = subghz_worker_alloc();
    subghz_worker_set_overrun_callback(
        app->worker, (SubGhzWorkerOverrunCallback)subghz_receiver_reset);
    subghz_worker_set_pair_callback(
        app->worker, (SubGhzWorkerPairCallback)subghz_receiver_decode);
    subghz_worker_set_context(app->worker, app->receiver);

    subghz_devices_init();
    // NB: the descriptor variable subghz_device_cc1101_int is NOT
    // exported to FAPs (api_symbols status '-'); the accessor is.
    app->device = subghz_devices_get_by_name(SUBGHZ_DEVICE_CC1101_INT_NAME);
    if(!app->device) {
        FURI_LOG_E(TAG, "radio device not found");
        subghz_devices_deinit();
        subghz_worker_free(app->worker);
        subghz_receiver_free(app->receiver);
        subghz_environment_free(app->environment);
        app->worker = NULL;
        app->receiver = NULL;
        app->environment = NULL;
        return false;
    }
    // The internal CC1101 interconnect has no begin step (begin == NULL),
    // so subghz_devices_begin returns false for it by design; the stock
    // Sub-GHz wrapper proceeds regardless. Do the same.
    subghz_devices_begin(app->device);
    subghz_devices_load_preset(app->device, FuriHalSubGhzPresetOok650Async, NULL);
    subghz_devices_idle(app->device);
    subghz_devices_set_frequency(app->device, FREQ_TABLE[app->freq_index].hz);
    subghz_devices_flush_rx(app->device);
    subghz_devices_start_async_rx(
        app->device, (void*)subghz_worker_rx_callback, app->worker);
    subghz_worker_start(app->worker);
    app->listening = true;
    return true;
}

static void radio_stop(CensusApp* app) {
    if(!app->listening) return;
    subghz_worker_stop(app->worker);
    subghz_devices_stop_async_rx(app->device);
    subghz_devices_idle(app->device);
    subghz_devices_end(app->device);
    subghz_devices_deinit();
    subghz_worker_free(app->worker);
    subghz_receiver_free(app->receiver);
    subghz_environment_free(app->environment);
    app->worker = NULL;
    app->receiver = NULL;
    app->environment = NULL;
    app->device = NULL;
    app->listening = false;
}

// ---------------------------------------------------------------- store

static bool json_str(const char* line, const char* key, char* out, size_t len) {
    char pat[40];
    snprintf(pat, sizeof(pat), "\"%s\":\"", key);
    const char* p = strstr(line, pat);
    if(!p) return false;
    p += strlen(pat);
    size_t i = 0;
    while(*p && *p != '"' && i < len - 1) out[i++] = *p++;
    out[i] = '\0';
    return true;
}

static bool json_num(const char* line, const char* key, long* out) {
    char pat[40];
    snprintf(pat, sizeof(pat), "\"%s\":", key);
    const char* p = strstr(line, pat);
    if(!p) return false;
    p += strlen(pat);
    *out = strtol(p, NULL, 10);
    return true;
}

// strtok is not exported to FAPs; split lines by hand.
static char* next_line(char** cursor) {
    if(!*cursor || !**cursor) return NULL;
    char* start = *cursor;
    char* nl = strchr(start, '\n');
    if(nl) {
        *nl = '\0';
        *cursor = nl + 1;
    } else {
        *cursor = start + strlen(start);
    }
    return start;
}

static void census_load(CensusApp* app) {
    File* f = storage_file_alloc(app->storage);
    if(storage_file_open(f, CENSUS_PATH, FSAM_READ, FSOM_OPEN_EXISTING)) {
        uint64_t size = storage_file_size(f);
        if(size > 0 && size < 60000) {
            char* buf = malloc(size + 1);
            size_t rd = storage_file_read(f, buf, size);
            buf[rd] = '\0';
            char* cursor = buf;
            char* line = next_line(&cursor);
            while(line && app->record_count < MAX_RECORDS) {
                CensusRecord* r = &app->records[app->record_count];
                memset(r, 0, sizeof(*r));
                long v;
                if(!json_str(line, "protocol", r->protocol, sizeof(r->protocol))) {
                    line = next_line(&cursor);
                    continue;
                }
                if(json_num(line, "hash", &v)) r->hash = (uint32_t)v;
                if(json_num(line, "freq_hz", &v)) r->freq_hz = (uint32_t)v;
                if(json_num(line, "count", &v)) r->count = (int)v;
                if(json_num(line, "rssi_dbm", &v)) r->rssi = (int)v;
                json_str(line, "first", r->first, sizeof(r->first));
                json_str(line, "last", r->last, sizeof(r->last));
                json_str(line, "detail", r->detail, sizeof(r->detail));
                json_str(line, "name", r->name, sizeof(r->name));
                app->record_count++;
                line = next_line(&cursor);
            }
            free(buf);
        }
        storage_file_close(f);
    }
    storage_file_free(f);

    // Friendly names sidecar: "hash=name" per line (decimal hash).
    File* nf = storage_file_alloc(app->storage);
    if(storage_file_open(nf, NAMES_PATH, FSAM_READ, FSOM_OPEN_EXISTING)) {
        uint64_t size = storage_file_size(nf);
        if(size > 0 && size < 8000) {
            char* buf = malloc(size + 1);
            size_t rd = storage_file_read(nf, buf, size);
            buf[rd] = '\0';
            char* cursor = buf;
            char* line = next_line(&cursor);
            while(line) {
                char* eq = strchr(line, '=');
                if(eq) {
                    uint32_t h = (uint32_t)strtoul(line, NULL, 10);
                    for(int i = 0; i < app->record_count; i++) {
                        if(app->records[i].hash == h) {
                            snprintf(
                                app->records[i].name,
                                sizeof(app->records[i].name),
                                "%s",
                                eq + 1);
                        }
                    }
                }
                line = next_line(&cursor);
            }
            free(buf);
        }
        storage_file_close(nf);
    }
    storage_file_free(nf);
}

static void file_write_str(File* f, const char* s) {
    storage_file_write(f, s, strlen(s));
}

static void census_flush(CensusApp* app) {
    File* f = storage_file_alloc(app->storage);
    if(storage_file_open(f, CENSUS_PATH, FSAM_WRITE, FSOM_CREATE_ALWAYS)) {
        char line[512];
        for(int i = 0; i < app->record_count; i++) {
            CensusRecord* r = &app->records[i];
            snprintf(
                line,
                sizeof(line),
                "{\"kind\":\"subghz\",\"protocol\":\"%s\",\"hash\":%lu,\"freq_hz\":%lu,"
                "\"preset\":\"AM650\",\"count\":%d,\"first\":\"%s\",\"last\":\"%s\","
                "\"rssi_dbm\":%d,\"detail\":\"%s\",\"name\":\"%s\"}\n",
                r->protocol,
                (unsigned long)r->hash,
                (unsigned long)r->freq_hz,
                r->count,
                r->first,
                r->last,
                r->rssi,
                r->detail,
                r->name);
            file_write_str(f, line);
        }
        storage_file_close(f);
    }
    storage_file_free(f);
    app->dirty = false;
    app->last_flush_tick = furi_get_tick();
}

static void census_export_md(CensusApp* app) {
    File* f = storage_file_alloc(app->storage);
    if(storage_file_open(f, EXPORT_PATH, FSAM_WRITE, FSOM_CREATE_ALWAYS)) {
        char line[512];
        file_write_str(f, "# House Census — Sub-GHz records\n\n");
        file_write_str(
            f, "| Name | Protocol | Freq | Count | First | Last | RSSI | Detail |\n");
        file_write_str(f, "|---|---|---|---|---|---|---|---|\n");
        for(int i = 0; i < app->record_count; i++) {
            CensusRecord* r = &app->records[i];
            snprintf(
                line,
                sizeof(line),
                "| %s | %s | %lu | %d | %s | %s | %d | %s |\n",
                r->name[0] ? r->name : "(unnamed)",
                r->protocol,
                (unsigned long)r->freq_hz,
                r->count,
                r->first,
                r->last,
                r->rssi,
                r->detail);
            file_write_str(f, line);
        }
        storage_file_close(f);
    }
    storage_file_free(f);
}

// ---------------------------------------------------------------- gui

static void census_draw(Canvas* canvas, void* model) {
    CensusApp* app = model;
    char buf[64];
    canvas_clear(canvas);
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 2, 10, "House Census");
    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str(canvas, 92, 10, "v0.1.1");
    canvas_draw_line(canvas, 0, 13, 127, 13);

    if(app->listening) {
        snprintf(
            buf,
            sizeof(buf),
            "Listening %s MHz AM650",
            FREQ_TABLE[app->freq_index].label);
        canvas_draw_str(canvas, 2, 24, buf);
        snprintf(buf, sizeof(buf), "RX this session: %d", app->session_obs);
        canvas_draw_str(canvas, 2, 36, buf);
        if(app->last_line[0]) {
            canvas_draw_str(canvas, 2, 48, app->last_line);
        } else {
            canvas_draw_str(canvas, 2, 48, "(nothing heard yet)");
        }
        canvas_draw_str(canvas, 2, 62, "OK stop   </> freq   Back menu");
    } else {
        snprintf(
            buf,
            sizeof(buf),
            "Freq: %s MHz   Records: %d",
            FREQ_TABLE[app->freq_index].label,
            app->record_count);
        canvas_draw_str(canvas, 2, 24, buf);
        canvas_draw_str(canvas, 2, 36, "OK: listen   Left/Right: freq");
        canvas_draw_str(canvas, 2, 48, "Down: export census.md");
        if(app->last_line[0]) {
            canvas_draw_str(canvas, 2, 62, app->last_line);
        } else {
            canvas_draw_str(canvas, 2, 62, "Back: exit");
        }
    }
}

static void census_input(InputEvent* event, void* context) {
    CensusApp* app = context;
    furi_message_queue_put(app->queue, event, FuriWaitForever);
}

int32_t house_census_app(void* p) {
    UNUSED(p);
    CensusApp* app = malloc(sizeof(CensusApp));
    memset(app, 0, sizeof(*app));
    app->mutex = furi_mutex_alloc(FuriMutexTypeNormal);
    app->storage = furi_record_open(RECORD_STORAGE);
    storage_common_mkdir(app->storage, CENSUS_DIR);
    storage_common_mkdir(app->storage, EXPORT_DIR);
    census_load(app);

    app->gui = furi_record_open(RECORD_GUI);
    app->view_port = view_port_alloc();
    view_port_draw_callback_set(app->view_port, census_draw, app);
    app->queue = furi_message_queue_alloc(8, sizeof(InputEvent));
    view_port_input_callback_set(app->view_port, census_input, app);
    gui_add_view_port(app->gui, app->view_port, GuiLayerFullscreen);

    InputEvent event;
    while(!app->exit_requested) {
        if(furi_message_queue_get(app->queue, &event, 250) == FuriStatusOk) {
            if(event.type == InputTypePress) {
                if(event.key == InputKeyBack) {
                    if(app->listening) {
                        radio_stop(app);
                    } else {
                        app->exit_requested = true;
                    }
                } else if(event.key == InputKeyOk) {
                    if(app->listening) {
                        radio_stop(app);
                    } else if(!radio_start(app)) {
                        snprintf(app->last_line, sizeof(app->last_line), "radio failed");
                    }
                } else if(event.key == InputKeyDown && !app->listening) {
                    census_flush(app);
                    census_export_md(app);
                    snprintf(app->last_line, sizeof(app->last_line), "exported census.md");
                } else if(event.key == InputKeyLeft || event.key == InputKeyRight) {
                    bool was = app->listening;
                    if(was) radio_stop(app);
                    app->freq_index = (event.key == InputKeyRight) ?
                                           (app->freq_index + 1) % FREQ_COUNT :
                                           (app->freq_index + FREQ_COUNT - 1) % FREQ_COUNT;
                    if(was) radio_start(app);
                }
            } else if(
                event.type == InputTypeLong && event.key == InputKeyRight &&
                !app->listening) {
                // Long Right in the menu = export, not frequency.
                census_flush(app);
                census_export_md(app);
                snprintf(app->last_line, sizeof(app->last_line), "exported census.md");
            }
        }
        if(app->dirty &&
           (!app->listening || furi_get_tick() - app->last_flush_tick > 2000)) {
            furi_mutex_acquire(app->mutex, FuriWaitForever);
            census_flush(app);
            furi_mutex_release(app->mutex);
        }
        view_port_update(app->view_port);
    }

    if(app->listening) radio_stop(app);
    census_flush(app);
    census_export_md(app);

    gui_remove_view_port(app->gui, app->view_port);
    view_port_free(app->view_port);
    furi_message_queue_free(app->queue);
    furi_record_close(RECORD_GUI);
    furi_record_close(RECORD_STORAGE);
    furi_mutex_free(app->mutex);
    free(app);
    return 0;
}
