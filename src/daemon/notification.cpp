#include "notification.hpp"
#include "notification_keys.hpp"

#include <tether/i18n.hpp>

#include <gio/gdesktopappinfo.h>
#include <gio/gio.h>
#include <glib.h>
#include <libnotify/notify.h>

#include <functional>
#include <mutex>
#include <string>
#include <tether/bluetooth/ancs/notifications.hpp>
#include <tether/log.hpp>
#include <tether/net.hpp>
#include <thread>
#include <unordered_map>
#include <unordered_set>

namespace tether {

    namespace {

        constexpr const char* DESKTOP_ENTRY = "tether-gtk";

        // A second popup from the same app inside this window replaces the
        // first instead of stacking, so a busy group chat is one popup that
        // keeps updating rather than one per message.
        constexpr int GROUP_WINDOW_SECONDS = 8;

        struct NotificationRequest {
            std::filesystem::path path;
        };

        // Everything below is touched on the notifier's thread only.
        std::unordered_map<std::string, NotifyNotification*> g_live_by_key;
        struct PopupMember {
            NotificationSpec spec;
            uint64_t sequence = 0;
        };
        std::unordered_map<std::string, PopupMember> g_members_by_key;
        uint64_t g_member_sequence = 0;

        struct RecentPopup {
            NotifyNotification* notification = nullptr;
            gint64 shown_at = 0;
            int count = 0;
            // A folded popup keeps the first one's Reply button, so only the
            // same conversation may fold into it.
            std::string reply_thread;
        };
        std::unordered_map<std::string, RecentPopup> g_recent_by_app;

        std::function<void(const std::string&)> g_mute_handler;

        void forget_live(NotifyNotification* notification) {
            for (auto it = g_live_by_key.begin(); it != g_live_by_key.end();) {
                if (it->second == notification) {
                    g_members_by_key.erase(it->first);
                    it = g_live_by_key.erase(it);
                } else
                    ++it;
            }
            for (auto it = g_recent_by_app.begin(); it != g_recent_by_app.end();) {
                if (it->second.notification == notification)
                    it = g_recent_by_app.erase(it);
                else
                    ++it;
            }
        }

        // Every icon name installed by any theme on this machine.
        // scanned once and cached for the life of the process.
        const std::unordered_set<std::string>& installed_icons() {
            static const std::unordered_set<std::string> names = [] {
                std::unordered_set<std::string> found;
                std::vector<std::filesystem::path> roots;

                const char* home = std::getenv("HOME");
                if (const char* data_home = std::getenv("XDG_DATA_HOME"); data_home && *data_home)
                    roots.emplace_back(std::filesystem::path(data_home) / "icons");
                else if (home)
                    roots.emplace_back(std::filesystem::path(home) / ".local/share/icons");
                if (home)
                    roots.emplace_back(std::filesystem::path(home) / ".icons");

                const char* data_dirs = std::getenv("XDG_DATA_DIRS");
                std::string dirs = data_dirs && *data_dirs ? data_dirs : "/usr/local/share:/usr/share";
                for (size_t start = 0; start <= dirs.size();) {
                    const size_t end = dirs.find(':', start);
                    const std::string dir = dirs.substr(start, end - start);
                    if (!dir.empty())
                        roots.emplace_back(std::filesystem::path(dir) / "icons");
                    if (end == std::string::npos)
                        break;
                    start = end + 1;
                }
                roots.emplace_back("/usr/share/pixmaps");

                std::error_code ec;
                for (const auto& root : roots) {
                    auto it = std::filesystem::recursive_directory_iterator(
                        root, std::filesystem::directory_options::skip_permission_denied, ec);
                    if (ec) {
                        ec.clear();
                        continue;
                    }
                    for (const auto& entry : it) {
                        const auto ext = entry.path().extension();
                        if (ext == ".png" || ext == ".svg" || ext == ".xpm")
                            found.insert(entry.path().stem().string());
                    }
                }
                debug::log(INFO, "Found {} icon names across the installed themes", found.size());
                return found;
            }();
            return names;
        }

