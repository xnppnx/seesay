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

#include <memory>

namespace app {

// Owns keyboard interaction for the notes app. Registered as the global key
// listener so it receives every key press, long press and release.
class NotesController {
public:
    NotesController(viewmodel::BaseViewModel& view_model, AssetManager& assets);
    ~NotesController();

    NotesController(const NotesController&) = delete;
    NotesController& operator=(const NotesController&) = delete;

private:
    bool handle_key(uint32_t key, bool long_pressed);
    void handle_release(uint32_t key);

    static bool global_key_listener(uint32_t key,
                                    const char* key_name,
                                    bool long_pressed,
                                    void* user_data);
    static void key_release_listener(uint32_t key, const char* key_name, void* user_data);

    viewmodel::BaseViewModel& view_model_;
    std::unique_ptr<view::widgets::Toast> toast_;
    std::unique_ptr<view::widgets::HelpPopup> help_popup_;
    uint32_t exit_key_pressed_{0};
    bool exit_armed_{false};
};

} // namespace app
