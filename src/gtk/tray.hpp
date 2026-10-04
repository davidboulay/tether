#pragma once

#include "ui_util.hpp"

#include <functional>
#include <string>

namespace tether::ui {

    // Publishes a StatusNotifierItem. A desktop with no tray never claims it.
    void tray_init();

    // Mirrors set_route_status; the tooltip is rebuilt from both routes at once.
    void tray_set_route(Route route, bool ok, const std::string& detail);

    // Total unread across threads. Drives the badged icon and a tooltip line.
    void tray_set_unread(int count);

    // Pairing requests waiting for an answer. Non-zero puts the item in the
    // NeedsAttention status, badges the icon and adds a tooltip line.
    void tray_set_pending_pairs(int count);

    // Mirrors the daemon's clipboard switch into the menu's check item.
    void tray_set_clipboard_sync(bool enabled);

    // Called once if no StatusNotifierWatcher turns up within a few seconds of
    // start, so a window hidden for a tray that does not exist can be shown.
    void tray_on_no_host(std::function<void()> callback);

    // AirPods battery for the tooltip, already formatted. Empty removes the line:
    // nothing connected, or the channel has reported nothing yet.
    void tray_set_airpods(const std::string& name, const std::string& battery);

    // Repaints from cached state, for when the daemon returns before any route event.
    void tray_refresh();

    // False means the close button quits, as it always has.
    bool tray_close_to_tray();
    void tray_set_close_to_tray(bool enabled);

} // namespace tether::ui
