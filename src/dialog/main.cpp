#include <gtk-layer-shell.h>
#include <gtk/gtk.h>
#include <gio/gio.h>

#include "../core/include/tether/log.hpp"
#include <cstdlib>
#include <cstring>
#include <string>

// Exit codes:
//   0 = accept button clicked
//   1 = reject button clicked
//   2 = timeout expired
//   3 = error (bad args, display failure, etc.)
static int exit_code = 3;

static void on_accept(GtkWidget*, gpointer) {
    exit_code = 0;
    gtk_main_quit();
}

static void on_reject(GtkWidget*, gpointer) {
    exit_code = 1;
    gtk_main_quit();
}

static gboolean on_timeout(gpointer) {
    exit_code = 2;
    gtk_main_quit();
    return FALSE;
}

// Enter is left to the focused button, so it never accepts while Reject has focus.
static gboolean on_key_press(GtkWidget*, GdkEventKey* event, gpointer has_reject) {
    if (event->keyval == GDK_KEY_Escape && GPOINTER_TO_INT(has_reject)) {
        exit_code = 1;
        gtk_main_quit();
        return TRUE;
    }
    return FALSE;
}

static void print_usage(const char* argv0) {
    debug::log(ERR,
               "Usage: {} --title <text> --body <text> --accept <label> [--reject <label>] [--timeout <seconds>]\n",
               argv0);
}

// Theme colours only: the prompt follows the GTK theme, light or dark, and a
// high-contrast theme keeps its contrast. The buttons are stock GTK buttons,
// with Accept marked as the suggested action the way the theme draws it.
static const char* CSS = R"(
    window {
        background-color: transparent;
    }
    .dialog-frame {
        background-color: @theme_bg_color;
        border: 1px solid alpha(@theme_fg_color, 0.25);
        border-radius: 16px;
        padding: 28px 32px;
        margin: 12px;
    }
    .dialog-title {
        font-size: 16px;
        font-weight: 700;
        color: @theme_fg_color;
        margin-bottom: 10px;
    }
    .dialog-body {
        font-size: 13px;
        color: alpha(@theme_fg_color, 0.8);
        margin-bottom: 22px;
    }
    .dialog-btn-row {
        margin-top: 4px;
    }
    .btn-accept, .btn-reject {
        padding: 8px 22px;
        min-width: 90px;
    }
)";

// xdg-desktop-portal Settings: 0 = no preference, 1 = dark, 2 = light. A
// one-shot prompt reads it once; it is gone before the setting can change.
static void follow_system_color_scheme() {
    if (g_getenv("GTK_THEME"))
        return;
    GError* error = nullptr;
    GDBusProxy* proxy = g_dbus_proxy_new_for_bus_sync(G_BUS_TYPE_SESSION,
                                                      G_DBUS_PROXY_FLAGS_DO_NOT_LOAD_PROPERTIES,
                                                      nullptr,
                                                      "org.freedesktop.portal.Desktop",
                                                      "/org/freedesktop/portal/desktop",
                                                      "org.freedesktop.portal.Settings",
                                                      nullptr,
                                                      &error);
    if (!proxy) {
        g_clear_error(&error);
        return;
    }
    GVariant* result = g_dbus_proxy_call_sync(proxy,
                                              "Read",
                                              g_variant_new("(ss)", "org.freedesktop.appearance", "color-scheme"),
                                              G_DBUS_CALL_FLAGS_NONE,
                                              500,
                                              nullptr,
                                              &error);
    if (result) {
        GVariant* outer = nullptr;
        g_variant_get(result, "(v)", &outer);
        GVariant* inner = g_variant_get_variant(outer);
        if (g_variant_is_of_type(inner, G_VARIANT_TYPE_UINT32)) {
            const guint32 scheme = g_variant_get_uint32(inner);
            if (scheme == 1 || scheme == 2) {
                if (GtkSettings* settings = gtk_settings_get_default())
                    g_object_set(settings, "gtk-application-prefer-dark-theme", scheme == 1 ? TRUE : FALSE, nullptr);
            }
        }
        g_variant_unref(inner);
        g_variant_unref(outer);
        g_variant_unref(result);
    }
    g_clear_error(&error);
    g_object_unref(proxy);
}

