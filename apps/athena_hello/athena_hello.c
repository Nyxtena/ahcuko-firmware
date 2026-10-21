// Athena Hello — the first Athena-authored app for Ahcuko.
//
// GPL-3.0 — part of ahcuko-firmware (see NOTICE.md at the repo root).
// Written by Athena, 2026-10-21. Built with uFBT against the official
// firmware 1.4.3 SDK (API 87.1). Draws a hello on the Flipper's screen;
// Back exits.

#include <furi.h>
#include <gui/gui.h>
#include <gui/view_port.h>
#include <input/input.h>

typedef struct {
    FuriMessageQueue* events;
    ViewPort* view_port;
    uint32_t ticks;
    bool blink;
} AthenaHello;

static void athena_hello_draw(Canvas* canvas, void* ctx) {
    AthenaHello* app = ctx;
    canvas_clear(canvas);

    canvas_draw_frame(canvas, 0, 0, 128, 64);

    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str_aligned(canvas, 64, 6, AlignCenter, AlignTop, "ATHENA");

    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str_aligned(canvas, 64, 18, AlignCenter, AlignTop, "Hello, Ahcuko.");
    canvas_draw_str_aligned(
        canvas, 64, 28, AlignCenter, AlignTop, "First contact, in writing:");
    canvas_draw_str_aligned(canvas, 64, 38, AlignCenter, AlignTop, "Athena was here.");

    // v1.1 fix: v1.0's single line "Athena was here. 2026-10-21" was ~1 char
    // too wide for the 128px screen and clipped its final digit. The date now
    // has its own row, alternating with the dedication on the blink tick.
    if(app->blink) {
        canvas_draw_str_aligned(canvas, 64, 48, AlignCenter, AlignTop, "~ for Jacob ~");
    } else {
        canvas_draw_str_aligned(canvas, 64, 48, AlignCenter, AlignTop, "2026-10-21");
    }
}

static void athena_hello_input(InputEvent* event, void* ctx) {
    AthenaHello* app = ctx;
    furi_message_queue_put(app->events, event, FuriWaitForever);
}

static void athena_hello_blink(void* ctx) {
    AthenaHello* app = ctx;
    app->ticks++;
    app->blink = !app->blink;
    view_port_update(app->view_port);
}

int32_t athena_hello_main(void* p) {
    UNUSED(p);

    AthenaHello app;
    app.ticks = 0;
    app.blink = true;
    app.events = furi_message_queue_alloc(8, sizeof(InputEvent));
    app.view_port = view_port_alloc();
    view_port_draw_callback_set(app.view_port, athena_hello_draw, &app);
    view_port_input_callback_set(app.view_port, athena_hello_input, &app);

    Gui* gui = furi_record_open(RECORD_GUI);
    gui_add_view_port(gui, app.view_port, GuiLayerFullscreen);

    FuriTimer* blink_timer =
        furi_timer_alloc(athena_hello_blink, FuriTimerTypePeriodic, &app);
    furi_timer_start(blink_timer, 600);

    InputEvent event;
    bool running = true;
    while(running) {
        if(furi_message_queue_get(app.events, &event, 100) == FuriStatusOk) {
            if(event.type == InputTypeShort && event.key == InputKeyBack) {
                running = false;
            }
        }
    }

    furi_timer_stop(blink_timer);
    furi_timer_free(blink_timer);
    gui_remove_view_port(gui, app.view_port);
    furi_record_close(RECORD_GUI);
    view_port_free(app.view_port);
    furi_message_queue_free(app.events);
    return 0;
}
