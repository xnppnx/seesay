/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace model {

// Stores plain-text notes in a single file, one note per line.
// Uses '\n' inside a note encoded as "\n" so a note stays on one physical line.
class NotesModel {
public:
    NotesModel();

    void load();
    bool save() const;

    std::size_t count() const;
    const std::string& at(std::size_t index) const;

    void append(const std::string& text);
    void remove(std::size_t index);

    void set_path(std::string path);
    const std::string& path() const;

    static std::string default_path();

private:
    std::vector<std::string> notes_;
    std::string path_;
};

} // namespace model
