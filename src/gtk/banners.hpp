#pragma once

#include <gtk/gtk.h>
#include <string>
#include <vector>

namespace tether::ui {

    struct PendingPair {
        std::string fingerprint;
        std::string name;
        std::string address;
    };

    // The strip under the header bar: one row per pairing request waiting for
    // an answer, and a warning when the running daemon is a different build.
    // Call once; the caller packs it above the views.
    GtkWidget* banners_new();

    // Replaces the pairing rows. Empty hides the strip.
    void banners_set_pending_pairs(const std::vector<PendingPair>& requests);

    // The running daemon's version string, or empty for a build older than the
    // field. Hidden again when it matches this build.
    void banners_set_daemon_version(const std::string& running);

    // No daemon means no version to argue with.
    void banners_set_daemon_online(bool online);

} // namespace tether::ui
