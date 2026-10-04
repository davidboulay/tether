#pragma once

#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace tether {

    // A mirrored notification as the desktop should present it.
    struct NotificationSpec {
        // The originating iPhone app. Shown as the popup's application name
        std::string app_name;
        std::string summary;
        std::string body;
        // Freedesktop icon names, best first.
        std::vector<std::string> icons;
        bool quiet = false;
        std::string reply_thread;
        // OTP found in this notification, or empty. Non-empty adds a copy action.
        std::string otp_code;
        // Originating iPhone bundle id.
        std::string app_id;
        // MAP handle to mark read when the code is copied. Empty for
        // notifications with no message behind them.
        std::string read_handle;
        // Stable identity for withdraw(): the phone's ANCS uid for a mirrored
        // notification. Empty means it cannot be withdrawn.
        std::string key;
        // Daemon health notices bypass the desktop-popups switch: that switch is
        // about iPhone traffic, and a broken daemon has to be able to say so.
        bool system = false;
        // Extra buttons with their own handlers, e.g. Accept / Reject on a
        // pairing request. Handlers run on the notifier's thread.
        struct Choice {
            std::string id;
            std::string label;
            std::function<void()> run;
        };
        std::vector<Choice> choices;
        // Stays on screen until acted on or closed.
        bool resident = false;
    };

    class DesktopNotifier {
    public:
        DesktopNotifier();
        ~DesktopNotifier();

        DesktopNotifier(const DesktopNotifier&) = delete;
        DesktopNotifier& operator=(const DesktopNotifier&) = delete;

        bool init();

        // Where the "Copy Code" action sends the code, along with the spec's
        // read_handle. Called on the notifier's own thread, so the handler is
        // responsible for any thread hop it needs.
        void set_copy_handler(std::function<void(const std::string& code, const std::string& handle)> handler);

        void notify_file_arrived(const std::filesystem::path& path);

        void notify(const NotificationSpec& spec);

        // Closes the popup shown for spec.key, if it is still on screen. The
        // phone dismissed it, so the desktop copy should go too.
        void withdraw(const std::string& key);

        // Where a "Mute <app>" button sends its bundle id. Called on the
        // notifier's own thread.
        void set_mute_handler(std::function<void(const std::string& app_id)> handler);

    private:
        struct Impl;
        std::unique_ptr<Impl> impl_;
    };

} // namespace tether
