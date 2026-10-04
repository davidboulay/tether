#include "toast.hpp"

#include "ui_util.hpp"

#include <ctime>
#include <deque>
#include <string>
#include <tether/i18n.hpp>

namespace tether::ui {

    namespace {

        struct Entry {
            std::time_t when = 0;
            std::string text;
            ToastLevel level = ToastLevel::Info;
        };

        struct ToastState {
            GtkWidget* revealer = nullptr;
            GtkWidget* frame = nullptr;
            GtkWidget* label = nullptr;
            GtkWidget* icon = nullptr;
            guint hide_id = 0;
            ToastLevel showing = ToastLevel::Info;
            std::deque<Entry> queue;

            std::deque<Entry> history;
            int unseen_errors = 0;
            GtkWidget* history_button = nullptr;
            GtkWidget* history_icon = nullptr;
            GtkWidget* history_list = nullptr;
            GtkWidget* history_empty = nullptr;
        };

        ToastState g_toast;

        constexpr size_t HISTORY_LIMIT = 50;
        constexpr size_t QUEUE_LIMIT = 6;

        const char* level_class(ToastLevel level) {
            switch (level) {
            case ToastLevel::Success:
                return "tether-toast-success";
            case ToastLevel::Error:
                return "tether-toast-error";
            case ToastLevel::Info:
                break;
            }
            return "tether-toast-info";
        }

        const char* level_icon(ToastLevel level) {
            switch (level) {
            case ToastLevel::Success:
                return "emblem-ok-symbolic";
            case ToastLevel::Error:
                return "dialog-warning-symbolic";
            case ToastLevel::Info:
                break;
            }
            return "dialog-information-symbolic";
        }

        int level_seconds(ToastLevel level) {
            switch (level) {
            case ToastLevel::Success:
                return 3;
            case ToastLevel::Error:
                return 9;
            case ToastLevel::Info:
                break;
            }
            return 4;
        }

        void update_history_icon() {
            if (!g_toast.history_icon)
                return;
            gtk_image_set_from_icon_name(GTK_IMAGE(g_toast.history_icon),
                                         g_toast.unseen_errors > 0 ? "dialog-warning-symbolic"
                                                                   : "document-open-recent-symbolic",
                                         GTK_ICON_SIZE_BUTTON);
            if (g_toast.history_button) {
                const std::string name =
                    g_toast.unseen_errors > 0
                        ? tr_format(P_("Recent events, {} unread error",
                                       "Recent events, {} unread errors",
                                       g_toast.unseen_errors),
                                    g_toast.unseen_errors)
                        : _("Recent events");
                gtk_widget_set_tooltip_text(g_toast.history_button, name.c_str());
                set_accessible_name(g_toast.history_button, name);
            }
        }

        std::string format_when(std::time_t when) {
            std::tm tm{};
            localtime_r(&when, &tm);
            char buffer[32];
            // xgettext:no-c-format
            std::strftime(buffer, sizeof(buffer), _("%H:%M"), &tm);
            return buffer;
        }

        void rebuild_history_list() {
            if (!g_toast.history_list)
                return;
            clear_list_box(g_toast.history_list);
            for (const Entry& entry : g_toast.history) {
                GtkWidget* row = gtk_list_box_row_new();
                gtk_list_box_row_set_activatable(GTK_LIST_BOX_ROW(row), FALSE);
                gtk_list_box_row_set_selectable(GTK_LIST_BOX_ROW(row), FALSE);
                GtkWidget* box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
                gtk_container_set_border_width(GTK_CONTAINER(box), 6);
                GtkWidget* icon = gtk_image_new_from_icon_name(level_icon(entry.level), GTK_ICON_SIZE_MENU);
                gtk_widget_set_valign(icon, GTK_ALIGN_START);
                gtk_box_pack_start(GTK_BOX(box), icon, FALSE, FALSE, 0);
                GtkWidget* text = gtk_label_new(entry.text.c_str());
                gtk_label_set_xalign(GTK_LABEL(text), 0.0);
                gtk_label_set_line_wrap(GTK_LABEL(text), TRUE);
                gtk_label_set_line_wrap_mode(GTK_LABEL(text), PANGO_WRAP_WORD_CHAR);
                gtk_label_set_max_width_chars(GTK_LABEL(text), 44);
                gtk_label_set_selectable(GTK_LABEL(text), TRUE);
                gtk_box_pack_start(GTK_BOX(box), text, TRUE, TRUE, 0);
                GtkWidget* when = gtk_label_new(format_when(entry.when).c_str());
                gtk_widget_set_valign(when, GTK_ALIGN_START);
                gtk_style_context_add_class(gtk_widget_get_style_context(when), "muted");
                gtk_box_pack_end(GTK_BOX(box), when, FALSE, FALSE, 0);
                gtk_container_add(GTK_CONTAINER(row), box);
                gtk_list_box_insert(GTK_LIST_BOX(g_toast.history_list), row, -1);
            }
            gtk_widget_show_all(g_toast.history_list);
            if (g_toast.history_empty)
                gtk_widget_set_visible(g_toast.history_empty, g_toast.history.empty());
        }

