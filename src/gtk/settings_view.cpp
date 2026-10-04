#include "settings_view.hpp"

#include "daemon_client.hpp"
#include "prefs.hpp"
#include "toast.hpp"
#include "tray.hpp"
#include "ui_util.hpp"

#include <string>
#include <tether/bluetooth/ancs/notifications.hpp>
#include <tether/i18n.hpp>

namespace tether::ui {

    namespace {

        struct SettingsState {
            GtkWidget* window = nullptr;

            // No iPhone bonded means every daemon-backed row below is dead.
            GtkWidget* lbl_bt_hint = nullptr;

            GtkWidget* sw_tray = nullptr;
            GtkWidget* cmb_tray_icon = nullptr;

            GtkWidget* row_ancs = nullptr;
            GtkWidget* sw_ancs = nullptr;
            GtkWidget* row_content = nullptr;
            GtkWidget* sw_content = nullptr;

            GtkWidget* sw_popups = nullptr;
            GtkWidget* row_previews = nullptr;
            GtkWidget* sw_previews = nullptr;

            GtkWidget* row_retention = nullptr;
            GtkWidget* cmb_retention = nullptr;
            GtkWidget* lbl_keyring = nullptr;

            GtkWidget* row_calls = nullptr;
            GtkWidget* sw_calls = nullptr;

            GtkWidget* sw_lock_away = nullptr;

            GtkWidget* sw_clipboard = nullptr;
            GtkWidget* row_clipboard = nullptr;

            GtkWidget* muted_list = nullptr;
            GtkWidget* muted_empty = nullptr;

            nlohmann::json bt_status = nlohmann::json::object();
        };

        SettingsState g_settings;

        // A titled row: description under the title, control on the right.
        GtkWidget* add_row(GtkWidget* list, const std::string& title, const std::string& subtitle, GtkWidget* control) {
            GtkWidget* row = gtk_list_box_row_new();
            gtk_list_box_row_set_activatable(GTK_LIST_BOX_ROW(row), FALSE);
            gtk_list_box_row_set_selectable(GTK_LIST_BOX_ROW(row), FALSE);

            GtkWidget* box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
            gtk_container_set_border_width(GTK_CONTAINER(box), 10);

            GtkWidget* text = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
            GtkWidget* title_label = gtk_label_new(title.c_str());
            gtk_label_set_xalign(GTK_LABEL(title_label), 0.0);
            gtk_box_pack_start(GTK_BOX(text), title_label, FALSE, FALSE, 0);
            // Lets screen readers name the control after the row title.
            gtk_label_set_mnemonic_widget(GTK_LABEL(title_label), control);
            if (!subtitle.empty()) {
                atk_object_set_description(gtk_widget_get_accessible(control), subtitle.c_str());
                GtkWidget* subtitle_label = gtk_label_new(subtitle.c_str());
                gtk_label_set_xalign(GTK_LABEL(subtitle_label), 0.0);
                gtk_label_set_line_wrap(GTK_LABEL(subtitle_label), TRUE);
                gtk_label_set_max_width_chars(GTK_LABEL(subtitle_label), 46);
                gtk_style_context_add_class(gtk_widget_get_style_context(subtitle_label), "muted");
                gtk_box_pack_start(GTK_BOX(text), subtitle_label, FALSE, FALSE, 0);
            }
            gtk_box_pack_start(GTK_BOX(box), text, TRUE, TRUE, 0);

            gtk_widget_set_valign(control, GTK_ALIGN_CENTER);
            gtk_box_pack_end(GTK_BOX(box), control, FALSE, FALSE, 0);

            gtk_container_add(GTK_CONTAINER(row), box);
            gtk_container_add(GTK_CONTAINER(list), row);
            return row;
        }

