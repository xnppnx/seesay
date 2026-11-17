/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "subjects.h"
#include "base_model.h"
#include "notes_model.h"
#include "ocr_service.h"

#include "lvgl.h"

namespace viewmodel {

class BaseViewModel {
public:
    BaseViewModel();
    ~BaseViewModel();

    BaseViewModel(const BaseViewModel&) = delete;
    BaseViewModel& operator=(const BaseViewModel&) = delete;

    lv_subject_t* title_subject();
    lv_subject_t* greeting_subject();
    lv_subject_t* dark_mode_subject();
    lv_subject_t* current_page_subject();
    lv_subject_t* counter_subject();
    lv_subject_t* bold_text_subject();
    lv_subject_t* info_visible_subject();
    lv_subject_t* quit_requested_subject();
    lv_subject_t* draft_subject();
    lv_subject_t* notes_list_subject();
    lv_subject_t* selected_index_subject();
    lv_subject_t* status_subject();
    lv_subject_t* ocr_result_subject();
    lv_subject_t* ocr_busy_subject();

    bool is_dark_mode() const;
    void set_dark_mode(bool enabled);
    void toggle_dark_mode();

    model::AppPage current_page() const;
    void show_apple_page();
    void show_butter_page();
    void toggle_page();
    void increment_counter();
    void decrement_counter();
    void toggle_bold_text();
    void toggle_info();
    void request_quit();

    // Notes actions.
    void load_notes();
    void begin_new_note();
    void open_note(std::size_t index);
    void append_draft(const char* text);
    void append_draft_char(char ch);
    void backspace_draft();
    void clear_draft();
    void save_current_note();
    void delete_selected_note();
    void move_selection(int delta);
    void set_status(const char* status);

    // Voice-loop actions.
    bool request_capture();
    bool request_replay();
    void poll_ocr();
    void poll_stage();  // reflect pipeline stage (ANALYZING etc.) on screen
    void clear_ocr_result();

    const std::string& draft() const;
    std::size_t selected_index() const;
    std::size_t notes_count() const;

private:
    void publish_all();
    void publish_notes();

    model::BaseModel model_;
    model::NotesModel notes_;
    reactive::StringSubject<32> title_subject_;
    reactive::StringSubject<32> greeting_subject_;
    reactive::BoolSubject dark_mode_subject_;
    reactive::IntSubject current_page_subject_;
    reactive::IntSubject counter_subject_;
    reactive::BoolSubject bold_text_subject_;
    reactive::BoolSubject info_visible_subject_;
    reactive::BoolSubject quit_requested_subject_;
    reactive::StringSubject<512> draft_subject_;
    reactive::StringSubject<1024> notes_list_subject_;
    reactive::IntSubject selected_index_subject_;
    reactive::StringSubject<64> status_subject_;
    reactive::StringSubject<2048> ocr_result_subject_;
    reactive::BoolSubject ocr_busy_subject_;
    bool editing_existing_{false};
    platform::OcrService ocr_service_;
};

} // namespace viewmodel