        // The first candidate this machine can actually draw. Handing the server
        // a name no theme has leaves the popup with no icon at all, which is
        // worse than a generic one.
        std::string resolve_icon(const std::vector<std::string>& candidates) {
            const auto& installed = installed_icons();
            for (const auto& name : candidates) {
                if (installed.count(name))
                    return name;
            }
            return {};
        }

        // Identifies the sending application to the notification server. Without
        // this KDE shows the popup and then forgets it.
        void set_identity(NotifyNotification* notification, const std::string& app_name) {
            notify_notification_set_hint(notification, "desktop-entry", g_variant_new_string(DESKTOP_ENTRY));
            if (!app_name.empty()) {
                // Keeps history and configuration grouped under Tether while the
                // header names the iPhone app the notification came from.
                notify_notification_set_hint(
                    notification, "x-kde-display-appname", g_variant_new_string(app_name.c_str()));
            }
            notify_notification_set_hint(notification, "x-kde-origin-name", g_variant_new_string("iPhone"));
        }

        struct NotificationActionData {
            std::string payload;
            std::string handle;
        };

        // Set once during startup, before any notification exists.
        std::function<void(const std::string&, const std::string&)> g_copy_handler;

        void free_request(gpointer data) { delete static_cast<NotificationRequest*>(data); }

        void free_action_data(gpointer data) { delete static_cast<NotificationActionData*>(data); }

        bool launch_uri(const std::string& uri) {
            GError* error = nullptr;
            gboolean ok = g_app_info_launch_default_for_uri(uri.c_str(), nullptr, &error);
            if (!ok) {
                debug::log(ERR, "Failed to open URI '{}'", uri);
                if (error) {
                    debug::log(ERR, ": {}", error->message);
                    g_error_free(error);
                }
                debug::log(ERR, "");
                return false;
            }

            return true;
        }

        // Hitting an already-running GUI is GApplication's job. A second launch is routed to the primary instance
        // rather than starting a new window.
        void open_gui_thread(const std::string& thread_key) {
            std::string argument = "--thread=" + thread_key;
            char* argv[] = {const_cast<char*>("tether-gtk"), argument.data(), nullptr};
            GError* error = nullptr;
            if (!g_spawn_async(nullptr,
                               argv,
                               nullptr,
                               static_cast<GSpawnFlags>(G_SPAWN_SEARCH_PATH | G_SPAWN_STDOUT_TO_DEV_NULL |
                                                        G_SPAWN_STDERR_TO_DEV_NULL),
                               nullptr,
                               nullptr,
                               nullptr,
                               &error)) {
                debug::log(ERR, "Failed to open tether-gtk: {}", error ? error->message : "unknown");
                g_clear_error(&error);
            }
        }

        void on_copy_code_action(NotifyNotification* notification, char*, gpointer user_data) {
            auto* action = static_cast<NotificationActionData*>(user_data);
            if (action && g_copy_handler)
                g_copy_handler(action->payload, action->handle);
            // The code is used; the popup has nothing left to say.
            notify_notification_close(notification, nullptr);
        }

        void on_mute_action(NotifyNotification* notification, char*, gpointer user_data) {
            auto* action = static_cast<NotificationActionData*>(user_data);
            if (action && g_mute_handler)
                g_mute_handler(action->payload);
            notify_notification_close(notification, nullptr);
        }

        struct ChoiceData {
            std::function<void()> run;
        };

        void free_choice_data(gpointer data) { delete static_cast<ChoiceData*>(data); }

        void on_choice_action(NotifyNotification* notification, char*, gpointer user_data) {
            if (auto* choice = static_cast<ChoiceData*>(user_data); choice && choice->run)
                choice->run();
            notify_notification_close(notification, nullptr);
        }

