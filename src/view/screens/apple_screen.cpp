/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 * SPDX-License-Identifier: MIT
 *
 * SeeSay main screen - implements project book chapter 5:
 *   top status dot + state label, middle result area, bottom key hints.
 * The screen is a development aid only; audio is the real interface.
 */
#include "apple_screen.h"

#include "asset_manager.h"
#include "base_viewmodel.h"
#include "bindings.h"
#include "theme.h"

#include <string>

namespace screen {
namespace {

// Project book 5.3/5.4: top status shown as a colored dot (green=ready/done,
// yellow=working, red=error) plus a short state word. The leading dot is a
// Unicode glyph so it inherits the label color, keeping dot and word in sync.
// Also surfaces the ANALYZING state on screen (previously audio-only).
void status_observer(lv_observer_t* observer, lv_subject_t* subject) {
    auto* label = static_cast<lv_obj_t*>(lv_observer_get_target_obj(observer));
    if (!label) {
        return;
    }
    const char* value = lv_subject_get_string(subject);
    if (!value) {
        value = "";
    }
    const std::string state(value);

    // dot glyph: filled circle. Text color set below by role.
    struct StateInfo {
        const char* code;
        const char* shown;
        int role;  // 0=success(green) 1=warning(yellow) 2=error(red)
    };
    static const StateInfo kStates[] = {
        {"READY",     "\xE2\x97\x8F  READY",     0},
        {"DONE",      "\xE2\x97\x8F  DONE",      0},
        {"UNSURE",    "\xE2\x97\x8F  UNSURE",    2},
        {"ERROR",     "\xE2\x97\x8F  ERROR",     2},
        {"SHOOTING",  "\xE2\x97\x8F  SHOOTING",  1},
        {"ANALYZING", "\xE2\x97\x8F  ANALYZING", 1},
        {"REPLAY",    "\xE2\x97\x8F  REPLAY",    1},
        {"REPORTING", "\xE2\x97\x8F  SPEAKING",  1},
    };

    const char* shown = "\xE2\x97\x8F  READY";
    int role = 1;
    for (const auto& s : kStates) {
        if (state == s.code) {
            shown = s.shown;
            role = s.role;
            break;
        }
    }
    lv_label_set_text(label, shown);

    const auto p = view::palette(false);
    lv_color_t color = (role == 0) ? p.success : (role == 2) ? p.error : p.warning;
    lv_obj_set_style_text_color(label, color, 0);
}

} // namespace

AppleScreen::AppleScreen(viewmodel::BaseViewModel& view_model, app::AssetManager& assets)
    : BaseScreen(view_model, assets) {
    init();
}

void AppleScreen::build_content(lv_obj_t* content) {
    auto* column = lv_obj_create(content);
    lv_obj_remove_style_all(column);
    lv_obj_set_size(column, LV_PCT(100), LV_PCT(100));
    lv_obj_set_flex_flow(column, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(column, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_all(column, 6, 0);
    lv_obj_set_style_pad_row(column, 3, 0);
    lv_obj_clear_flag(column, LV_OBJ_FLAG_SCROLLABLE);

    auto* font = assets().load_standard_font(14, app::StandardFontWeight::Regular);
    auto* small_font = assets().load_standard_font(12, app::StandardFontWeight::Regular);

    // Status line: a colored dot + state label (project book 5.3/5.4).
    auto* status = lv_label_create(column);
    lv_obj_set_width(status, LV_PCT(100));
    lv_label_set_long_mode(status, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_font(status, small_font ? small_font : &lv_font_montserrat_12, 0);
    lv_subject_add_observer_obj(view_model().status_subject(), status_observer, status, nullptr);

    // Result area: scrollable, wraps long Chinese/English text.
    auto* result = lv_label_create(column);
    lv_obj_set_width(result, LV_PCT(100));
    lv_obj_set_height(result, 64);
    lv_label_set_long_mode(result, LV_LABEL_LONG_WRAP);
    lv_label_bind_text(result, view_model().ocr_result_subject(), nullptr);
    lv_obj_set_style_text_font(result, font ? font : &lv_font_montserrat_14, 0);
    lv_obj_add_flag(result, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(result, LV_OBJ_FLAG_SCROLL_CHAIN);
    reactive::bind_theme(result, view_model().dark_mode_subject(), reactive::ThemeRole::Text);

    // Bottom key hints (project book 5.2): one full-width line, no icon row
    // overlapping it. NavBar's template icons are hidden for this app.
    auto* help = lv_label_create(column);
    lv_obj_set_width(help, LV_PCT(100));
    lv_label_set_long_mode(help, LV_LABEL_LONG_CLIP);
    lv_label_set_text(help, "SPACE shoot  OK replay  fn+H help");
    lv_obj_set_style_text_font(help, small_font ? small_font : &lv_font_montserrat_12, 0);
    reactive::bind_theme(help, view_model().dark_mode_subject(), reactive::ThemeRole::Text);
    lv_obj_align(help, LV_ALIGN_BOTTOM_MID, 0, 0);
}

} // namespace screen
