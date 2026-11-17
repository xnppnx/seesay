/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#include "base_screen.h"

#include "asset_manager.h"
#include "bindings.h"
#include "theme.h"

namespace screen {

BaseScreen::BaseScreen(viewmodel::BaseViewModel& view_model, app::AssetManager& assets)
    : view_model_(view_model), assets_(assets) {}

BaseScreen::~BaseScreen() {
    title_bar_.reset();
    nav_bar_.reset();

    if (root_ && lv_obj_is_valid(root_)) {
        lv_obj_delete(root_);
    }
}

void BaseScreen::init() {
    if (root_) {
        return;
    }

    root_ = lv_obj_create(nullptr);
    lv_obj_remove_style_all(root_);
    lv_obj_clear_flag(root_, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_set_size(root_, view::kScreenWidth, view::kScreenHeight);
    reactive::bind_theme(root_, view_model_.dark_mode_subject(), reactive::ThemeRole::Screen);

    title_bar_ = std::make_unique<view::widgets::TitleBar>(root_, view_model_, assets_);
    title_bar_->build();

    nav_bar_ = std::make_unique<view::widgets::NavBar>(root_, view_model_, assets_);
    nav_bar_->build();
    // SeeSay uses SPACE/ENTER as its primary keys; the template's 4~8 nav
    // container is meaningless here and showed up as an empty bordered box.
    // Keep the object alive for the key router, but hide the whole bar.
    if (nav_bar_->root()) {
        lv_obj_add_flag(nav_bar_->root(), LV_OBJ_FLAG_HIDDEN);
    }

    content_ = lv_obj_create(root_);
    lv_obj_remove_style_all(content_);
    // The nav bar is hidden for SeeSay, so the content area uses the full
    // height below the title bar (bottom key hints live inside the content).
    lv_obj_set_size(content_, LV_PCT(100), view::kScreenHeight - view::kTitleBarHeight);
    lv_obj_align(content_, LV_ALIGN_TOP_MID, 0, view::kTitleBarHeight);
    lv_obj_clear_flag(content_, LV_OBJ_FLAG_SCROLLABLE);
    reactive::bind_theme(content_, view_model_.dark_mode_subject(), reactive::ThemeRole::Surface);

    build_content(content_);
}

lv_obj_t* BaseScreen::root() const {
    return root_;
}

viewmodel::BaseViewModel& BaseScreen::view_model() {
    return view_model_;
}

app::AssetManager& BaseScreen::assets() {
    return assets_;
}

} // namespace screen
