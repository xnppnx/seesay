/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#include "butter_screen.h"

#include "asset_manager.h"
#include "bindings.h"

namespace screen {

ButterScreen::ButterScreen(viewmodel::BaseViewModel& view_model, app::AssetManager& assets)
    : BaseScreen(view_model, assets) {
    init();
}

void ButterScreen::build_content(lv_obj_t* content) {
    auto* column = lv_obj_create(content);
    lv_obj_remove_style_all(column);
    lv_obj_set_size(column, LV_PCT(100), LV_PCT(100));
    lv_obj_set_flex_flow(column, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(column, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_all(column, 6, 0);
    lv_obj_set_style_pad_row(column, 4, 0);
    lv_obj_clear_flag(column, LV_OBJ_FLAG_SCROLLABLE);

    auto* font = assets().load_standard_font(14, app::StandardFontWeight::Regular);
    auto* small_font = assets().load_standard_font(11, app::StandardFontWeight::Regular);

    auto* prompt = lv_label_create(column);
    lv_label_set_text(prompt, "Writing note:");
    lv_obj_set_style_text_font(prompt, small_font ? small_font : &lv_font_montserrat_12, 0);
    reactive::bind_theme(prompt, view_model().dark_mode_subject(), reactive::ThemeRole::Text);

    // The typed text, wrapping onto multiple lines.
    auto* editor = lv_label_create(column);
    lv_obj_set_width(editor, LV_PCT(100));
    lv_label_set_long_mode(editor, LV_LABEL_LONG_WRAP);
    lv_label_bind_text(editor, view_model().draft_subject(), nullptr);
    lv_obj_set_style_text_font(editor, font ? font : &lv_font_montserrat_14, 0);
    reactive::bind_theme(editor, view_model().dark_mode_subject(), reactive::ThemeRole::Text);

    // Blinking cursor line.
    auto* cursor = lv_label_create(column);
    lv_obj_set_width(cursor, LV_PCT(100));
    lv_label_set_text(cursor, "_");
    lv_obj_set_style_text_font(cursor, font ? font : &lv_font_montserrat_14, 0);
    reactive::bind_theme(cursor, view_model().dark_mode_subject(), reactive::ThemeRole::Text);
    lv_obj_add_flag(cursor, LV_OBJ_FLAG_HIDDEN);

    auto* help = lv_label_create(column);
    lv_obj_set_width(help, LV_PCT(100));
    lv_label_set_long_mode(help, LV_LABEL_LONG_WRAP);
    lv_label_set_text(help, "Type  Del backspace  Enter save\nESC discard  Hold ESC quit");
    lv_obj_set_style_text_font(help, small_font ? small_font : &lv_font_montserrat_12, 0);
    reactive::bind_theme(help, view_model().dark_mode_subject(), reactive::ThemeRole::Text);
}

} // namespace screen