        // Heading, optional one-line explanation, and the framed list the rows go in.
        GtkWidget* add_group(GtkWidget* column, const std::string& heading, const std::string& description) {
            GtkWidget* heading_label = gtk_label_new(nullptr);
            gtk_label_set_xalign(GTK_LABEL(heading_label), 0.0);
            set_markup(heading_label, "<b>" + escape_markup(heading) + "</b>");
            gtk_widget_set_margin_top(heading_label, 8);
            gtk_box_pack_start(GTK_BOX(column), heading_label, FALSE, FALSE, 0);

            if (!description.empty()) {
                GtkWidget* description_label = gtk_label_new(description.c_str());
                gtk_label_set_xalign(GTK_LABEL(description_label), 0.0);
                gtk_label_set_line_wrap(GTK_LABEL(description_label), TRUE);
                gtk_style_context_add_class(gtk_widget_get_style_context(description_label), "muted");
                gtk_box_pack_start(GTK_BOX(column), description_label, FALSE, FALSE, 0);
            }

            GtkWidget* list = gtk_list_box_new();
            gtk_list_box_set_selection_mode(GTK_LIST_BOX(list), GTK_SELECTION_NONE);
            gtk_style_context_add_class(gtk_widget_get_style_context(list), "frame");
            gtk_box_pack_start(GTK_BOX(column), list, FALSE, FALSE, 0);
            return list;
        }

        GtkWidget* new_switch() {
            GtkWidget* widget = gtk_switch_new();
            gtk_widget_set_halign(widget, GTK_ALIGN_END);
            return widget;
        }

        void on_tray_toggled(GtkSwitch* widget, GParamSpec*, gpointer) {
            tray_set_close_to_tray(gtk_switch_get_active(widget) == TRUE);
        }

        void set_switch(GtkWidget* widget, gpointer handler, bool active);

        // A switch is a promise the daemon has to keep. When the command cannot
        // even be handed over, the switch goes back and the reason is said; the
        // old behaviour left it flipped with nothing saved behind it.
        void send_switch(GtkSwitch* widget, gpointer handler, const nlohmann::json& command) {
            if (daemon_send(command))
                return;
            set_switch(GTK_WIDGET(widget), handler, !gtk_switch_get_active(widget));
            show_toast(_("The Tether daemon is not running, so this setting was not changed."), ToastLevel::Error);
        }

        void on_tray_icon_changed(GtkComboBox* combo, gpointer) {
            if (const gchar* id = gtk_combo_box_get_active_id(combo)) {
                prefs()["tray_icon"] = std::string(id);
                prefs_save();
            }
        }

        void on_ancs_toggled(GtkSwitch* widget, GParamSpec*, gpointer) {
            send_switch(widget,
                        reinterpret_cast<gpointer>(on_ancs_toggled),
                        {{"command", "bt_set_ancs"}, {"enabled", gtk_switch_get_active(widget) == TRUE}});
        }

        void on_content_toggled(GtkSwitch* widget, GParamSpec*, gpointer) {
            send_switch(widget,
                        reinterpret_cast<gpointer>(on_content_toggled),
                        {{"command", "bt_set_ancs_content"}, {"enabled", gtk_switch_get_active(widget) == TRUE}});
        }

        void on_popups_toggled(GtkSwitch* widget, GParamSpec*, gpointer) {
            send_switch(widget,
                        reinterpret_cast<gpointer>(on_popups_toggled),
                        {{"command", "set_desktop_popups"}, {"enabled", gtk_switch_get_active(widget) == TRUE}});
        }

        void on_previews_toggled(GtkSwitch* widget, GParamSpec*, gpointer) {
            send_switch(widget,
                        reinterpret_cast<gpointer>(on_previews_toggled),
                        {{"command", "set_popup_previews"}, {"enabled", gtk_switch_get_active(widget) == TRUE}});
        }

        void on_calls_toggled(GtkSwitch* widget, GParamSpec*, gpointer) {
            send_switch(widget,
                        reinterpret_cast<gpointer>(on_calls_toggled),
                        {{"command", "bt_set_calls"}, {"enabled", gtk_switch_get_active(widget) == TRUE}});
        }

        void on_lock_away_toggled(GtkSwitch* widget, GParamSpec*, gpointer) {
            send_switch(widget,
                        reinterpret_cast<gpointer>(on_lock_away_toggled),
                        {{"command", "bt_set_lock_on_away"}, {"enabled", gtk_switch_get_active(widget) == TRUE}});
        }

        void on_clipboard_toggled(GtkSwitch* widget, GParamSpec*, gpointer) {
            send_switch(widget,
                        reinterpret_cast<gpointer>(on_clipboard_toggled),
                        {{"command", "set_clipboard_sync"}, {"enabled", gtk_switch_get_active(widget) == TRUE}});
        }

