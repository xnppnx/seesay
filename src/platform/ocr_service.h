/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <atomic>
#include <mutex>
#include <string>
#include <thread>

namespace platform {

enum class OcrErrorCode {
    None = 0,
    CameraFailed,
    RecognitionFailed,
    Busy,
};

// One capture + local-recognition round trip.
struct OcrJobResult {
    OcrErrorCode error{OcrErrorCode::None};
    std::string speech;   // full sentence that was spoken
    std::string display;  // short text for the screen
    bool unsure{false};
    std::string detail;   // diagnostics
};

// Runs the whole voice-loop pipeline on a worker thread so LVGL never blocks.
// A single job at a time; extra requests while busy are ignored.
class OcrService {
public:
    OcrService();
    ~OcrService();

    OcrService(const OcrService&) = delete;
    OcrService& operator=(const OcrService&) = delete;

    // Kicks off the local pipeline (speak -> shoot -> describe -> read -> speak).
    bool start();
    // Speak the last announcement again without re-shooting.
    bool replay();

    bool busy() const { return busy_.load(); }

    // Non-blocking: picks up a finished job, if any.
    bool consume(OcrJobResult& out);

private:
    void run();
    OcrJobResult do_job();

    std::atomic<bool> busy_{false};
    std::atomic<bool> stop_{false};
    std::atomic<bool> replay_{false};
    std::mutex result_mutex_;
    bool has_result_{false};
    OcrJobResult pending_result_;
    std::thread worker_;
};

} // namespace platform
