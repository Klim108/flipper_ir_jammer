/*
 * IR Jammer
 * by: Ki213
 */
#include <furi.h>  // by: Ki213
#include <furi_hal_infrared.h>  // by: Ki213
#include <gui/gui.h>  // by: Ki213
#include <gui/elements.h>  // by: Ki213
#include <input/input.h>  // by: Ki213
#include <string.h>  // by: Ki213
// by: Ki213
/* Common IR carrier frequencies used across consumer remotes (TV, AC,  // by: Ki213
 * etc.) - cycle through these with Left/Right to jam a wider or  // by: Ki213
 * narrower slice of what's actually in use nearby. 38kHz is by far  // by: Ki213
 * the most common. */  // by: Ki213
static const uint32_t kFrequencies[] = {33000, 36000, 38000, 40000, 56000};  // by: Ki213
#define FREQ_COUNT (sizeof(kFrequencies) / sizeof(kFrequencies[0]))  // by: Ki213
#define DEFAULT_FREQ_INDEX 2 /* 38kHz */  // by: Ki213
#define DUTY_CYCLE 0.33f  // by: Ki213
// by: Ki213
typedef struct {  // by: Ki213
    ViewPort* view_port;  // by: Ki213
    Gui* gui;  // by: Ki213
    FuriMessageQueue* event_queue;  // by: Ki213
    bool jamming;  // by: Ki213
    size_t freq_index;  // by: Ki213
    FuriHalInfraredTxPin tx_pin; /* Internal LED, or external module on pin 2 (PA7) */  // by: Ki213
    bool external_was_detected; /* what auto-detect found at startup */  // by: Ki213
    uint32_t noise_seed; /* only touched from the TX ISR - fine, single writer */  // by: Ki213
    uint32_t anim_tick;  // by: Ki213
} IrJammerApp;  // by: Ki213
// by: Ki213
/* --- IR TX data callback: runs in interrupt context, so it has to be  // by: Ki213
 * fast and allocation-free. A tiny xorshift PRNG is both. Returning  // by: Ki213
 * random mark/space durations and random levels forever is what  // by: Ki213
 * actually produces the jamming noise - there's no "end of signal",  // by: Ki213
 * we just keep feeding it garbage until furi_hal_infrared_async_tx_stop()  // by: Ki213
 * is called from the main loop. */  // by: Ki213