        void set_combo(GtkWidget* widget, gpointer handler, const std::string& id);

        void on_retention_changed(GtkComboBox* combo, gpointer) {
            const gchar* id = gtk_combo_box_get_active_id(combo);
            if (!id)
                return;
            const std::string previous = g_settings.bt_status.value("retention", "encrypted");
            if (std::string(id) == "none" && previous != "none") {
                // This one deletes what is stored, at once, with no way back.
                GtkWidget* dialog = gtk_message_dialog_new(GTK_WINDOW(g_settings.window),
                                                           GTK_DIALOG_MODAL,
                                                           GTK_MESSAGE_WARNING,
                                                           GTK_BUTTONS_NONE,
                                                           "%s",
                                                           _("Delete the stored message history?"));
                gtk_message_dialog_format_secondary_text(
                    GTK_MESSAGE_DIALOG(dialog),
                    "%s",
                    _("Messages and contacts already saved on this computer are deleted right away. The phone "
                      "keeps its own copy, and conversations are re-read from it while connected."));
                gtk_dialog_add_button(GTK_DIALOG(dialog), _("Cancel"), GTK_RESPONSE_CANCEL);
                GtkWidget* confirm = gtk_dialog_add_button(GTK_DIALOG(dialog), _("Delete history"), GTK_RESPONSE_OK);
                gtk_style_context_add_class(gtk_widget_get_style_context(confirm), "destructive-action");
                gtk_dialog_set_default_response(GTK_DIALOG(dialog), GTK_RESPONSE_CANCEL);
                const bool confirmed = gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_OK;
                gtk_widget_destroy(dialog);
                if (!confirmed) {
                    set_combo(GTK_WIDGET(combo), reinterpret_cast<gpointer>(on_retention_changed), previous);
                    return;
                }
            }
            if (!daemon_send({{"command", "bt_set_retention"}, {"retention", id}})) {
                set_combo(GTK_WIDGET(combo), reinterpret_cast<gpointer>(on_retention_changed), previous);
                show_toast(_("The Tether daemon is not running, so this setting was not changed."), ToastLevel::Error);
            }
        }

        void on_unmute_clicked(GtkButton* button, gpointer) {
            const char* app_id = static_cast<const char*>(g_object_get_data(G_OBJECT(button), "app_id"));
            if (!app_id)
                return;
            if (!daemon_send({{"command", "set_app_muted"}, {"app_id", app_id}, {"muted", false}})) {
                show_toast(_("The Tether daemon is not running, so this setting was not changed."), ToastLevel::Error);
                return;
            }
            gtk_widget_set_sensitive(GTK_WIDGET(button), FALSE);
        }

        void rebuild_muted_list() {
            if (!g_settings.muted_list)
                return;
            clear_list_box(g_settings.muted_list);
            const nlohmann::json apps = g_settings.bt_status.value("muted_apps", nlohmann::json::array());
            for (const auto& app : apps) {
                if (!app.is_string())
                    continue;
                const std::string app_id = app.get<std::string>();
                std::string name = bluetooth::ancs::derive_app_name(app_id);
                if (name.empty())
                    name = app_id;
                GtkWidget* unmute = gtk_button_new_with_label(_("Unmute"));
                g_object_set_data_full(G_OBJECT(unmute), "app_id", g_strdup(app_id.c_str()), g_free);
                g_signal_connect(unmute, "clicked", G_CALLBACK(on_unmute_clicked), nullptr);
                add_row(g_settings.muted_list, name, name == app_id ? "" : app_id, unmute);
            }
            gtk_widget_show_all(g_settings.muted_list);
            if (g_settings.muted_empty)
                gtk_widget_set_visible(g_settings.muted_empty, apps.empty());
        }

        // gtk_switch_set_active emits notify::active, so every programmatic write
        // has to be fenced or it echoes straight back to the daemon.
        void set_switch(GtkWidget* widget, gpointer handler, bool active) {
            g_signal_handlers_block_by_func(widget, handler, nullptr);
            gtk_switch_set_active(GTK_SWITCH(widget), active ? TRUE : FALSE);
            g_signal_handlers_unblock_by_func(widget, handler, nullptr);
        }

