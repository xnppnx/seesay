/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#include "ocr_controller.h"

#include "asset_manager.h"
#include "linux_input.h"

namespace app {

OcrController::OcrController(viewmodel::BaseViewModel& view_model, AssetManager& assets)
    : view_model_(view_model),
      toast_(std::make_unique<view::widgets::Toast>(lv_layer_top(), view_model, assets)),
      help_popup_(std::make_unique<view::widgets::HelpPopup>(lv_layer_top(), view_model, assets)) {
    toast_->build();
    help_popup_->build();
    platform::set_global_key_listener(global_key_listener, this);
    poll_timer_ = lv_timer_create(poll_timer_cb, 100, this);
}

OcrController::~OcrController() {
    if (poll_timer_) {
        lv_timer_delete(poll_timer_);
        poll_timer_ = nullptr;
    }
    platform::clear_global_key_listener(global_key_listener, this);
}

bool OcrController::handle_key(uint32_t key, bool long_pressed) {
    // Long press ESC = return to HOME (system behaviour). Do not intercept it;
    // returning false lets the system handle it (project book 6.1).
    if (long_pressed) {
        return false;
    }

    // fn+H is the official help key. A bare 'h' is intentionally NOT bound.
    if (key == platform::kKeyHelp) {
        toast_->hide();
        if (help_popup_->visible()) {
            help_popup_->hide();
        } else {
            help_popup_->show(view_model_.current_page());
        }
        return true;
    }

    if (help_popup_->visible()) {
        if (key == LV_KEY_ESC) {
            help_popup_->hide();
        }
        return true;
    }

    // SPACE = take a photo and speak the result.
    if (key == ' ') {
        toast_->hide();
        view_model_.request_capture();
        return true;
    }

    // Enter / OK = replay the last announcement. ESC is intentionally NOT a
    // replay key: a long-press ESC must go straight to the system HOME with
    // no side effect, and a short-press ESC does nothing here (project book
    // 6.1: short ESC = ESC, long ESC = HOME system behaviour).
    if (key == LV_KEY_ENTER) {
        view_model_.request_replay();
        return true;
    }

    // 'c' clears the current result and returns to READY.
    if (key == 'c' || key == 'C') {
        view_model_.clear_ocr_result();
        return true;
    }

    return false;
}

bool OcrController::global_key_listener(uint32_t key,
                                        const char*,
                                        bool long_pressed,
                                        void* user_data) {
    auto* controller = static_cast<OcrController*>(user_data);
    return controller && controller->handle_key(key, long_pressed);
}

void OcrController::poll_timer_cb(lv_timer_t* timer) {
    auto* controller = static_cast<OcrController*>(lv_timer_get_user_data(timer));
    if (controller) {
        controller->view_model_.poll_stage();
        controller->view_model_.poll_ocr();
    }
}

} // namespace app
