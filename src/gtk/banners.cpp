#include "banners.hpp"

#include "daemon_client.hpp"
#include "toast.hpp"
#include "ui_util.hpp"

#include <tether/client.hpp>
#include <tether/i18n.hpp>
#include <tether/version.hpp>

namespace tether::ui {

    namespace {

        struct BannerState {
            GtkWidget* root = nullptr;
            GtkWidget* pairs = nullptr;
            GtkWidget* version_bar = nullptr;
            GtkWidget* version_label = nullptr;
            std::string running_version;
            bool online = false;
            bool version_known = false;
        };

        BannerState g_banners;

        const char* stashed(GtkWidget* widget, const char* key) {
            const char* value = static_cast<const char*>(g_object_get_data(G_OBJECT(widget), key));
            return value ? value : "";
        }

        void disable_row_buttons(GtkWidget* button) {
            GtkWidget* row = gtk_widget_get_parent(button);
            if (!row)
                return;
            GList* children = gtk_container_get_children(GTK_CONTAINER(row));
            for (GList* item = children; item; item = item->next) {
                if (GTK_IS_BUTTON(item->data))
                    gtk_widget_set_sensitive(GTK_WIDGET(item->data), FALSE);
            }
            g_list_free(children);
        }

        void on_accept(GtkWidget* button, gpointer) {
            const std::string fingerprint = stashed(button, "fingerprint");
            const std::string name = stashed(button, "name");
            if (!daemon_send({{"command", "accept_device"}, {"fingerprint", fingerprint}, {"device_name", name}})) {
                show_toast(_("Could not reach the Tether daemon."), ToastLevel::Error);
                return;
            }
            disable_row_buttons(button);
        }

        void on_reject(GtkWidget* button, gpointer) {
            const std::string fingerprint = stashed(button, "fingerprint");
            if (!daemon_send({{"command", "reject_device"}, {"fingerprint", fingerprint}})) {
                show_toast(_("Could not reach the Tether daemon."), ToastLevel::Error);
                return;
            }
            disable_row_buttons(button);
        }

        void update_version_bar() {
            if (!g_banners.version_bar)
                return;
            const bool mismatch = g_banners.online && g_banners.version_known && g_banners.running_version != TETHER_VERSION;
            if (!mismatch) {
                gtk_widget_hide(g_banners.version_bar);
                return;
            }
            // TRANSLATORS: {0} is the daemon's version, {1} this app's version.
            std::string text = tr_format(_("The running Tether daemon is {0}, but this app is {1}. {2}"),
                                         g_banners.running_version.empty() ? _("an older build")
                                                                           : g_banners.running_version.c_str(),
                                         TETHER_VERSION,
                                         tether::daemon_restart_hint());
            set_text(g_banners.version_label, text);
            gtk_widget_show(g_banners.version_bar);
        }

        void on_restart_daemon(GtkWidget* button, gpointer) {
            gtk_widget_set_sensitive(button, FALSE);
            GError* error = nullptr;
            if (tether::systemd_owns_tetherd()) {
                const char* argv[] = {"systemctl", "--user", "restart", "tetherd", nullptr};
                g_spawn_async(nullptr,
                              const_cast<char**>(argv),
                              nullptr,
                              static_cast<GSpawnFlags>(G_SPAWN_SEARCH_PATH | G_SPAWN_STDOUT_TO_DEV_NULL |
                                                       G_SPAWN_STDERR_TO_DEV_NULL),
                              nullptr,
                              nullptr,
                              nullptr,
                              &error);
            } else {
                const char* argv[] = {"pkill", "-x", "tetherd", nullptr};
                g_spawn_async(nullptr,
                              const_cast<char**>(argv),
                              nullptr,
                              static_cast<GSpawnFlags>(G_SPAWN_SEARCH_PATH | G_SPAWN_STDOUT_TO_DEV_NULL |
                                                       G_SPAWN_STDERR_TO_DEV_NULL),
                              nullptr,
                              nullptr,
                              nullptr,
                              &error);
                // The reconnect loop only spawns a daemon once per run; this is the
                // one case where a second spawn is wanted.
                daemon_client_allow_respawn();
            }
            if (error) {
                show_toast(tr_format(_("Could not restart the daemon: {}"), error->message), ToastLevel::Error);
                g_clear_error(&error);
                gtk_widget_set_sensitive(button, TRUE);
                return;
            }
            show_toast(_("Restarting the Tether daemon…"));
            gtk_widget_set_sensitive(button, TRUE);
        }

    } // namespace

    GtkWidget* banners_new() {
        g_banners.root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);