        void set_combo(GtkWidget* widget, gpointer handler, const std::string& id) {
            g_signal_handlers_block_by_func(widget, handler, nullptr);
            gtk_combo_box_set_active_id(GTK_COMBO_BOX(widget), id.c_str());
            g_signal_handlers_unblock_by_func(widget, handler, nullptr);
        }

        void sync_from_status() {
            const nlohmann::json& status = g_settings.bt_status;
            const bool available = status.value("available", false);
            const bool bonded = !status.value("device_address", std::string()).empty();
            const bool bt_on = status.value("enabled", true);
            const bool ancs_on = status.value("ancs_enabled", true);
            const bool usable = available && bonded && bt_on;

            gtk_widget_set_visible(g_settings.lbl_bt_hint, !available || !bonded);

            gtk_widget_set_sensitive(g_settings.row_ancs, usable);
            gtk_widget_set_sensitive(g_settings.row_content, usable && ancs_on);
            gtk_widget_set_sensitive(g_settings.row_retention, available && bonded);
            gtk_widget_set_sensitive(g_settings.row_calls, usable);

            set_switch(g_settings.sw_ancs, reinterpret_cast<gpointer>(on_ancs_toggled), ancs_on);
            set_switch(g_settings.sw_content,
                       reinterpret_cast<gpointer>(on_content_toggled),
                       status.value("ancs_content_enabled", true));
            set_switch(g_settings.sw_calls,
                       reinterpret_cast<gpointer>(on_calls_toggled),
                       status.value("calls_enabled", false));

            // Popups cover Wi-Fi file transfers too, so this one never depends on a bond.
            const bool popups_on = status.value("desktop_popups_enabled", true);
            set_switch(g_settings.sw_popups, reinterpret_cast<gpointer>(on_popups_toggled), popups_on);
            gtk_widget_set_sensitive(g_settings.row_previews, popups_on);
            set_switch(g_settings.sw_previews,
                       reinterpret_cast<gpointer>(on_previews_toggled),
                       status.value("popup_previews_enabled", true));

            set_switch(g_settings.sw_lock_away,
                       reinterpret_cast<gpointer>(on_lock_away_toggled),
                       status.value("lock_on_away", false));

            set_switch(g_settings.sw_clipboard,
                       reinterpret_cast<gpointer>(on_clipboard_toggled),
                       status.value("clipboard_sync_enabled", true));
            gtk_widget_set_sensitive(g_settings.row_clipboard, daemon_connected());

            rebuild_muted_list();

            const std::string retention = status.value("retention", "encrypted");
            set_combo(g_settings.cmb_retention, reinterpret_cast<gpointer>(on_retention_changed), retention);

            gtk_widget_set_visible(g_settings.lbl_keyring,
                                   retention == "encrypted" && !status.value("retention_ready", true));
        }