        void show_next();

        gboolean on_hide_timeout(gpointer) {
            g_toast.hide_id = 0;
            gtk_revealer_set_reveal_child(GTK_REVEALER(g_toast.revealer), FALSE);
            if (!g_toast.queue.empty()) {
                // Let the slide-out finish before the next slides in.
                g_toast.hide_id = g_timeout_add(
                    260,
                    [](gpointer) -> gboolean {
                        g_toast.hide_id = 0;
                        show_next();
                        return G_SOURCE_REMOVE;
                    },
                    nullptr);
            }
            return G_SOURCE_REMOVE;
        }

        void show_entry(const Entry& entry) {
            GtkStyleContext* context = gtk_widget_get_style_context(g_toast.frame);
            for (ToastLevel level : {ToastLevel::Info, ToastLevel::Success, ToastLevel::Error})
                gtk_style_context_remove_class(context, level_class(level));
            gtk_style_context_add_class(context, level_class(entry.level));
            gtk_image_set_from_icon_name(GTK_IMAGE(g_toast.icon), level_icon(entry.level), GTK_ICON_SIZE_BUTTON);
            set_text(g_toast.label, entry.text);
            g_toast.showing = entry.level;
            gtk_revealer_set_reveal_child(GTK_REVEALER(g_toast.revealer), TRUE);
            if (g_toast.hide_id != 0)
                g_source_remove(g_toast.hide_id);
            g_toast.hide_id = g_timeout_add_seconds(level_seconds(entry.level), on_hide_timeout, nullptr);
        }

        void show_next() {
            if (g_toast.queue.empty())
                return;
            const Entry entry = g_toast.queue.front();
            g_toast.queue.pop_front();
            show_entry(entry);
        }

        void on_close_clicked(GtkWidget*, gpointer) {
            if (g_toast.hide_id != 0) {
                g_source_remove(g_toast.hide_id);
                g_toast.hide_id = 0;
            }
            on_hide_timeout(nullptr);
        }

        void on_history_opened(GtkWidget*, gpointer) {
            g_toast.unseen_errors = 0;
            update_history_icon();
            rebuild_history_list();
        }

        void on_history_clear(GtkWidget*, gpointer) {
            g_toast.history.clear();
            g_toast.unseen_errors = 0;
            update_history_icon();
            rebuild_history_list();
        }

    } // namespace

    GtkWidget* create_toast_overlay(GtkWidget* content) {
        GtkWidget* overlay = gtk_overlay_new();
        gtk_container_add(GTK_CONTAINER(overlay), content);

        g_toast.revealer = gtk_revealer_new();
        gtk_revealer_set_transition_type(GTK_REVEALER(g_toast.revealer), GTK_REVEALER_TRANSITION_TYPE_SLIDE_DOWN);
        gtk_revealer_set_transition_duration(GTK_REVEALER(g_toast.revealer), 220);
        gtk_widget_set_halign(g_toast.revealer, GTK_ALIGN_CENTER);
        gtk_widget_set_valign(g_toast.revealer, GTK_ALIGN_START);
        gtk_widget_set_margin_top(g_toast.revealer, 10);

        g_toast.frame = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
        gtk_style_context_add_class(gtk_widget_get_style_context(g_toast.frame), "tether-toast");
        g_toast.icon = gtk_image_new_from_icon_name("dialog-information-symbolic", GTK_ICON_SIZE_BUTTON);
        gtk_box_pack_start(GTK_BOX(g_toast.frame), g_toast.icon, FALSE, FALSE, 0);
        g_toast.label = gtk_label_new(nullptr);
        gtk_label_set_line_wrap(GTK_LABEL(g_toast.label), TRUE);
        gtk_label_set_line_wrap_mode(GTK_LABEL(g_toast.label), PANGO_WRAP_WORD_CHAR);
        gtk_label_set_max_width_chars(GTK_LABEL(g_toast.label), 60);
        gtk_label_set_xalign(GTK_LABEL(g_toast.label), 0.0);
        // Announced when it changes, so a screen reader hears the notice.
        atk_object_set_role(gtk_widget_get_accessible(g_toast.label), ATK_ROLE_NOTIFICATION);
        gtk_box_pack_start(GTK_BOX(g_toast.frame), g_toast.label, TRUE, TRUE, 0);
        GtkWidget* close = gtk_button_new_from_icon_name("window-close-symbolic", GTK_ICON_SIZE_BUTTON);
        gtk_button_set_relief(GTK_BUTTON(close), GTK_RELIEF_NONE);
        set_accessible_name(close, _("Dismiss"));
        g_signal_connect(close, "clicked", G_CALLBACK(on_close_clicked), nullptr);
        gtk_box_pack_end(GTK_BOX(g_toast.frame), close, FALSE, FALSE, 0);

        gtk_container_add(GTK_CONTAINER(g_toast.revealer), g_toast.frame);
        gtk_overlay_add_overlay(GTK_OVERLAY(overlay), g_toast.revealer);
        // The slide-in must not steal a click aimed at what is under it.
        gtk_overlay_set_overlay_pass_through(GTK_OVERLAY(overlay), g_toast.revealer, FALSE);
        return overlay;
    }

