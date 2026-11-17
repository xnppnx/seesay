/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#include "notes_controller.h"

#include "asset_manager.h"
#include "logger.h"
#include "linux_input.h"

namespace app {
namespace {

constexpr uint32_t kKeyNewNote = 'n';
constexpr uint32_t kKeyDelete = 'd';
constexpr uint32_t kKeySave1 = 's';

bool is_printable(uint32_t key) {
    return (key >= 'a' && key <= 'z') || (key >= 'A' && key <= 'Z') ||
           (key >= '0' && key <= '9') || key == ' ' || key == '.' || key == ',' ||
           key == '/' || key == '-' || key == ';' || key == '\'';
}

} // namespace

NotesController::NotesController(viewmodel::BaseViewModel& view_model, AssetManager& assets)
    : view_model_(view_model),
      toast_(std::make_unique<view::widgets::Toast>(lv_layer_top(), view_model, assets)),
      help_popup_(std::make_unique<view::widgets::HelpPopup>(lv_layer_top(), view_model, assets)) {
    toast_->build();
    help_popup_->build();
    platform::set_global_key_listener(global_key_listener, this);
    platform::set_key_release_listener(key_release_listener, this);
}

NotesController::~NotesController() {
    platform::clear_key_release_listener(key_release_listener, this);
    platform::clear_global_key_listener(global_key_listener, this);
}

bool NotesController::handle_key(uint32_t key, bool long_pressed) {
    const bool editing = view_model_.current_page() == model::AppPage::Butter;

    // Long press ESC always quits the app (official contract).
    if (long_pressed) {
        if (key == LV_KEY_ESC) {
            toast_->hide();
            view_model_.request_quit();
            return true;
        }
        return false;
    }

    // Help key toggles the help popup.
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

    // ESC short press: from editor go back to list without saving.
    if (key == LV_KEY_ESC) {
        if (editing) {
            view_model_.set_status("Discarded");
            view_model_.show_apple_page();
        }
        return true;
    }

    // ---- Editing page: typing ----
    if (editing) {
        if (key == LV_KEY_ENTER) {
            toast_->hide();
            view_model_.save_current_note();
            return true;
        }
        if (key == LV_KEY_BACKSPACE) {
            view_model_.backspace_draft();
            return true;
        }
        if (is_printable(key)) {
            view_model_.append_draft_char(static_cast<char>(key));
            return true;
        }
        return false;
    }

    // ---- List page: navigation and actions ----
    if (key == kKeyNewNote || key == LV_KEY_ENTER) {
        toast_->hide();
        view_model_.begin_new_note();
        return true;
    }
    if (key == kKeySave1) {
        // Open the currently selected note for editing.
        if (view_model_.notes_count() > 0) {
            view_model_.open_note(view_model_.selected_index());
        } else {
            view_model_.set_status("No notes");
        }
        return true;
    }
    if (key == kKeyDelete) {
        view_model_.delete_selected_note();
        return true;
    }
    if (key == LV_KEY_UP || key == LV_KEY_LEFT || key == 'z' || key == 'w') {
        view_model_.move_selection(-1);
        return true;
    }
    if (key == LV_KEY_DOWN || key == LV_KEY_RIGHT || key == 'x' || key == 'c') {
        view_model_.move_selection(1);
        return true;
    }

    return false;
}

void NotesController::handle_release(uint32_t key) {
    if (key == exit_key_pressed_) {
        exit_key_pressed_ = 0;
        exit_armed_ = false;
    }
}

bool NotesController::global_key_listener(uint32_t key,
                                          const char*,
                                          bool long_pressed,
                                          void* user_data) {
    auto* controller = static_cast<NotesController*>(user_data);
    return controller && controller->handle_key(key, long_pressed);
}

void NotesController::key_release_listener(uint32_t key, const char*, void* user_data) {
    auto* controller = static_cast<NotesController*>(user_data);
    if (controller) {
        controller->handle_release(key);
    }
}

} // namespace app