        void on_reply_action(NotifyNotification*, char*, gpointer user_data) {
            if (auto* action = static_cast<NotificationActionData*>(user_data))
                open_gui_thread(action->payload);
        }

        // Whether this process can see the host's installed applications. A flatpak sandbox cannot.
        bool sandboxed() {
            static const bool yes = std::filesystem::exists("/.flatpak-info");
            return yes;
        }

        // How to open the Linux counterpart of an iPhone app.
        struct AppLauncher {
            // "scheme://" URI, a "<id>.desktop" entry, or empty when
            std::string payload;
            std::string name;
        };

        AppLauncher resolve_launcher(const std::string& app_id) {
            const auto target = bluetooth::ancs::launch_target(app_id);

            if (!target.uri_scheme.empty()) {
                if (GAppInfo* info = g_app_info_get_default_for_uri_scheme(target.uri_scheme.c_str())) {
                    // Launch the handler itself rather than a bare "scheme://" the app
                    // would have to make sense of.
                    const char* id = g_app_info_get_id(info);
                    const char* name = g_app_info_get_display_name(info);
                    AppLauncher launcher{id ? id : target.uri_scheme + "://", name ? name : ""};
                    g_object_unref(info);
                    return launcher;
                }
                if (sandboxed())
                    return {target.uri_scheme + "://", ""};
            }

            for (const auto& id : target.desktop_ids) {
                const std::string entry = id + ".desktop";
                if (GDesktopAppInfo* info = g_desktop_app_info_new(entry.c_str())) {
                    const char* name = g_app_info_get_display_name(G_APP_INFO(info));
                    AppLauncher launcher{entry, name ? name : ""};
                    g_object_unref(info);
                    return launcher;
                }
            }
            return {};
        }

        void on_open_app_action(NotifyNotification*, char*, gpointer user_data) {
            auto* action = static_cast<NotificationActionData*>(user_data);
            if (!action)
                return;

            if (!action->payload.ends_with(".desktop")) {
                launch_uri(action->payload);
                return;
            }

            GDesktopAppInfo* info = g_desktop_app_info_new(action->payload.c_str());
            if (!info) {
                debug::log(ERR, "No desktop entry '{}' to open", action->payload);
                return;
            }
            GError* error = nullptr;
            if (!g_app_info_launch(G_APP_INFO(info), nullptr, nullptr, &error)) {
                debug::log(ERR, "Failed to launch {}: {}", action->payload, error ? error->message : "unknown");
                g_clear_error(&error);
            }
            g_object_unref(info);
        }

        void on_notification_action(NotifyNotification*, char*, gpointer user_data) {
            auto* action = static_cast<NotificationActionData*>(user_data);
            if (!action) {
                return;
            }

            launch_uri(action->payload);
        }

        void on_notification_closed(NotifyNotification* notification, gpointer) {
            forget_live(notification);
            g_object_unref(notification);
        }