        void build_window() {
            GtkWidget* window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
            g_settings.window = window;
            gtk_window_set_title(GTK_WINDOW(window), _("Settings"));
            gtk_window_set_default_size(GTK_WINDOW(window), 540, 760);
            gtk_window_set_type_hint(GTK_WINDOW(window), GDK_WINDOW_TYPE_HINT_DIALOG);
            if (GtkWidget* parent = main_window())
                gtk_window_set_transient_for(GTK_WINDOW(window), GTK_WINDOW(parent));
            g_signal_connect(window, "delete-event", G_CALLBACK(gtk_widget_hide_on_delete), nullptr);
            // Escape and Ctrl+W close, after the focused control had its chance at the key.
            g_signal_connect_after(window,
                                   "key-press-event",
                                   G_CALLBACK(+[](GtkWidget* widget, GdkEventKey* event, gpointer) -> gboolean {
                                       const bool ctrl_w = (event->state & GDK_CONTROL_MASK) &&
                                                           gdk_keyval_to_lower(event->keyval) == GDK_KEY_w;
                                       if (event->keyval != GDK_KEY_Escape && !ctrl_w)
                                           return FALSE;
                                       gtk_widget_hide(widget);
                                       return TRUE;
                                   }),
                                   nullptr);
            g_signal_connect(
                window, "destroy", G_CALLBACK(+[](GtkWidget*, gpointer) { g_settings.window = nullptr; }), nullptr);

            GtkWidget* scroll = gtk_scrolled_window_new(nullptr, nullptr);
            gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
            gtk_container_add(GTK_CONTAINER(window), scroll);

            GtkWidget* column = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
            gtk_container_set_border_width(GTK_CONTAINER(column), 18);
            gtk_container_add(GTK_CONTAINER(scroll), column);

            g_settings.lbl_bt_hint = gtk_label_new(_("No iPhone is paired over Bluetooth, so the Bluetooth "
                                                     "settings below cannot be changed yet."));
            gtk_label_set_xalign(GTK_LABEL(g_settings.lbl_bt_hint), 0.0);
            gtk_label_set_line_wrap(GTK_LABEL(g_settings.lbl_bt_hint), TRUE);
            gtk_style_context_add_class(gtk_widget_get_style_context(g_settings.lbl_bt_hint), "tether-setup");
            gtk_widget_set_no_show_all(g_settings.lbl_bt_hint, TRUE);
            gtk_box_pack_start(GTK_BOX(column), g_settings.lbl_bt_hint, FALSE, FALSE, 0);

            // General
            GtkWidget* general = add_group(column, _("General"), "");

            g_settings.sw_tray = new_switch();
            gtk_switch_set_active(GTK_SWITCH(g_settings.sw_tray), tray_close_to_tray() ? TRUE : FALSE);
            g_signal_connect(g_settings.sw_tray, "notify::active", G_CALLBACK(on_tray_toggled), nullptr);
            add_row(general,
                    _("Keep running in the tray when the window is closed"),
                    _("Closing the window hides it instead of quitting Tether."),
                    g_settings.sw_tray);

            g_settings.cmb_tray_icon = gtk_combo_box_text_new();
            gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(g_settings.cmb_tray_icon), "symbolic", _("Symbolic"));
            gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(g_settings.cmb_tray_icon), "color", _("Color"));
            gtk_combo_box_set_active_id(GTK_COMBO_BOX(g_settings.cmb_tray_icon),
                                        prefs().value("tray_icon", std::string("symbolic")).c_str());
            g_signal_connect(g_settings.cmb_tray_icon, "changed", G_CALLBACK(on_tray_icon_changed), nullptr);
            add_row(general,
                    _("Tray icon"),
                    _("Symbolic follows the panel's color. Takes effect the next time Tether starts."),
                    g_settings.cmb_tray_icon);

            // Clipboard
            GtkWidget* clipboard = add_group(
                column, _("Clipboard"), _("Over Wi-Fi, what is copied here goes to the iPhone and back."));
            g_settings.sw_clipboard = new_switch();
            g_signal_connect(g_settings.sw_clipboard, "notify::active", G_CALLBACK(on_clipboard_toggled), nullptr);
            g_settings.row_clipboard =
                add_row(clipboard,
                        _("Sync the clipboard automatically"),
                        _("Off pauses it both ways; Send Clipboard on the Devices page still works. Copies a "
                          "password manager marks as sensitive are never sent."),
                        g_settings.sw_clipboard);

            // iPhone notifications
            GtkWidget* ancs = add_group(
                column, _("iPhone notifications"), _("Alerts your iPhone forwards to this computer over Bluetooth."));

            g_settings.sw_ancs = new_switch();
            g_signal_connect(g_settings.sw_ancs, "notify::active", G_CALLBACK(on_ancs_toggled), nullptr);
            g_settings.row_ancs = add_row(ancs,
                                          _("Mirror iPhone notifications"),
                                          _("Disabled when pairing cannot support LE."),
                                          g_settings.sw_ancs);

            g_settings.sw_content = new_switch();
            g_signal_connect(g_settings.sw_content, "notify::active", G_CALLBACK(on_content_toggled), nullptr);
            g_settings.row_content = add_row(ancs,
                                             _("Include the notification text"),
                                             _("Title and message content are shown, not just which app sent "
                                               "it."),
                                             g_settings.sw_content);

            // Desktop popups
            GtkWidget* popups =
                add_group(column, _("Desktop popups"), _("What Tether shows in your notification area."));

            g_settings.sw_popups = new_switch();
            g_signal_connect(g_settings.sw_popups, "notify::active", G_CALLBACK(on_popups_toggled), nullptr);
            add_row(popups,
                    _("Show desktop popups"),
                    _("Off silences iPhone alerts, new messages and arriving files."),
                    g_settings.sw_popups);

