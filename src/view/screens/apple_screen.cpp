/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 * SPDX-License-Identifier: MIT
 *
 * SeeSay main screen - implements project book chapter 5:
 *   top status dot + state label, middle = thumbnail (left) + result text
 *   (right), bottom single-line key hints. Audio is the real interface;
 *   the screen is a development aid.
 */
#include "apple_screen.h"

#include "asset_manager.h"
#include "base_viewmodel.h"
#include "bindings.h"
#include "theme.h"

#include <cstdlib>
#include <string>

namespace screen {
namespace {

// Project book 5.3/5.4: colored status dot (green=ready/done, yellow=working,
// red=error) plus a short state word. Also surfaces ANALYZING on screen.
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

// Resolve the app data dir, matching the pipeline / ocr_service logic.
std::string thumb_lvgl_path() {
    std::string dir;
    if (const char* d = std::getenv("SEESAY_DATA_DIR")) {
        if (*d) dir = d;
    }
    if (dir.empty()) {
        if (const char* xdg = std::getenv("XDG_DATA_HOME")) {
            if (*xdg) dir = std::string(xdg) + "/seesay";
        }
    }
    if (dir.empty()) {
        const char* home = std::getenv("HOME");
        dir = (home && *home) ? (std::string(home) + "/.local/share/seesay")
                              : "/tmp/seesay";
    }
    // LVGL stdio driver uses drive letter 'A' mapped to "/", so an absolute
    // POSIX path must be prefixed with "A:".
    return "A:" + dir + "/thumb.png";
}

// Show/hide the thumbnail as the job state changes. On DONE/REPORTING the
// PNG exists (written by the pipeline); otherwise hide it so the text can
// use the full width.
void thumb_observer(lv_observer_t* observer, lv_subject_t* subject) {
    auto* image = static_cast<lv_obj_t*>(lv_observer_get_user_data(observer));
    if (!image) {
        return;
    }
    const char* value = lv_subject_get_string(subject);
    const std::string state(value ? value : "");
    // Show as soon as ANALYZING: by then the pipeline has already written the
    // thumbnail right after capture, so the photo appears ~1s after the press
    // while recognition continues in the background (project book 7.1).
    const bool show = (state == "ANALYZING" || state == "DONE" ||
                       state == "REPORTING" || state == "UNSURE");
    if (show) {
        lv_image_set_src(image, thumb_lvgl_path().c_str());
        lv_obj_remove_flag(image, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(image, LV_OBJ_FLAG_HIDDEN);
    }
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
    // Spread the three blocks so the key-hint line sits at the very bottom.
    lv_obj_set_flex_align(column, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_all(column, 5, 0);
    lv_obj_clear_flag(column, LV_OBJ_FLAG_SCROLLABLE);

    auto* font = assets().load_standard_font(14, app::StandardFontWeight::Regular);
    auto* small_font = assets().load_standard_font(12, app::StandardFontWeight::Regular);

    // Status line: a colored dot + state label (project book 5.3/5.4).
    auto* status = lv_label_create(column);
    lv_obj_set_width(status, LV_PCT(100));
    lv_label_set_long_mode(status, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_font(status, small_font ? small_font : &lv_font_montserrat_12, 0);
    lv_subject_add_observer_obj(view_model().status_subject(), status_observer, status, nullptr);

    // Middle row: thumbnail (left) + result text (right), project book 5.2.
    auto* row = lv_obj_create(column);
    lv_obj_remove_style_all(row);
    lv_obj_set_width(row, LV_PCT(100));
    lv_obj_set_height(row, 78);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(row, 6, 0);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

    // Thumbnail: 120x90 area, image scaled to fit (CONTAIN) without distortion.
    auto* image = lv_image_create(row);
    lv_obj_set_size(image, 120, 90);
    lv_image_set_inner_align(image, LV_IMAGE_ALIGN_CONTAIN);
    lv_obj_add_flag(image, LV_OBJ_FLAG_HIDDEN);  // hidden until a photo exists
    lv_subject_add_observer_obj(view_model().status_subject(), thumb_observer,
                                nullptr, image);

    // Result text: scrollable, wraps long Chinese/English text, right of image.
    auto* result = lv_label_create(row);
    lv_obj_set_width(result, 180);
    lv_obj_set_height(result, 78);
    lv_label_set_long_mode(result, LV_LABEL_LONG_WRAP);
    lv_label_bind_text(result, view_model().ocr_result_subject(), nullptr);
    lv_obj_set_style_text_font(result, font ? font : &lv_font_montserrat_14, 0);
    lv_obj_add_flag(result, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(result, LV_OBJ_FLAG_SCROLL_CHAIN);
    reactive::bind_theme(result, view_model().dark_mode_subject(), reactive::ThemeRole::Text);

    // Bottom key hints (project book 5.2): one full-width line, no icon row.
    auto* help = lv_label_create(column);
    lv_obj_set_width(help, LV_PCT(100));
    lv_label_set_long_mode(help, LV_LABEL_LONG_CLIP);
    lv_label_set_text(help, "SPACE shoot  OK replay  fn+H help");
    lv_obj_set_style_text_font(help, small_font ? small_font : &lv_font_montserrat_12, 0);
    reactive::bind_theme(help, view_model().dark_mode_subject(), reactive::ThemeRole::Text);
}

} // namespace screen