        gboolean show_notification_on_main(gpointer user_data) {
            std::unique_ptr<NotificationRequest> request(static_cast<NotificationRequest*>(user_data));

            auto file = std::filesystem::absolute(request->path);
            auto file_string = file.string();
            auto folder_string = file.parent_path().string();
            auto file_uri = g_filename_to_uri(file_string.c_str(), nullptr, nullptr);
            auto folder_uri = g_filename_to_uri(folder_string.c_str(), nullptr, nullptr);

            if (!file_uri || !folder_uri) {
                if (file_uri)
                    g_free(file_uri);
                if (folder_uri)
                    g_free(folder_uri);
                debug::log(ERR, "Failed to create notification URI for {}", file);
                return G_SOURCE_REMOVE;
            }

            NotifyNotification* notification =
                notify_notification_new(_("File arrived"), file.filename().c_str(), "document-save");

            set_identity(notification, "");
            notify_notification_set_hint(notification, "resident", g_variant_new_boolean(TRUE));
            notify_notification_set_timeout(notification, NOTIFY_EXPIRES_DEFAULT);

            auto* open_file = new NotificationActionData{file_uri};
            auto* open_folder = new NotificationActionData{folder_uri};

            g_signal_connect(notification, "closed", G_CALLBACK(on_notification_closed), nullptr);
            notify_notification_add_action(
                notification, "open-file", _("Open File"), on_notification_action, open_file, free_action_data);
            notify_notification_add_action(
                notification, "open-folder", _("Open Folder"), on_notification_action, open_folder, free_action_data);

            GError* error = nullptr;
            gboolean shown = notify_notification_show(notification, &error);
            if (!shown) {
                debug::log(ERR, "Failed to show notification");
                if (error) {
                    debug::log(ERR, ": {}", error->message);
                    g_error_free(error);
                }
                debug::log(ERR, "");
                g_object_unref(notification);
            }
            g_free(file_uri);
            g_free(folder_uri);
            return G_SOURCE_REMOVE;
        }

        gboolean show_spec_on_main(gpointer user_data) {
            std::unique_ptr<NotificationSpec> spec(static_cast<NotificationSpec*>(user_data));

            const std::string icon = resolve_icon(spec->icons);

            // A burst from one app folds into the popup already on screen.
            const gint64 now = g_get_monotonic_time();
            // A code needs its own Copy Code button, so it never folds.
            const bool groupable = !spec->app_id.empty() && spec->choices.empty() && spec->otp_code.empty();
            if (groupable) {
                auto recent = g_recent_by_app.find(spec->app_id);
                if (recent != g_recent_by_app.end() && recent->second.reply_thread == spec->reply_thread &&
                    now - recent->second.shown_at < GROUP_WINDOW_SECONDS * G_USEC_PER_SEC) {
                    RecentPopup& popup = recent->second;
                    popup.count += 1;
                    popup.shown_at = now;
                    std::string body = spec->body;
                    if (!body.empty())
                        body += "\n";
                    body += tr_format(P_("{} more notification", "{} more notifications", popup.count - 1),
                                      popup.count - 1);
                    notify_notification_update(
                        popup.notification, spec->summary.c_str(), body.c_str(), icon.empty() ? nullptr : icon.c_str());
                    if (!spec->key.empty()) {
                        g_live_by_key[spec->key] = popup.notification;
                        g_members_by_key[spec->key] = PopupMember{*spec, ++g_member_sequence};
                    }
                    GError* error = nullptr;
                    if (!notify_notification_show(popup.notification, &error)) {
                        debug::log(ERR, "Failed to update notification: {}", error ? error->message : "unknown");
                        g_clear_error(&error);
                    }
                    return G_SOURCE_REMOVE;
                }
            }

            NotifyNotification* notification =
                notify_notification_new(spec->summary.c_str(),
                                        spec->body.empty() ? nullptr : spec->body.c_str(),
                                        icon.empty() ? nullptr : icon.c_str());
            if (!notification)
                return G_SOURCE_REMOVE;

            set_identity(notification, spec->app_name);
            notify_notification_set_urgency(notification,
                                            spec->quiet    ? NOTIFY_URGENCY_LOW
                                            : spec->system ? NOTIFY_URGENCY_CRITICAL
                                                           : NOTIFY_URGENCY_NORMAL);
            if (spec->resident) {
                notify_notification_set_hint(notification, "resident", g_variant_new_boolean(TRUE));
                notify_notification_set_timeout(notification, NOTIFY_EXPIRES_NEVER);
            }

            const AppLauncher launcher = spec->app_id.empty() ? AppLauncher{} : resolve_launcher(spec->app_id);
            // Health notices and file popups are not iPhone apps, so nothing to mute.
            const bool mutable_app = !spec->app_id.empty() && !spec->system;
            // The popup object is kept alive, and tracked, for as long as something
            // might still act on it: a button, a later update, or a withdraw.
            const bool tracked = true;
            if (tracked)
                g_signal_connect(notification, "closed", G_CALLBACK(on_notification_closed), nullptr);
            if (!spec->key.empty()) {
                g_live_by_key[spec->key] = notification;
                g_members_by_key[spec->key] = PopupMember{*spec, ++g_member_sequence};
            }
            if (groupable)
                g_recent_by_app[spec->app_id] = RecentPopup{notification, now, 1, spec->reply_thread};

            if (!spec->reply_thread.empty()) {
                notify_notification_add_action(notification,
                                               "default",
                                               "default",
                                               on_reply_action,
                                               new NotificationActionData{spec->reply_thread},
                                               free_action_data);
                notify_notification_add_action(notification,
                                               "reply",
                                               _("Reply"),
                                               on_reply_action,
                                               new NotificationActionData{spec->reply_thread},
                                               free_action_data);
            }

            if (!spec->otp_code.empty()) {
                notify_notification_add_action(notification,
                                               "copy-code",
                                               _("Copy Code"),
                                               on_copy_code_action,
                                               new NotificationActionData{spec->otp_code, spec->read_handle},
                                               free_action_data);
            }

            if (!launcher.payload.empty()) {
                const std::string label =
                    tr_format(_("Open in {}"), launcher.name.empty() ? spec->app_name : launcher.name);
                notify_notification_add_action(notification,
                                               "open-app",
                                               label.c_str(),
                                               on_open_app_action,
                                               new NotificationActionData{launcher.payload},
                                               free_action_data);
            }

            for (const auto& choice : spec->choices) {
                notify_notification_add_action(notification,
                                               choice.id.c_str(),
                                               choice.label.c_str(),
                                               on_choice_action,
                                               new ChoiceData{choice.run},
                                               free_choice_data);
            }

            if (mutable_app) {
                // TRANSLATORS: {} is an iPhone app name. Silences its popups on this computer.
                const std::string label = tr_format(_("Mute {}"), spec->app_name.empty() ? "app" : spec->app_name);
                notify_notification_add_action(notification,
                                               "mute-app",
                                               label.c_str(),
                                               on_mute_action,
                                               new NotificationActionData{spec->app_id},
                                               free_action_data);
            }

            GError* error = nullptr;
            if (!notify_notification_show(notification, &error)) {
                debug::log(ERR, "Failed to show notification: {}", error ? error->message : "unknown");
                g_clear_error(&error);
                forget_live(notification);
                g_object_unref(notification);
                return G_SOURCE_REMOVE;
            }
            return G_SOURCE_REMOVE;
        }