            g_settings.sw_previews = new_switch();
            g_signal_connect(g_settings.sw_previews, "notify::active", G_CALLBACK(on_previews_toggled), nullptr);
            g_settings.row_previews =
                add_row(popups, _("Show message previews"), _("Off shows only the sender."), g_settings.sw_previews);

            // Muted apps: filled from bt_status. The Mute button on a popup adds to it.
            g_settings.muted_list = add_group(column,
                                              _("Muted apps"),
                                              _("iPhone apps whose alerts show no popup here. They still appear "
                                                "on the Notifications tab. Mute one from its popup."));
            g_settings.muted_empty = gtk_label_new(_("No apps are muted."));
            gtk_label_set_xalign(GTK_LABEL(g_settings.muted_empty), 0.0);
            gtk_style_context_add_class(gtk_widget_get_style_context(g_settings.muted_empty), "muted");
            gtk_widget_set_no_show_all(g_settings.muted_empty, TRUE);
            gtk_box_pack_start(GTK_BOX(column), g_settings.muted_empty, FALSE, FALSE, 0);

            // Security
            GtkWidget* security = add_group(column, _("Security"), _("What Tether does when the iPhone leaves."));

            g_settings.sw_lock_away = new_switch();
            g_signal_connect(g_settings.sw_lock_away, "notify::active", G_CALLBACK(on_lock_away_toggled), nullptr);
            add_row(security,
                    _("Lock the session when the iPhone goes out of range"),
                    _("Locks about half a minute after the link times out. Switching Bluetooth off on the "
                      "iPhone does not lock, and neither does suspending this machine."),
                    g_settings.sw_lock_away);

            // Messages
            GtkWidget* messages =
                add_group(column, _("Messages"), _("SMS and iMessage conversations synced over Bluetooth."));

            g_settings.cmb_retention = gtk_combo_box_text_new();
            gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(g_settings.cmb_retention), "encrypted", _("Encrypted"));
            gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(g_settings.cmb_retention), "plaintext", _("Unencrypted"));
            gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(g_settings.cmb_retention), "none", _("Do not keep"));
            g_signal_connect(g_settings.cmb_retention, "changed", G_CALLBACK(on_retention_changed), nullptr);
            g_settings.row_retention = add_row(messages,
                                               _("Keep message history"),
                                               _("Encrypted uses a key from your desktop keyring. Do not keep "
                                                 "deletes what is stored."),
                                               g_settings.cmb_retention);

            g_settings.lbl_keyring =
                gtk_label_new(_("Paused: no key from the desktop keyring. Unlock it, or choose Unencrypted."));
            gtk_label_set_xalign(GTK_LABEL(g_settings.lbl_keyring), 0.0);
            gtk_label_set_line_wrap(GTK_LABEL(g_settings.lbl_keyring), TRUE);
            gtk_style_context_add_class(gtk_widget_get_style_context(g_settings.lbl_keyring), "muted");
            gtk_widget_set_no_show_all(g_settings.lbl_keyring, TRUE);
            gtk_box_pack_start(GTK_BOX(column), g_settings.lbl_keyring, FALSE, FALSE, 0);

            // Experimental
            GtkWidget* experimental =
                add_group(column, _("Experimental"), _("Unfinished features. These can stop working at any time."));

            g_settings.sw_calls = new_switch();
            g_signal_connect(g_settings.sw_calls, "notify::active", G_CALLBACK(on_calls_toggled), nullptr);
            g_settings.row_calls = add_row(experimental,
                                           _("Call control over Bluetooth"),
                                           _("Answer and place calls from the Calls tab."),
                                           g_settings.sw_calls);

            sync_from_status();
        }

    } // namespace

    void settings_window_show() {
        if (!g_settings.window)
            build_window();
        gtk_widget_show_all(g_settings.window);
        gtk_window_present(GTK_WINDOW(g_settings.window));
        // The window can open before any broadcast has arrived.
        daemon_send({{"command", "bt_status"}});
    }

    void settings_handle_event(const nlohmann::json& event) {
        if (event.value("command", std::string()) != "bt_status")
            return;
        g_settings.bt_status = event;
        if (g_settings.window)
            sync_from_status();
    }

} // namespace tether::ui
