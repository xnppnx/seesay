/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#include "base_viewmodel.h"

#include <algorithm>
#include <cstdio>

namespace viewmodel {
namespace {

int page_to_int(model::AppPage page) {
    return static_cast<int>(page);
}

constexpr std::size_t kMaxDraftChars = 480;
constexpr std::size_t kMaxNotesShown = 12;

} // namespace

BaseViewModel::BaseViewModel()
    : title_subject_(model_.app_title()),
      greeting_subject_(model_.greeting()),
      dark_mode_subject_(model_.dark_mode()),
      current_page_subject_(page_to_int(model_.current_page())),
      counter_subject_(0),
      bold_text_subject_(false),
      info_visible_subject_(false),
      quit_requested_subject_(false),
      draft_subject_(""),
      notes_list_subject_(""),
      selected_index_subject_(0),
      status_subject_(""),
      ocr_result_subject_(""),
      ocr_busy_subject_(false) {}

BaseViewModel::~BaseViewModel() = default;

lv_subject_t* BaseViewModel::title_subject() {
    return title_subject_.native();
}

lv_subject_t* BaseViewModel::greeting_subject() {
    return greeting_subject_.native();
}

lv_subject_t* BaseViewModel::dark_mode_subject() {
    return dark_mode_subject_.native();
}

lv_subject_t* BaseViewModel::current_page_subject() {
    return current_page_subject_.native();
}

lv_subject_t* BaseViewModel::counter_subject() {
    return counter_subject_.native();
}

lv_subject_t* BaseViewModel::bold_text_subject() {
    return bold_text_subject_.native();
}

lv_subject_t* BaseViewModel::info_visible_subject() {
    return info_visible_subject_.native();
}

lv_subject_t* BaseViewModel::quit_requested_subject() {
    return quit_requested_subject_.native();
}

lv_subject_t* BaseViewModel::draft_subject() {
    return draft_subject_.native();
}

lv_subject_t* BaseViewModel::notes_list_subject() {
    return notes_list_subject_.native();
}

lv_subject_t* BaseViewModel::selected_index_subject() {
    return selected_index_subject_.native();
}

lv_subject_t* BaseViewModel::status_subject() {
    return status_subject_.native();
}

lv_subject_t* BaseViewModel::ocr_result_subject() {
    return ocr_result_subject_.native();
}

lv_subject_t* BaseViewModel::ocr_busy_subject() {
    return ocr_busy_subject_.native();
}

bool BaseViewModel::is_dark_mode() const {
    return model_.dark_mode();
}

void BaseViewModel::set_dark_mode(bool enabled) {
    model_.set_dark_mode(enabled);
    publish_all();
}

void BaseViewModel::toggle_dark_mode() {
    model_.toggle_dark_mode();
    publish_all();
}

model::AppPage BaseViewModel::current_page() const {
    return model_.current_page();
}

void BaseViewModel::show_apple_page() {
    model_.set_current_page(model::AppPage::Apple);
    publish_all();
}

void BaseViewModel::show_butter_page() {
    model_.set_current_page(model::AppPage::Butter);
    publish_all();
}

void BaseViewModel::toggle_page() {
    model_.toggle_page();
    publish_all();
}

void BaseViewModel::increment_counter() {
    counter_subject_.set(counter_subject_.value() + 1);
}

void BaseViewModel::decrement_counter() {
    const auto next = counter_subject_.value() - 1;
    counter_subject_.set(next < 0 ? 0 : next);
}

void BaseViewModel::toggle_bold_text() {
    bold_text_subject_.toggle();
}

void BaseViewModel::toggle_info() {
    info_visible_subject_.toggle();
}

void BaseViewModel::request_quit() {
    quit_requested_subject_.set(true);
}

void BaseViewModel::publish_all() {
    title_subject_.set(model_.app_title());
    greeting_subject_.set(model_.greeting());
    dark_mode_subject_.set(model_.dark_mode());
    current_page_subject_.set(page_to_int(model_.current_page()));
}

void BaseViewModel::publish_notes() {
    char buffer[1200];
    buffer[0] = '\0';
    std::size_t used = 0;

    const std::size_t total = notes_.count();
    if (total == 0) {
        std::snprintf(buffer, sizeof(buffer), "No notes yet.  Press N to write one.");
    } else {
        const std::size_t start = selected_index_subject_.value() >= 0
            ? static_cast<std::size_t>(selected_index_subject_.value())
            : 0;
        const std::size_t end = std::min(total, start + kMaxNotesShown);
        for (std::size_t i = start; i < end; ++i) {
            const char* marker = (i == static_cast<std::size_t>(selected_index_subject_.value())) ? "> " : "  ";
            const std::string& note = notes_.at(i);
            std::string flat = note;
            std::replace(flat.begin(), flat.end(), '\n', ' ');
            if (flat.size() > 34) {
                flat = flat.substr(0, 34);
                flat += "...";
            }
            const int written = std::snprintf(buffer + used,
                                              sizeof(buffer) - used,
                                              "%s%zu. %s\n",
                                              marker,
                                              i + 1,
                                              flat.c_str());
            if (written <= 0 || used + static_cast<std::size_t>(written) >= sizeof(buffer)) {
                break;
            }
            used += static_cast<std::size_t>(written);
        }
    }
    notes_list_subject_.set(buffer);
}

void BaseViewModel::load_notes() {
    notes_.load();
    const auto count = static_cast<int>(notes_.count());
    selected_index_subject_.set(count > 0 ? 0 : 0);
    publish_notes();
    set_status("Ready");
}

void BaseViewModel::begin_new_note() {
    editing_existing_ = false;
    clear_draft();
    set_status("New note");
    show_butter_page();
}

void BaseViewModel::open_note(std::size_t index) {
    if (index >= notes_.count()) {
        return;
    }
    editing_existing_ = true;
    draft_subject_.set(notes_.at(index).c_str());
    set_status("Edit note");
    show_butter_page();
}

void BaseViewModel::append_draft(const char* text) {
    if (!text) {
        return;
    }
    std::string next = draft();
    if (next.size() >= kMaxDraftChars) {
        set_status("Note is full");
        return;
    }
    next += text;
    draft_subject_.set(next.c_str());
}

void BaseViewModel::append_draft_char(char ch) {
    char text[2] = {ch, '\0'};
    append_draft(text);
}

void BaseViewModel::backspace_draft() {
    std::string next = draft();
    if (next.empty()) {
        return;
    }
    next.pop_back();
    draft_subject_.set(next.c_str());
}

void BaseViewModel::clear_draft() {
    draft_subject_.set("");
}

void BaseViewModel::save_current_note() {
    const std::string text = draft();
    if (text.empty()) {
        set_status("Empty note discarded");
    } else if (editing_existing_) {
        const auto index = static_cast<std::size_t>(selected_index_subject_.value());
        if (index < notes_.count()) {
            notes_.remove(index);
            notes_.append(text);
            set_status("Note updated");
        }
    } else {
        notes_.append(text);
        set_status("Note saved");
    }

    if (!notes_.save()) {
        set_status("Save failed!");
    }
    editing_existing_ = false;
    clear_draft();
    if (notes_.count() > 0 &&
        static_cast<std::size_t>(selected_index_subject_.value()) >= notes_.count()) {
        selected_index_subject_.set(static_cast<int>(notes_.count() - 1));
    }
    publish_notes();
    show_apple_page();
}

void BaseViewModel::delete_selected_note() {
    const auto index = static_cast<std::size_t>(selected_index_subject_.value());
    if (index >= notes_.count()) {
        set_status("Nothing to delete");
        return;
    }
    notes_.remove(index);
    notes_.save();
    if (static_cast<std::size_t>(selected_index_subject_.value()) >= notes_.count() && notes_.count() > 0) {
        selected_index_subject_.set(static_cast<int>(notes_.count() - 1));
    }
    publish_notes();
    set_status("Note deleted");
}

void BaseViewModel::move_selection(int delta) {
    const auto count = static_cast<int>(notes_.count());
    if (count == 0) {
        return;
    }
    int next = selected_index_subject_.value() + delta;
    if (next < 0) {
        next = 0;
    }
    if (next >= count) {
        next = count - 1;
    }
    selected_index_subject_.set(next);
    publish_notes();
}

void BaseViewModel::set_status(const char* status) {
    status_subject_.set(status ? status : "");
}

bool BaseViewModel::request_capture() {
    if (ocr_busy_subject_.value()) {
        set_status("Busy");
        return false;
    }
    if (!ocr_service_.start()) {
        set_status("Busy");
        return false;
    }
    ocr_busy_subject_.set(true);
    ocr_result_subject_.set("Taking photo...");
    set_status("SHOOTING");
    std::fprintf(stderr, "[seesay] capture requested\n");
    return true;
}

bool BaseViewModel::request_replay() {
    if (ocr_busy_subject_.value()) {
        return false;
    }
    if (!ocr_service_.replay()) {
        return false;
    }
    ocr_busy_subject_.set(true);
    set_status("REPLAY");
    std::fprintf(stderr, "[seesay] replay requested\n");
    return true;
}

void BaseViewModel::poll_ocr() {
    platform::OcrJobResult result;
    if (!ocr_service_.consume(result)) {
        return;
    }

    ocr_busy_subject_.set(false);
    switch (result.error) {
        case platform::OcrErrorCode::None:
            if (!result.display.empty()) {
                ocr_result_subject_.set(result.display.c_str());
            }
            set_status(result.unsure ? "UNSURE" : "DONE");
            std::fprintf(stderr, "[seesay] success: %zu chars\n", result.speech.size());
            break;
        case platform::OcrErrorCode::CameraFailed:
            ocr_result_subject_.set("Camera failed.");
            set_status("ERROR");
            std::fprintf(stderr, "[seesay] camera failed: %s\n", result.detail.c_str());
            break;
        case platform::OcrErrorCode::RecognitionFailed:
            if (!result.display.empty()) {
                ocr_result_subject_.set(result.display.c_str());
            } else {
                ocr_result_subject_.set("Recognition failed.");
            }
            set_status("ERROR");
            std::fprintf(stderr, "[seesay] recognition failed: %s\n", result.detail.c_str());
            break;
        default:
            ocr_result_subject_.set("Error.");
            set_status("ERROR");
            break;
    }
}

void BaseViewModel::clear_ocr_result() {
    ocr_result_subject_.set("");
    set_status("READY");
}

const std::string& BaseViewModel::draft() const {
    static const std::string empty;
    const char* value = lv_subject_get_string(const_cast<lv_subject_t*>(draft_subject_.native()));
    // StringSubject owns its buffer; return a stable reference via a thread-local copy.
    thread_local std::string cache;
    cache = value ? value : "";
    (void)empty;
    return cache;
}

std::size_t BaseViewModel::selected_index() const {
    const int value = selected_index_subject_.value();
    return value < 0 ? 0 : static_cast<std::size_t>(value);
}

std::size_t BaseViewModel::notes_count() const {
    return notes_.count();
}

} // namespace viewmodel
