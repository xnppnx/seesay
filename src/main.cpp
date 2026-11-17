/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 * SPDX-License-Identifier: MIT
 *
 * SeeSay-specific startup: the CardputerZero APPLaunch starts this binary in
 * the logged-in user's graphical session (there is no systemd service). Set up
 * the offline runtime environment here, in code, so it does not depend on any
 * service manager.
 */
#include "app.h"

#include <cstdlib>

int main() {
    // Hard offline guarantees (project book 8.6) - must be set before the
    // Python engines are ever spawned.
    if (!std::getenv("HF_HUB_OFFLINE")) {
        setenv("HF_HUB_OFFLINE", "1", 1);
    }
    if (!std::getenv("HF_HUB_DISABLE_TELEMETRY")) {
        setenv("HF_HUB_DISABLE_TELEMETRY", "1", 1);
    }
    if (!std::getenv("CACTUS_TELEMETRY")) {
        setenv("CACTUS_TELEMETRY", "0", 1);
    }

    app::Application application;
    return application.run();
}
