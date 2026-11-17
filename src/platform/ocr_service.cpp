/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#include "ocr_service.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <thread>

namespace platform {
namespace {

// Paths can be redirected by env vars (used by tests and by packaging).
const char* env_or(const char* name, const char* fallback) {
    const char* value = std::getenv(name);
    return (value && *value) ? value : fallback;
}

std::string pipeline_script() {
    return std::string(env_or("SEESAY_RUNTIME_DIR", "/usr/share/seesay/runtime")) +
           "/seesay_pipeline.py";
}

// Wrap a value in single quotes for safe shell use.
std::string shell_quote(const std::string& value) {
    std::string out = "'";
    for (char c : value) {
        if (c == '\'') {
            out += "'\\''";
        } else {
            out += c;
        }
    }
    out += "'";
    return out;
}

std::string read_file(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return {};
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

std::string json_string(const std::string& json, const char* key) {
    const std::string needle = std::string("\"") + key + "\"";
    const auto key_pos = json.find(needle);
    if (key_pos == std::string::npos) {
        return {};
    }
    const auto colon = json.find(':', key_pos + needle.size());
    if (colon == std::string::npos) {
        return {};
    }
    const auto open = json.find('"', colon + 1);
    if (open == std::string::npos) {
        return {};
    }
    std::string value;
    bool escaped = false;
    for (std::size_t i = open + 1; i < json.size(); ++i) {
        const char c = json[i];
        if (escaped) {
            switch (c) {
                case 'n': value += '\n'; break;
                case 'r': value += '\r'; break;
                case 't': value += '\t'; break;
                case '"': value += '"'; break;
                case '\\': value += '\\'; break;
                case '/': value += '/'; break;
                default: value += c; break;
            }
            escaped = false;
            continue;
        }
        if (c == '\\') {
            escaped = true;
            continue;
        }
        if (c == '"') {
            break;
        }
        value += c;
    }
    return value;
}

bool json_bool(const std::string& json, const char* key, bool fallback) {
    const std::string needle = std::string("\"") + key + "\"";
    const auto key_pos = json.find(needle);
    if (key_pos == std::string::npos) {
        return fallback;
    }
    const auto colon = json.find(':', key_pos + needle.size());
    if (colon == std::string::npos) {
        return fallback;
    }
    const auto tail = json.substr(colon + 1);
    if (tail.find("true") == 0) {
        return true;
    }
    if (tail.find("false") == 0) {
        return false;
    }
    return fallback;
}

const char* python_bin() {
    return env_or("SEESAY_PYTHON", "python3");
}

} // namespace

OcrService::OcrService() {
    worker_ = std::thread([this] { run(); });
}

OcrService::~OcrService() {
    stop_.store(true);
    if (worker_.joinable()) {
        worker_.join();
    }
}

bool OcrService::start() {
    bool expected = false;
    if (!busy_.compare_exchange_strong(expected, true)) {
        return false;
    }
    replay_.store(false);
    return true;
}

bool OcrService::replay() {
    bool expected = false;
    if (!busy_.compare_exchange_strong(expected, true)) {
        return false;
    }
    replay_.store(true);
    return true;
}

bool OcrService::consume(OcrJobResult& out) {
    std::lock_guard<std::mutex> lock(result_mutex_);
    if (!has_result_) {
        return false;
    }
    out = pending_result_;
    has_result_ = false;
    return true;
}

void OcrService::run() {
    while (!stop_.load()) {
        if (!busy_.load()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(60));
            continue;
        }

        OcrJobResult result = do_job();
        {
            std::lock_guard<std::mutex> lock(result_mutex_);
            pending_result_ = std::move(result);
            has_result_ = true;
        }
        busy_.store(false);
    }
}

OcrJobResult OcrService::do_job() {
    OcrJobResult result;

    const std::string result_path = env_or("SEESAY_RESULT", "/tmp/seesay_result.json");
    std::error_code ec;
    std::filesystem::remove(result_path, ec);

    const bool is_replay = replay_.load();
    const std::string arg = is_replay ? "--replay" : "";
    std::string command = std::string(shell_quote(python_bin())) + " " +
                          shell_quote(pipeline_script());
    if (!arg.empty()) {
        command += " " + shell_quote(arg);
    }
    command += " >/dev/null 2>" + shell_quote("/tmp/seesay_pipeline_err.txt");

    const int status = std::system(command.c_str());
    const std::string payload = read_file(result_path);
    const std::string err = read_file("/tmp/seesay_pipeline_err.txt");

    if (is_replay) {
        // Replay has no result JSON; just report success/failure.
        result.error = (status == 0) ? OcrErrorCode::None : OcrErrorCode::RecognitionFailed;
        if (status != 0) {
            result.detail = err.size() > 160 ? err.substr(err.size() - 160) : err;
        }
        return result;
    }

    if (payload.empty()) {
        result.error = OcrErrorCode::RecognitionFailed;
        result.detail = err.size() > 160 ? err.substr(err.size() - 160) : err;
        if (result.detail.empty()) {
            result.detail = "pipeline produced no result";
        }
        return result;
    }

    const bool ok = json_bool(payload, "ok", false);
    if (!ok) {
        const std::string error_kind = json_string(payload, "error");
        result.error = (error_kind == "camera") ? OcrErrorCode::CameraFailed
                                                : OcrErrorCode::RecognitionFailed;
        result.speech = json_string(payload, "speech");
        result.display = json_string(payload, "display");
        result.detail = json_string(payload, "detail");
        if (result.detail.empty()) {
            result.detail = payload.size() > 160 ? payload.substr(0, 160) : payload;
        }
        return result;
    }

    result.error = OcrErrorCode::None;
    result.speech = json_string(payload, "speech");
    result.display = json_string(payload, "display");
    result.unsure = json_bool(payload, "unsure", false);
    return result;
}

} // namespace platform