        gboolean withdraw_on_main(gpointer user_data) {
            std::unique_ptr<std::string> key(static_cast<std::string*>(user_data));
            const auto it = g_live_by_key.find(*key);
            if (it == g_live_by_key.end())
                return G_SOURCE_REMOVE;
            NotifyNotification* grouped = it->second;
            NotifyNotification* notification = detach_notification_key(g_live_by_key, *key);
            g_members_by_key.erase(*key);
            if (!notification) {
                // Show the newest surviving member and the remaining count,
                // rather than leaving dismissed phone content in the popup.
                const PopupMember* newest = nullptr;
                int count = 0;
                for (const auto& [member_key, popup] : g_live_by_key) {
                    if (popup != grouped)
                        continue;
                    const auto member = g_members_by_key.find(member_key);
                    if (member == g_members_by_key.end())
                        continue;
                    ++count;
                    if (!newest || member->second.sequence > newest->sequence)
                        newest = &member->second;
                }
                if (newest) {
                    std::string body = newest->spec.body;
                    if (count > 1) {
                        if (!body.empty())
                            body += "\n";
                        body += tr_format(P_("{} more notification", "{} more notifications", count - 1), count - 1);
                    }
                    const std::string icon = resolve_icon(newest->spec.icons);
                    notify_notification_update(
                        grouped, newest->spec.summary.c_str(), body.c_str(), icon.empty() ? nullptr : icon.c_str());
                    for (auto& [app, popup] : g_recent_by_app)
                        if (popup.notification == grouped)
                            popup.count = count;
                    GError* error = nullptr;
                    if (!notify_notification_show(grouped, &error)) {
                        debug::log(ERR, "Failed to update notification: {}", error ? error->message : "unknown");
                        g_clear_error(&error);
                    }
                }
                return G_SOURCE_REMOVE;
            }
            // close emits "closed", which drops the remaining references.
            notify_notification_close(notification, nullptr);
            return G_SOURCE_REMOVE;
        }

    } // namespace

