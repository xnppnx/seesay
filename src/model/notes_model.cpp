/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#include "notes_model.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace model {
namespace {

std::string encode(const std::string& text) {
    std::string out;
    out.reserve(text.size());
    for (char ch : text) {
        switch (ch) {
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': break;
            default: out += ch; break;
        }
    }
    return out;
}

std::string decode(const std::string& text) {
    std::string out;
    out.reserve(text.size());
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '\\' && i + 1 < text.size()) {
            const char next = text[++i];
            if (next == 'n') {
                out += '\n';
            } else if (next == '\\') {
                out += '\\';
            } else {
                out += next;
            }
        } else {
            out += text[i];
        }
    }
    return out;
}

} // namespace

NotesModel::NotesModel() : path_(default_path()) {}

std::string NotesModel::default_path() {
    if (const char* home = std::getenv("HOME")) {
        return (std::filesystem::path(home) / ".config" / "seesay" / "notes.txt").string();
    }
    return "notes.txt";
}

void NotesModel::set_path(std::string path) {
    path_ = std::move(path);
}

const std::string& NotesModel::path() const {
    return path_;
}

void NotesModel::load() {
    notes_.clear();
    std::ifstream input(path_);
    if (!input) {
        return;
    }
    std::string line;
    while (std::getline(input, line)) {
        if (!line.empty()) {
            notes_.push_back(decode(line));
        }
    }
}

bool NotesModel::save() const {
    std::error_code error;
    const std::filesystem::path file(path_);
    const auto parent = file.parent_path();
    if (!parent.empty()) {
        std::filesystem::create_directories(parent, error);
        if (error) {
            return false;
        }
    }
    std::ofstream output(path_, std::ios::trunc);
    if (!output) {
        return false;
    }
    for (const auto& note : notes_) {
        output << encode(note) << '\n';
    }
    return static_cast<bool>(output);
}

std::size_t NotesModel::count() const {
    return notes_.size();
}

const std::string& NotesModel::at(std::size_t index) const {
    return notes_[index];
}

void NotesModel::append(const std::string& text) {
    notes_.push_back(text);
}

void NotesModel::remove(std::size_t index) {
    if (index < notes_.size()) {
        notes_.erase(notes_.begin() + static_cast<std::ptrdiff_t>(index));
    }
}

} // namespace model