    void show_toast(const std::string& text, ToastLevel level) {
        if (text.empty())
            return;

        Entry entry{std::time(nullptr), text, level};
        g_toast.history.push_front(entry);
        while (g_toast.history.size() > HISTORY_LIMIT)
            g_toast.history.pop_back();
        if (level == ToastLevel::Error)
            g_toast.unseen_errors += 1;
        update_history_icon();

        if (!g_toast.revealer)
            return;

        const bool busy = gtk_revealer_get_reveal_child(GTK_REVEALER(g_toast.revealer));
        if (!busy) {
            show_entry(entry);
            return;
        }
        // An error waits for nothing; everything else queues behind what is up.
        if (level == ToastLevel::Error && g_toast.showing != ToastLevel::Error) {
            show_entry(entry);
            return;
        }
        if (g_toast.queue.size() >= QUEUE_LIMIT)
            g_toast.queue.pop_front();
        g_toast.queue.push_back(entry);
    }

    GtkWidget* create_history_button() {
        GtkWidget* button = gtk_menu_button_new();
        g_toast.history_button = button;
        g_toast.history_icon = gtk_image_new_from_icon_name("document-open-recent-symbolic", GTK_ICON_SIZE_BUTTON);
        gtk_button_set_image(GTK_BUTTON(button), g_toast.history_icon);

        GtkWidget* popover = gtk_popover_new(button);
        gtk_menu_button_set_popover(GTK_MENU_BUTTON(button), popover);

        GtkWidget* column = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
        gtk_container_set_border_width(GTK_CONTAINER(column), 8);

        GtkWidget* header = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
        GtkWidget* title = gtk_label_new(nullptr);
        set_markup(title, "<b>" + escape_markup(_("Recent events")) + "</b>");
        gtk_label_set_xalign(GTK_LABEL(title), 0.0);
        gtk_box_pack_start(GTK_BOX(header), title, TRUE, TRUE, 0);
        GtkWidget* clear = gtk_button_new_with_label(_("Clear"));
        gtk_button_set_relief(GTK_BUTTON(clear), GTK_RELIEF_NONE);
        g_signal_connect(clear, "clicked", G_CALLBACK(on_history_clear), nullptr);
        gtk_box_pack_end(GTK_BOX(header), clear, FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(column), header, FALSE, FALSE, 0);

        g_toast.history_empty = gtk_label_new(_("Nothing has happened yet."));
        gtk_style_context_add_class(gtk_widget_get_style_context(g_toast.history_empty), "muted");
        gtk_widget_set_no_show_all(g_toast.history_empty, TRUE);
        gtk_box_pack_start(GTK_BOX(column), g_toast.history_empty, FALSE, FALSE, 0);

        GtkWidget* scroll = gtk_scrolled_window_new(nullptr, nullptr);
        gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
        gtk_scrolled_window_set_max_content_height(GTK_SCROLLED_WINDOW(scroll), 360);
        gtk_scrolled_window_set_propagate_natural_height(GTK_SCROLLED_WINDOW(scroll), TRUE);
        gtk_widget_set_size_request(scroll, 380, -1);
        g_toast.history_list = gtk_list_box_new();
        gtk_list_box_set_selection_mode(GTK_LIST_BOX(g_toast.history_list), GTK_SELECTION_NONE);
        gtk_container_add(GTK_CONTAINER(scroll), g_toast.history_list);
        gtk_box_pack_start(GTK_BOX(column), scroll, TRUE, TRUE, 0);

        gtk_container_add(GTK_CONTAINER(popover), column);
        gtk_widget_show_all(column);
        g_signal_connect(popover, "show", G_CALLBACK(on_history_opened), nullptr);

        update_history_icon();
        rebuild_history_list();
        return button;
    }

} // namespace tether::ui