        g_banners.version_bar = gtk_info_bar_new();
        gtk_info_bar_set_message_type(GTK_INFO_BAR(g_banners.version_bar), GTK_MESSAGE_WARNING);
        g_banners.version_label = gtk_label_new(nullptr);
        gtk_label_set_xalign(GTK_LABEL(g_banners.version_label), 0.0);
        gtk_label_set_line_wrap(GTK_LABEL(g_banners.version_label), TRUE);
        gtk_label_set_line_wrap_mode(GTK_LABEL(g_banners.version_label), PANGO_WRAP_WORD_CHAR);
        gtk_label_set_selectable(GTK_LABEL(g_banners.version_label), TRUE);
        gtk_container_add(GTK_CONTAINER(gtk_info_bar_get_content_area(GTK_INFO_BAR(g_banners.version_bar))),
                          g_banners.version_label);
        GtkWidget* restart = gtk_button_new_with_label(_("Restart daemon"));
        g_signal_connect(restart, "clicked", G_CALLBACK(on_restart_daemon), nullptr);
        gtk_container_add(GTK_CONTAINER(gtk_info_bar_get_action_area(GTK_INFO_BAR(g_banners.version_bar))), restart);
        // Children shown once now; the bar itself is toggled, and show_all skips
        // a no-show-all widget outright.
        gtk_widget_show_all(g_banners.version_bar);
        gtk_widget_hide(g_banners.version_bar);
        gtk_widget_set_no_show_all(g_banners.version_bar, TRUE);
        gtk_box_pack_start(GTK_BOX(g_banners.root), g_banners.version_bar, FALSE, FALSE, 0);

        g_banners.pairs = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
        gtk_box_pack_start(GTK_BOX(g_banners.root), g_banners.pairs, FALSE, FALSE, 0);
        return g_banners.root;
    }

    void banners_set_pending_pairs(const std::vector<PendingPair>& requests) {
        if (!g_banners.pairs)
            return;
        GList* children = gtk_container_get_children(GTK_CONTAINER(g_banners.pairs));
        for (GList* item = children; item; item = item->next)
            gtk_widget_destroy(GTK_WIDGET(item->data));
        g_list_free(children);

        for (const PendingPair& request : requests) {
            GtkWidget* bar = gtk_info_bar_new();
            gtk_info_bar_set_message_type(GTK_INFO_BAR(bar), GTK_MESSAGE_QUESTION);

            GtkWidget* content = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
            gtk_box_pack_start(GTK_BOX(content),
                               gtk_image_new_from_icon_name("network-wireless-acquiring-symbolic", GTK_ICON_SIZE_BUTTON),
                               FALSE,
                               FALSE,
                               0);
            GtkWidget* label = gtk_label_new(nullptr);
            std::string short_fp = request.fingerprint.size() > 16 ? request.fingerprint.substr(0, 16) + "…"
                                                                   : request.fingerprint;
            // TRANSLATORS: {0} is a device name, {1} a shortened key fingerprint.
            set_markup(label,
                       tr_format(_("<b>{0}</b> wants to pair with this computer over Wi-Fi ({1})."),
                                 escape_markup(request.name.empty() ? _("Unknown Device") : request.name),
                                 escape_markup(short_fp)));
            gtk_label_set_xalign(GTK_LABEL(label), 0.0);
            gtk_label_set_line_wrap(GTK_LABEL(label), TRUE);
            gtk_label_set_line_wrap_mode(GTK_LABEL(label), PANGO_WRAP_WORD_CHAR);
            gtk_box_pack_start(GTK_BOX(content), label, TRUE, TRUE, 0);
            gtk_container_add(GTK_CONTAINER(gtk_info_bar_get_content_area(GTK_INFO_BAR(bar))), content);

            GtkWidget* actions = gtk_info_bar_get_action_area(GTK_INFO_BAR(bar));
            GtkWidget* reject = gtk_button_new_with_label(_("Reject"));
            g_object_set_data_full(G_OBJECT(reject), "fingerprint", g_strdup(request.fingerprint.c_str()), g_free);
            g_signal_connect(reject, "clicked", G_CALLBACK(on_reject), nullptr);
            gtk_container_add(GTK_CONTAINER(actions), reject);
            GtkWidget* accept = gtk_button_new_with_label(_("Accept"));
            gtk_style_context_add_class(gtk_widget_get_style_context(accept), "suggested-action");
            g_object_set_data_full(G_OBJECT(accept), "fingerprint", g_strdup(request.fingerprint.c_str()), g_free);
            g_object_set_data_full(G_OBJECT(accept), "name", g_strdup(request.name.c_str()), g_free);
            g_signal_connect(accept, "clicked", G_CALLBACK(on_accept), nullptr);
            gtk_container_add(GTK_CONTAINER(actions), accept);

            gtk_box_pack_start(GTK_BOX(g_banners.pairs), bar, FALSE, FALSE, 0);
        }
        gtk_widget_show_all(g_banners.pairs);
    }

    void banners_set_daemon_version(const std::string& running) {
        g_banners.running_version = running;
        g_banners.version_known = true;
        update_version_bar();
    }

    void banners_set_daemon_online(bool online) {
        g_banners.online = online;
        if (!online)
            g_banners.version_known = false;
        update_version_bar();
    }

} // namespace tether::ui
