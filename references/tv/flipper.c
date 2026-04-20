#include <furi.h>
#include <furi_hal.h>
#include <gui/gui.h>
#include <gui/view_port.h>
#include <input/input.h>

typedef struct {
    InputEvent input;
} AppInputEvent;

typedef enum {
    AppEventTypeInput,
    AppEventTypeBleAdvOnce,
} AppEventType;

typedef struct {
    AppEventType type;
    union {
        AppInputEvent input;
    } data;
} AppEvent;

typedef struct {
    ViewPort* vp;
    FuriMessageQueue* event_q;
    bool running;
    bool active;
} App;

static void draw_cb(Canvas* canvas, void* ctx) {
    App* app = ctx;
    canvas_clear(canvas);
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 2, 12, "Switch2 Wake Test");

    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str(canvas, 2, 28, app->active ? "State: ACTIVE" : "State: IDLE");
    canvas_draw_str(canvas, 2, 40, "OK: toggle  BACK: exit");
}

static void input_cb(InputEvent* event, void* ctx) {
    App* app = ctx;
    AppEvent app_event = {
        .type = AppEventTypeInput,
        .data.input.input = *event,
    };
    furi_message_queue_put(app->event_q, &app_event, 0);
}

static void post_ble_adv_once_event(App* app) {
    AppEvent app_event = {
        .type = AppEventTypeBleAdvOnce,
    };
    furi_message_queue_put(app->event_q, &app_event, 0);
}

static bool send_ble_advertisement_once(void) {
    static const uint8_t adv_payload[] = {
        0x02, 0x01, 0x06, // Flags
        0x1B, 0xFF, 0x53, 0x05, 0x01, 0x00, 0x03, 0x7E, 0x05, 0x66, 0x20, 0x00, 0x01,
        0x81, 0x97, 0x1F, 0x99, 0x55, 0xE2, 0x98, 0x0F, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00,
    };

    static const GapExtraBeaconConfig beacon_cfg = {
        .min_adv_interval_ms = 20,
        .max_adv_interval_ms = 30,
        .adv_channel_map = GapAdvChannelMapAll,
        .adv_power_level = GapAdvPowerLevel_0dBm,
        .address_type = GapAddressTypePublic,
        .address = {0xD8, 0xF7, 0xAE, 0x55, 0xE2, 0x98},
    };

    bool ok = true;
    furi_hal_bt_extra_beacon_stop();
    ok = ok && furi_hal_bt_extra_beacon_set_config(&beacon_cfg);
    ok = ok && furi_hal_bt_extra_beacon_set_data(adv_payload, sizeof(adv_payload));
    ok = ok && furi_hal_bt_extra_beacon_start();

    // Transmit for a short window to send a single burst.
    furi_delay_ms(100);
    ok = ok && furi_hal_bt_extra_beacon_stop();

    return ok;
}

int32_t switch2_wake_test_app(void* p) {
    UNUSED(p);

    App app = {0};
    app.event_q = furi_message_queue_alloc(8, sizeof(AppEvent));
    app.vp = view_port_alloc();
    view_port_draw_callback_set(app.vp, draw_cb, &app);
    view_port_input_callback_set(app.vp, input_cb, &app);

    Gui* gui = furi_record_open(RECORD_GUI);
    gui_add_view_port(gui, app.vp, GuiLayerFullscreen);

    app.running = true;
    while(app.running) {
        AppEvent ev;
        if(furi_message_queue_get(app.event_q, &ev, 100) == FuriStatusOk) {
            if(ev.type == AppEventTypeInput) {
                InputEvent* input = &ev.data.input.input;
                if(input->type == InputTypeShort) {
                    if(input->key == InputKeyBack) {
                        app.running = false;
                    } else if(input->key == InputKeyOk) {
                        app.active = !app.active;
                        if(app.active) {
                            post_ble_adv_once_event(&app);
                        }
                        FURI_LOG_I("switch2_wake_test", "Active=%d", app.active);
                        view_port_update(app.vp);
                    }
                }
            } else if(ev.type == AppEventTypeBleAdvOnce) {
                const bool sent = send_ble_advertisement_once();
                FURI_LOG_I("switch2_wake_test", "One-shot BLE advertisement sent=%d", sent);
            }
        }
    }

    gui_remove_view_port(gui, app.vp);
    furi_record_close(RECORD_GUI);

    view_port_free(app.vp);
    furi_message_queue_free(app.event_q);
    return 0;
}
