#include "subghz_frequency_analyzer.h"
#include "../subghz_i.h"

#include <math.h>
#include <furi.h>
#include <furi_hal.h>
#include <input/input.h>
#include <notification/notification_messages.h>
#include "../helpers/subghz_frequency_analyzer_worker.h"

#include <assets_icons.h>

typedef enum {
    SubGhzFrequencyAnalyzerStatusIDLE,
} SubGhzFrequencyAnalyzerStatus;

struct SubGhzFrequencyAnalyzer {
    View* view;
    SubGhzFrequencyAnalyzerWorker* worker;
    SubGhzFrequencyAnalyzerCallback callback;
    void* context;
    bool locked;
};

typedef struct {
    uint32_t frequency;
    float rssi;
    uint32_t history_frequency[3];
    bool signal;
} SubGhzFrequencyAnalyzerModel;

void subghz_frequency_analyzer_set_callback(
    SubGhzFrequencyAnalyzer* subghz_frequency_analyzer,
    SubGhzFrequencyAnalyzerCallback callback,
    void* context) {
    furi_assert(subghz_frequency_analyzer);
    furi_assert(callback);
    subghz_frequency_analyzer->callback = callback;
    subghz_frequency_analyzer->context = context;
}

void subghz_frequency_analyzer_draw_rssi(Canvas* canvas, float rssi) {
    uint8_t x = 20;
    uint8_t y = 64;
    uint8_t column_number = 0;
    if(rssi) {
        rssi = (rssi + 90) / 3;
        for(size_t i = 1; i < (uint8_t)rssi; i++) {
            if(i > 20) break;
            if(i % 4) {
                column_number++;
                canvas_draw_box(canvas, x + 2 * i, y - column_number, 2, 4 + column_number);
            }
        }
    }
}

static void subghz_frequency_analyzer_history_frequency_draw(
    Canvas* canvas,
    SubGhzFrequencyAnalyzerModel* model) {
    char buffer[64];
    uint8_t x = 66;
    uint8_t y = 43;

    canvas_set_font(canvas, FontKeyboard);
    for(uint8_t i = 0; i < 3; i++) {
        if(model->history_frequency[i]) {
            snprintf(
                buffer,
                sizeof(buffer),
                "%03ld.%03ld",
                model->history_frequency[i] / 1000000 % 1000,
                model->history_frequency[i] / 1000 % 1000);
            canvas_draw_str(canvas, x, y + i * 10, buffer);
        } else {
            canvas_draw_str(canvas, x, y + i * 10, "---.---");
        }
        canvas_draw_str(canvas, x + 44, y + i * 10, "MHz");
    }
    canvas_set_font(canvas, FontSecondary);
}

void subghz_frequency_analyzer_draw(Canvas* canvas, SubGhzFrequencyAnalyzerModel* model) {
    char buffer[64];

    canvas_set_color(canvas, ColorBlack);
    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str(canvas, 20, 8, "Frequency Analyzer");

    canvas_draw_str(canvas, 0, 64, "RSSI");
    subghz_frequency_analyzer_draw_rssi(canvas, model->rssi);

    subghz_frequency_analyzer_history_frequency_draw(canvas, model);

    //Frequency
    canvas_set_font(canvas, FontBigNumbers);
    snprintf(
        buffer,
        sizeof(buffer),
        "%03ld.%03ld",
        model->frequency / 1000000 % 1000,
        model->frequency / 1000 % 1000);
    if(model->signal) {
        canvas_draw_box(canvas, 4, 12, 121, 22);
        canvas_set_color(canvas, ColorWhite);
    } else {
    }

    canvas_draw_str(canvas, 8, 30, buffer);
    canvas_draw_icon(canvas, 96, 19, &I_MHz_25x11);
}

bool subghz_frequency_analyzer_input(InputEvent* event, void* context) {
    furi_assert(context);

    if(event->key == InputKeyBack) {
        return false;
    }

    return true;
}

void subghz_frequency_analyzer_pair_callback(
    void* context,
    uint32_t frequency,
    float rssi,
    bool signal) {
    SubGhzFrequencyAnalyzer* instance = context;
    if((rssi == 0.f) && (instance->locked)) {
        if(instance->callback) {
            instance->callback(SubGhzCustomEventSceneAnalyzerUnlock, instance->context);
        }
        //update history
        with_view_model(
            instance->view, (SubGhzFrequencyAnalyzerModel * model) {
                model->history_frequency[2] = model->history_frequency[1];
                model->history_frequency[1] = model->history_frequency[0];
                model->history_frequency[0] = model->frequency;
                return false;
            });
    } else if((rssi != 0.f) && (!instance->locked)) {
        if(instance->callback) {
            instance->callback(SubGhzCustomEventSceneAnalyzerLock, instance->context);
        }
    }

    instance->locked = (rssi != 0.f);
    with_view_model(
        instance->view, (SubGhzFrequencyAnalyzerModel * model) {
            model->rssi = rssi;
            model->frequency = frequency;
            model->signal = signal;
            return true;
        });
}

void subghz_frequency_analyzer_enter(void* context) {
    furi_assert(context);
    SubGhzFrequencyAnalyzer* instance = context;

    //Start worker
    instance->worker = subghz_frequency_analyzer_worker_alloc(instance->context);

    subghz_frequency_analyzer_worker_set_pair_callback(
        instance->worker,
        (SubGhzFrequencyAnalyzerWorkerPairCallback)subghz_frequency_analyzer_pair_callback,
        instance);

    subghz_frequency_analyzer_worker_start(instance->worker);

    with_view_model(
        instance->view, (SubGhzFrequencyAnalyzerModel * model) {
            model->rssi = 0;
            model->frequency = 0;
            model->history_frequency[2] = 0;
            model->history_frequency[1] = 0;
            model->history_frequency[0] = 0;
            return true;
        });
}

void subghz_frequency_analyzer_exit(void* context) {
    furi_assert(context);
    SubGhzFrequencyAnalyzer* instance = context;

    //Stop worker
    if(subghz_frequency_analyzer_worker_is_running(instance->worker)) {
        subghz_frequency_analyzer_worker_stop(instance->worker);
    }
    subghz_frequency_analyzer_worker_free(instance->worker);

    with_view_model(
        instance->view, (SubGhzFrequencyAnalyzerModel * model) {
            model->rssi = 0;
            return true;
        });
}

SubGhzFrequencyAnalyzer* subghz_frequency_analyzer_alloc() {
    SubGhzFrequencyAnalyzer* instance = malloc(sizeof(SubGhzFrequencyAnalyzer));

    // View allocation and configuration
    instance->view = view_alloc();
    view_allocate_model(
        instance->view, ViewModelTypeLocking, sizeof(SubGhzFrequencyAnalyzerModel));
    view_set_context(instance->view, instance);
    view_set_draw_callback(instance->view, (ViewDrawCallback)subghz_frequency_analyzer_draw);
    view_set_input_callback(instance->view, subghz_frequency_analyzer_input);
    view_set_enter_callback(instance->view, subghz_frequency_analyzer_enter);
    view_set_exit_callback(instance->view, subghz_frequency_analyzer_exit);

    with_view_model(
        instance->view, (SubGhzFrequencyAnalyzerModel * model) {
            model->rssi = 0;
            return true;
        });

    return instance;
}

void subghz_frequency_analyzer_free(SubGhzFrequencyAnalyzer* instance) {
    furi_assert(instance);

    view_free(instance->view);
    free(instance);
}

View* subghz_frequency_analyzer_get_view(SubGhzFrequencyAnalyzer* instance) {
    furi_assert(instance);
    return instance->view;
}