int main(int argc, char** argv) {
    std::string title, body, accept_label, reject_label;
    int timeout_secs = 0;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--title" && i + 1 < argc) {
            title = argv[++i];
        } else if (arg == "--body" && i + 1 < argc) {
            body = argv[++i];
        } else if (arg == "--accept" && i + 1 < argc) {
            accept_label = argv[++i];
        } else if (arg == "--reject" && i + 1 < argc) {
            reject_label = argv[++i];
        } else if (arg == "--timeout" && i + 1 < argc) {
            timeout_secs = std::atoi(argv[++i]);
        } else if (arg == "--help" || arg == "-h") {
            print_usage(argv[0]);
            return 0;
        }
    }

    if (title.empty() || body.empty() || accept_label.empty()) {
        debug::log(ERR, "Error: --title, --body, and --accept are required.\n");
        print_usage(argv[0]);
        return 3;
    }

    if (!gtk_init_check(&argc, &argv)) {
        debug::log(ERR, "Error: no display available.\n");
        return 3;
    }

    follow_system_color_scheme();

    // Apply CSS
    GtkCssProvider* css_provider = gtk_css_provider_new();
    gtk_css_provider_load_from_data(css_provider, CSS, -1, NULL);
    gtk_style_context_add_provider_for_screen(
        gdk_screen_get_default(), GTK_STYLE_PROVIDER(css_provider), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_unref(css_provider);

    // Create window
    GtkWidget* window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    // Undecorated, but screen readers still announce the title.
    gtk_window_set_title(GTK_WINDOW(window), title.c_str());
    gtk_window_set_default_size(GTK_WINDOW(window), 380, -1);
    gtk_window_set_decorated(GTK_WINDOW(window), FALSE);

    // Layer shell setup. fall back to an ordinary centered window.
    if (gtk_layer_is_supported()) {
        gtk_layer_init_for_window(GTK_WINDOW(window));
        gtk_layer_set_layer(GTK_WINDOW(window), GTK_LAYER_SHELL_LAYER_OVERLAY);
        gtk_layer_set_keyboard_mode(GTK_WINDOW(window), GTK_LAYER_SHELL_KEYBOARD_MODE_EXCLUSIVE);
        gtk_layer_set_namespace(GTK_WINDOW(window), "tether-dialog");
        // No anchors = centered
    } else {
        gtk_window_set_position(GTK_WINDOW(window), GTK_WIN_POS_CENTER);
        gtk_window_set_keep_above(GTK_WINDOW(window), TRUE);
    }

    // Main container
    GtkWidget* frame = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    GtkStyleContext* frame_ctx = gtk_widget_get_style_context(frame);
    gtk_style_context_add_class(frame_ctx, "dialog-frame");
    gtk_container_add(GTK_CONTAINER(window), frame);

    // Title
    GtkWidget* lbl_title = gtk_label_new(title.c_str());
    gtk_label_set_xalign(GTK_LABEL(lbl_title), 0.0);
    gtk_label_set_line_wrap(GTK_LABEL(lbl_title), TRUE);
    gtk_style_context_add_class(gtk_widget_get_style_context(lbl_title), "dialog-title");
    gtk_box_pack_start(GTK_BOX(frame), lbl_title, FALSE, FALSE, 0);

    // Body
    GtkWidget* lbl_body = gtk_label_new(body.c_str());
    gtk_label_set_xalign(GTK_LABEL(lbl_body), 0.0);
    gtk_label_set_line_wrap(GTK_LABEL(lbl_body), TRUE);
    gtk_label_set_max_width_chars(GTK_LABEL(lbl_body), 50);
    gtk_style_context_add_class(gtk_widget_get_style_context(lbl_body), "dialog-body");
    gtk_box_pack_start(GTK_BOX(frame), lbl_body, FALSE, FALSE, 0);

    // Button row
    GtkWidget* btn_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_style_context_add_class(gtk_widget_get_style_context(btn_row), "dialog-btn-row");
    gtk_widget_set_halign(btn_row, GTK_ALIGN_END);
    gtk_box_pack_start(GTK_BOX(frame), btn_row, FALSE, FALSE, 0);

    bool has_reject = !reject_label.empty();
    GtkWidget* btn_reject = nullptr;

    if (has_reject) {
        btn_reject = gtk_button_new_with_label(reject_label.c_str());
        gtk_style_context_add_class(gtk_widget_get_style_context(btn_reject), "btn-reject");
        g_signal_connect(btn_reject, "clicked", G_CALLBACK(on_reject), NULL);
        gtk_box_pack_start(GTK_BOX(btn_row), btn_reject, FALSE, FALSE, 0);
    }

    GtkWidget* btn_accept = gtk_button_new_with_label(accept_label.c_str());
    gtk_style_context_add_class(gtk_widget_get_style_context(btn_accept), "btn-accept");
    gtk_style_context_add_class(gtk_widget_get_style_context(btn_accept), "suggested-action");
    g_signal_connect(btn_accept, "clicked", G_CALLBACK(on_accept), NULL);
    gtk_box_pack_start(GTK_BOX(btn_row), btn_accept, FALSE, FALSE, 0);

    // The window grabs the keyboard when it appears, so a stray Enter typed for
    // another app must land on the safe choice.
    gtk_widget_grab_focus(btn_reject ? btn_reject : btn_accept);

    AtkObject* window_a11y = gtk_widget_get_accessible(window);
    atk_object_set_role(window_a11y, ATK_ROLE_ALERT);
    atk_object_add_relationship(window_a11y, ATK_RELATION_DESCRIBED_BY, gtk_widget_get_accessible(lbl_body));

    // Keyboard shortcuts
    g_signal_connect(window, "key-press-event", G_CALLBACK(on_key_press), GINT_TO_POINTER(has_reject));

    // Auto-dismiss timeout
    if (timeout_secs > 0) {
        g_timeout_add_seconds(timeout_secs, on_timeout, NULL);
    }

    g_signal_connect(window, "destroy", G_CALLBACK(gtk_main_quit), NULL);

    gtk_widget_show_all(window);
    gtk_main();

    return exit_code;
}