static FuriHalInfraredTxGetDataState  // by: Ki213
    ir_jammer_tx_data_callback(void* context, uint32_t* duration, bool* level) {  // by: Ki213
    IrJammerApp* app = context;  // by: Ki213
// by: Ki213
    uint32_t x = app->noise_seed;  // by: Ki213
    x ^= x << 13;  // by: Ki213
    x ^= x >> 17;  // by: Ki213
    x ^= x << 5;  // by: Ki213
    app->noise_seed = x;  // by: Ki213
// by: Ki213
    *level = (x & 1) != 0;  // by: Ki213
    *duration = 80 + (x % 1200); /* 80-1280us: spans typical mark/space widths */  // by: Ki213
// by: Ki213
    return FuriHalInfraredTxGetDataStateOk;  // by: Ki213
}  // by: Ki213
// by: Ki213
static void ir_jammer_start(IrJammerApp* app) {  // by: Ki213
    if(app->jamming) return;  // by: Ki213
    app->noise_seed = 0xA5A5A5A5u ^ (uint32_t)furi_get_tick();  // by: Ki213
    furi_hal_infrared_set_tx_output(app->tx_pin);  // by: Ki213
    furi_hal_infrared_async_tx_set_data_isr_callback(ir_jammer_tx_data_callback, app);  // by: Ki213
    furi_hal_infrared_async_tx_start(kFrequencies[app->freq_index], DUTY_CYCLE);  // by: Ki213
    app->jamming = true;  // by: Ki213
}  // by: Ki213
// by: Ki213
static void ir_jammer_stop(IrJammerApp* app) {  // by: Ki213
    if(!app->jamming) return;  // by: Ki213
    furi_hal_infrared_async_tx_stop();  // by: Ki213
    app->jamming = false;  // by: Ki213
}  // by: Ki213
// by: Ki213
static void ir_jammer_draw_callback(Canvas* canvas, void* context) {  // by: Ki213
    IrJammerApp* app = context;  // by: Ki213
// by: Ki213
    canvas_clear(canvas);  // by: Ki213
// by: Ki213
    /* Title bar */  // by: Ki213
    canvas_set_font(canvas, FontPrimary);  // by: Ki213
    canvas_draw_str_aligned(canvas, 64, 2, AlignCenter, AlignTop, "IR Jammer");  // by: Ki213
    canvas_draw_line(canvas, 2, 13, 125, 13);  // by: Ki213
// by: Ki213
    /* Two-column info row: frequency on the left, TX source on the right */  // by: Ki213
    canvas_set_font(canvas, FontSecondary);  // by: Ki213
    char freq_line[20];  // by: Ki213
    snprintf(freq_line, sizeof(freq_line), "%lu kHz", (unsigned long)(kFrequencies[app->freq_index] / 1000));  // by: Ki213
    canvas_draw_str(canvas, 4, 24, "Freq:");  // by: Ki213
    canvas_draw_str(canvas, 4, 34, freq_line);  // by: Ki213
// by: Ki213
    const char* src = (app->tx_pin == FuriHalInfraredTxPinExtPA7) ? "External" : "Internal";  // by: Ki213
    canvas_draw_str(canvas, 68, 24, "Source:");  // by: Ki213
    canvas_draw_str(canvas, 68, 34, src);  // by: Ki213
    if(app->tx_pin == FuriHalInfraredTxPinExtPA7) {  // by: Ki213
        canvas_draw_str(canvas, 68, 43, "(pin 2 / PA7)");  // by: Ki213
    } else if(app->external_was_detected) {  // by: Ki213
        canvas_draw_str(canvas, 68, 43, "(module found!)");  // by: Ki213
    }  // by: Ki213
// by: Ki213
    canvas_draw_line(canvas, 2, 46, 125, 46);  // by: Ki213
// by: Ki213
    /* Status + animated "broadcast" rings while active */  // by: Ki213
    canvas_set_font(canvas, FontPrimary);  // by: Ki213
    canvas_draw_str_aligned(canvas, 30, 56, AlignCenter, AlignBottom, app->jamming ? "ON" : "OFF");  // by: Ki213
// by: Ki213
    if(app->jamming) {  // by: Ki213
        int cx = 95, cy = 54;  // by: Ki213
        for(int i = 0; i < 3; i++) {  // by: Ki213
            uint32_t r = ((app->anim_tick / 2 + (uint32_t)i * 5) % 15) + 2;  // by: Ki213
            canvas_draw_circle(canvas, cx, cy, (int)r);  // by: Ki213
        }  // by: Ki213
        canvas_draw_disc(canvas, cx, cy, 2);  // by: Ki213
    }  // by: Ki213
// by: Ki213
    elements_button_left(canvas, "Freq");  // by: Ki213
    elements_button_center(canvas, app->jamming ? "Stop" : "Start");  // by: Ki213
    elements_button_right(canvas, "Src");  // by: Ki213
}  // by: Ki213
// by: Ki213
static void ir_jammer_input_callback(InputEvent* event, void* context) {  // by: Ki213
    FuriMessageQueue* queue = context;  // by: Ki213
    furi_message_queue_put(queue, event, FuriWaitForever);  // by: Ki213
}  // by: Ki213
// by: Ki213
int32_t ir_jammer_app_main(void* p) {  // by: Ki213
    UNUSED(p);  // by: Ki213
// by: Ki213
    IrJammerApp* app = malloc(sizeof(IrJammerApp));  // by: Ki213
    memset(app, 0, sizeof(IrJammerApp));  // by: Ki213
    app->freq_index = DEFAULT_FREQ_INDEX;  // by: Ki213
// by: Ki213
    /* Auto-detect an external module on first run; fall back to the  // by: Ki213
     * internal LED if nothing is found. The person can still force  // by: Ki213
     * External manually with the right button if detection misses  // by: Ki213
     * their specific module's driver circuit. */  // by: Ki213
    FuriHalInfraredTxPin detected = furi_hal_infrared_detect_tx_output();  // by: Ki213
    app->external_was_detected = (detected == FuriHalInfraredTxPinExtPA7);  // by: Ki213
    app->tx_pin = detected;  // by: Ki213
// by: Ki213
    app->event_queue = furi_message_queue_alloc(8, sizeof(InputEvent));  // by: Ki213
// by: Ki213
    app->view_port = view_port_alloc();  // by: Ki213
    view_port_draw_callback_set(app->view_port, ir_jammer_draw_callback, app);  // by: Ki213
    view_port_input_callback_set(app->view_port, ir_jammer_input_callback, app->event_queue);  // by: Ki213
// by: Ki213
    app->gui = furi_record_open(RECORD_GUI);  // by: Ki213
    gui_add_view_port(app->gui, app->view_port, GuiLayerFullscreen);  // by: Ki213
// by: Ki213
    bool running = true;  // by: Ki213
    InputEvent event;  // by: Ki213
    while(running) {  // by: Ki213
        FuriStatus status = furi_message_queue_get(app->event_queue, &event, 100);  // by: Ki213
        if(status == FuriStatusOk && event.type == InputTypeShort) {  // by: Ki213
            switch(event.key) {  // by: Ki213
            case InputKeyOk:  // by: Ki213
                if(app->jamming) {  // by: Ki213
                    ir_jammer_stop(app);  // by: Ki213
                } else {  // by: Ki213
                    ir_jammer_start(app);  // by: Ki213
                }  // by: Ki213
                break;  // by: Ki213
            case InputKeyLeft:  // by: Ki213
                if(!app->jamming) {  // by: Ki213
                    app->freq_index = (app->freq_index + FREQ_COUNT - 1) % FREQ_COUNT;  // by: Ki213
                }  // by: Ki213
                break;  // by: Ki213
            case InputKeyRight:  // by: Ki213
                if(!app->jamming) {  // by: Ki213
                    app->tx_pin = (app->tx_pin == FuriHalInfraredTxPinExtPA7) ?  // by: Ki213
                                      FuriHalInfraredTxPinInternal :  // by: Ki213
                                      FuriHalInfraredTxPinExtPA7;  // by: Ki213
                }  // by: Ki213
                break;  // by: Ki213
            case InputKeyUp:  // by: Ki213
            case InputKeyDown:  // by: Ki213
                if(!app->jamming) {  // by: Ki213
                    size_t dir = (event.key == InputKeyUp) ? 1 : (FREQ_COUNT - 1);  // by: Ki213
                    app->freq_index = (app->freq_index + dir) % FREQ_COUNT;  // by: Ki213
                }  // by: Ki213
                break;  // by: Ki213
            case InputKeyBack:  // by: Ki213
                running = false;  // by: Ki213
                break;  // by: Ki213
            default:  // by: Ki213
                break;  // by: Ki213
            }  // by: Ki213
        }  // by: Ki213
        app->anim_tick++;  // by: Ki213
        view_port_update(app->view_port);  // by: Ki213
    }  // by: Ki213
// by: Ki213
    ir_jammer_stop(app);  // by: Ki213
// by: Ki213
    gui_remove_view_port(app->gui, app->view_port);  // by: Ki213
    furi_record_close(RECORD_GUI);  // by: Ki213
    view_port_free(app->view_port);  // by: Ki213
    furi_message_queue_free(app->event_queue);  // by: Ki213
    free(app);  // by: Ki213
// by: Ki213
    return 0;  // by: Ki213
}  // by: Ki213