    struct DesktopNotifier::Impl {
        GMainContext* context = nullptr;
        GMainLoop* loop = nullptr;
        std::thread thread;
        bool initialized = false;
    };

    DesktopNotifier::DesktopNotifier() : impl_(std::make_unique<Impl>()) {}

    DesktopNotifier::~DesktopNotifier() {
        if (!impl_->initialized) {
            return;
        }

        g_main_context_invoke(
            impl_->context,
            [](gpointer data) -> gboolean {
                auto* loop = static_cast<GMainLoop*>(data);
                g_main_loop_quit(loop);
                return G_SOURCE_REMOVE;
            },
            impl_->loop);

        if (impl_->thread.joinable()) {
            impl_->thread.join();
        }

        g_main_loop_unref(impl_->loop);
        g_main_context_unref(impl_->context);
        notify_uninit();
    }

    bool DesktopNotifier::init() {
        if (impl_->initialized) {
            return true;
        }

        if (!notify_init("Tether")) {
            debug::log(ERR, "Failed to initialize libnotify");
            return false;
        }

        impl_->context = g_main_context_new();
        impl_->loop = g_main_loop_new(impl_->context, FALSE);
        impl_->thread = std::thread([ctx = impl_->context, loop = impl_->loop]() {
            g_main_context_push_thread_default(ctx);
            // Walking the icon themes takes long enough to be worth doing before
            // the first notification rather than during it.
            installed_icons();
            g_main_loop_run(loop);
            g_main_context_pop_thread_default(ctx);
        });
        impl_->initialized = true;
        return true;
    }

    void DesktopNotifier::set_copy_handler(
        std::function<void(const std::string& code, const std::string& handle)> handler) {
        g_copy_handler = std::move(handler);
    }

    void DesktopNotifier::set_mute_handler(std::function<void(const std::string& app_id)> handler) {
        g_mute_handler = std::move(handler);
    }

    void DesktopNotifier::withdraw(const std::string& key) {
        if (!impl_ || !impl_->initialized || key.empty())
            return;
        g_main_context_invoke_full(impl_->context, G_PRIORITY_DEFAULT, withdraw_on_main, new std::string(key), nullptr);
    }

    void DesktopNotifier::notify(const NotificationSpec& spec) {
        if (!impl_ || !impl_->initialized)
            return;
        if (!spec.system && !desktop_popups_enabled())
            return;
        if (!spec.system && app_muted(spec.app_id))
            return;

        // Bluetooth delivers on its own thread; libnotify's proxy belongs to the
        // notifier's loop.
        auto* copy = new NotificationSpec(spec);
        g_main_context_invoke_full(impl_->context, G_PRIORITY_DEFAULT, show_spec_on_main, copy, nullptr);
    }

    void DesktopNotifier::notify_file_arrived(const std::filesystem::path& path) {
        if (!impl_->initialized || !desktop_popups_enabled()) {
            return;
        }

        auto* request = new NotificationRequest{path};
        g_main_context_invoke_full(impl_->context, G_PRIORITY_DEFAULT, show_notification_on_main, request, nullptr);
    }

} // namespace tether
