/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "asset_manager.h"
#include "base_viewmodel.h"
#include "help_popup.h"
#include "toast.h"

#include "lvgl.h"

#include <memory>

namespace app {

// Owns keyboard interaction for the snap-and-read app. Registered as the
// global key listener so it receives every key press, long press and release.
// Also drives a small timer that polls the OCR worker for finished jobs.
class OcrController {
public:
    OcrController(viewmodel::BaseViewModel& view_model, AssetManager& assets);
    ~OcrController();

    OcrController(const OcrController&) = delete;
    OcrController& operator=(const OcrController&) = delete;

private:
    bool handle_key(uint32_t key, bool long_pressed);

    static bool global_key_listener(uint32_t key,
                                    const char* key_name,
                                    bool long_pressed,
                                    void* user_data);
    static void poll_timer_cb(lv_timer_t* timer);

    viewmodel::BaseViewModel& view_model_;
    std::unique_ptr<view::widgets::Toast> toast_;
    std::unique_ptr<view::widgets::HelpPopup> help_popup_;
    lv_timer_t* poll_timer_{nullptr};
};

} // namespace app
