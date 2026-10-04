#include "tray.hpp"

#include "daemon_client.hpp"
#include "prefs.hpp"
#include "settings_view.hpp"
#include <tether/i18n.hpp>

#include <gio/gio.h>
#include <gtk/gtk.h>
#include <nlohmann/json.hpp>
#include <tether/log.hpp>
#include <unistd.h>
#include <vector>

namespace tether::ui {

    namespace {

        // StatusNotifierItem: the tray protocol, spoken directly over D-Bus.
        constexpr const char* SNI_INTERFACE = "org.kde.StatusNotifierItem";
        constexpr const char* SNI_PATH = "/StatusNotifierItem";
        constexpr const char* WATCHER_NAME = "org.kde.StatusNotifierWatcher";
        constexpr const char* WATCHER_PATH = "/StatusNotifierWatcher";
        constexpr const char* MENU_INTERFACE = "com.canonical.dbusmenu";
        constexpr const char* MENU_PATH = "/MenuBar";
        constexpr int NO_HOST_GRACE_SECONDS = 6;

        // The menu the host draws on right-click, spoken as com.canonical.dbusmenu.
        constexpr const char* MENU_INTROSPECTION = R"XML(
<node>
  <interface name="com.canonical.dbusmenu">
    <property name="Version" type="u" access="read"/>
    <property name="TextDirection" type="s" access="read"/>
    <property name="Status" type="s" access="read"/>
    <property name="IconThemePath" type="as" access="read"/>
    <method name="GetLayout">
      <arg name="parentId" type="i" direction="in"/>
      <arg name="recursionDepth" type="i" direction="in"/>
      <arg name="propertyNames" type="as" direction="in"/>
      <arg name="revision" type="u" direction="out"/>
      <arg name="layout" type="(ia{sv}av)" direction="out"/>
    </method>
    <method name="GetGroupProperties">
      <arg name="ids" type="ai" direction="in"/>
      <arg name="propertyNames" type="as" direction="in"/>
      <arg name="properties" type="a(ia{sv})" direction="out"/>
    </method>
    <method name="GetProperty">
      <arg name="id" type="i" direction="in"/>
      <arg name="name" type="s" direction="in"/>
      <arg name="value" type="v" direction="out"/>
    </method>
    <method name="Event">
      <arg name="id" type="i" direction="in"/>
      <arg name="eventId" type="s" direction="in"/>
      <arg name="data" type="v" direction="in"/>
      <arg name="timestamp" type="u" direction="in"/>
    </method>
    <method name="EventGroup">
      <arg name="events" type="a(isvu)" direction="in"/>
      <arg name="idErrors" type="ai" direction="out"/>
    </method>
    <method name="AboutToShow">
      <arg name="id" type="i" direction="in"/>
      <arg name="needUpdate" type="b" direction="out"/>
    </method>
    <method name="AboutToShowGroup">
      <arg name="ids" type="ai" direction="in"/>
      <arg name="updatesNeeded" type="ai" direction="out"/>
      <arg name="idErrors" type="ai" direction="out"/>
    </method>
    <signal name="ItemsPropertiesUpdated">
      <arg name="updatedProps" type="a(ia{sv})"/>
      <arg name="removedProps" type="a(ias)"/>
    </signal>
    <signal name="LayoutUpdated">
      <arg name="revision" type="u"/>
      <arg name="parent" type="i"/>
    </signal>
    <signal name="ItemActivationRequested">
      <arg name="id" type="i"/>
      <arg name="timestamp" type="u"/>
    </signal>
  </interface>
</node>
)XML";

        enum MenuItem : int {
            MENU_ROOT = 0,
            MENU_TOGGLE_WINDOW = 1,
            MENU_CLIPBOARD = 2,
            MENU_SEPARATOR = 3,
            MENU_SETTINGS = 4,
            MENU_QUIT = 5,
        };

        constexpr const char* INTROSPECTION = R"XML(
<node>
  <interface name="org.kde.StatusNotifierItem">
    <property name="Category" type="s" access="read"/>
    <property name="Id" type="s" access="read"/>
    <property name="Title" type="s" access="read"/>
    <property name="Status" type="s" access="read"/>
    <property name="IconName" type="s" access="read"/>
    <property name="ItemIsMenu" type="b" access="read"/>
    <property name="Menu" type="o" access="read"/>
    <property name="ToolTip" type="(sa(iiay)ss)" access="read"/>
    <method name="Activate">
      <arg name="x" type="i" direction="in"/>
      <arg name="y" type="i" direction="in"/>
    </method>
    <method name="SecondaryActivate">
      <arg name="x" type="i" direction="in"/>
      <arg name="y" type="i" direction="in"/>
    </method>
    <method name="ContextMenu">
      <arg name="x" type="i" direction="in"/>
      <arg name="y" type="i" direction="in"/>
    </method>
    <method name="Scroll">
      <arg name="delta" type="i" direction="in"/>
      <arg name="orientation" type="s" direction="in"/>
    </method>
    <signal name="NewIcon"/>
    <signal name="NewToolTip"/>
    <signal name="NewStatus"><arg name="status" type="s"/></signal>
  </interface>
</node>
)XML";

        struct RouteState {
            bool ok = false;
            std::string detail;
        };

        GDBusConnection* g_bus = nullptr;
        GDBusNodeInfo* g_node = nullptr;
        GDBusNodeInfo* g_menu_node = nullptr;
        std::string g_bus_name;
        guint g_own_id = 0;
        guint g_watch_id = 0;
        guint g_no_host_id = 0;
        bool g_exported = false;
        bool g_menu_exported = false;
        bool g_host_seen = false;
        guint g_menu_revision = 1;
        bool g_clipboard_sync = true;
        std::function<void()> g_on_no_host;

        RouteState g_state[2];
        std::string g_icon = "tether-offline";
        int g_unread = 0;
        int g_pending_pairs = 0;
        std::string g_tooltip;
        std::string g_airpods_name;
        std::string g_airpods_battery;
        bool g_close_to_tray = false;
        std::string g_icon_style = "symbolic";
        std::string g_icon_suffix;

        RouteState& state(Route route) { return g_state[route == Route::WiFi ? 0 : 1]; }
        const char* route_name(Route route) { return route == Route::WiFi ? "Wi-Fi" : "Bluetooth"; }

        void load_prefs() {
            g_close_to_tray = prefs().value("close_to_tray", false);
            g_icon_style = prefs().value("tray_icon", std::string("symbolic"));
        }

        // Symbolic icons are drawn in the panel's foreground color, so the item matches the rest of
        // the tray. Falls back to the color set when the symbolic one is not installed.
        void resolve_icon_style() {
            if (g_icon_style == "color")
                return;
            if (gtk_icon_theme_has_icon(gtk_icon_theme_get_default(), "tether-symbolic"))
                g_icon_suffix = "-symbolic";
        }

        void present_window() {
            if (GtkWidget* window = main_window())
                gtk_window_present(GTK_WINDOW(window));
        }

        // The only hide affordance on a desktop with no window buttons. Keyed on
        // visibility, not focus: clicking the icon takes focus off the window.
        void toggle_window() {
            GtkWidget* window = main_window();
            if (!window)
                return;
            if (gtk_widget_get_visible(window))
                gtk_widget_hide(window);
            else
                gtk_window_present(GTK_WINDOW(window));
        }

        void emit(const char* signal) {
            if (!g_exported)
                return;
            g_dbus_connection_emit_signal(g_bus, nullptr, SNI_PATH, SNI_INTERFACE, signal, nullptr, nullptr);
        }

        GVariant* build_tooltip() {
            GVariantBuilder pixmaps;
            g_variant_builder_init(&pixmaps, G_VARIANT_TYPE("a(iiay)"));
            return g_variant_new("(sa(iiay)ss)", "", &pixmaps, "Tether", g_tooltip.c_str());
        }

        const char* status_name() { return g_pending_pairs > 0 ? "NeedsAttention" : "Active"; }

        bool window_visible() {
            GtkWidget* window = main_window();
            return window && gtk_widget_get_visible(window);
        }

        // --- dbusmenu ---

        void add_menu_item_properties(GVariantBuilder* props, int id) {
            switch (id) {
            case MENU_ROOT:
                g_variant_builder_add(props, "{sv}", "children-display", g_variant_new_string("submenu"));
                break;
            case MENU_TOGGLE_WINDOW:
                g_variant_builder_add(
                    props, "{sv}", "label", g_variant_new_string(window_visible() ? _("Hide Tether") : _("Show Tether")));
                break;
            case MENU_CLIPBOARD:
                g_variant_builder_add(props, "{sv}", "label", g_variant_new_string(_("Sync clipboard with iPhone")));
                g_variant_builder_add(props, "{sv}", "toggle-type", g_variant_new_string("checkmark"));
                g_variant_builder_add(props, "{sv}", "toggle-state", g_variant_new_int32(g_clipboard_sync ? 1 : 0));
                g_variant_builder_add(props, "{sv}", "enabled", g_variant_new_boolean(daemon_connected()));
                break;
            case MENU_SEPARATOR:
                g_variant_builder_add(props, "{sv}", "type", g_variant_new_string("separator"));
                break;
            case MENU_SETTINGS:
                g_variant_builder_add(props, "{sv}", "label", g_variant_new_string(_("Settings…")));
                break;
            case MENU_QUIT:
                g_variant_builder_add(props, "{sv}", "label", g_variant_new_string(_("Quit Tether")));
                break;
            default:
                break;
            }
        }

        GVariant* build_menu_item(int id, bool with_children) {
            GVariantBuilder props;
            g_variant_builder_init(&props, G_VARIANT_TYPE("a{sv}"));
            add_menu_item_properties(&props, id);

            GVariantBuilder children;
            g_variant_builder_init(&children, G_VARIANT_TYPE("av"));
            if (id == MENU_ROOT && with_children) {
                for (int child : {MENU_TOGGLE_WINDOW, MENU_CLIPBOARD, MENU_SEPARATOR, MENU_SETTINGS, MENU_QUIT})
                    g_variant_builder_add(&children, "v", build_menu_item(child, false));
            }
            return g_variant_new("(ia{sv}av)", id, &props, &children);
        }

        void emit_menu_layout_updated() {
            if (!g_menu_exported)
                return;
            g_menu_revision += 1;
            g_dbus_connection_emit_signal(g_bus,
                                          nullptr,
                                          MENU_PATH,
                                          MENU_INTERFACE,
                                          "LayoutUpdated",
                                          g_variant_new("(ui)", g_menu_revision, MENU_ROOT),
                                          nullptr);
        }

        void emit_menu_item_updated(int id) {
            if (!g_menu_exported)
                return;
            GVariantBuilder updated;
            g_variant_builder_init(&updated, G_VARIANT_TYPE("a(ia{sv})"));
            GVariantBuilder props;
            g_variant_builder_init(&props, G_VARIANT_TYPE("a{sv}"));
            add_menu_item_properties(&props, id);
            g_variant_builder_add(&updated, "(ia{sv})", id, &props);
            GVariantBuilder removed;
            g_variant_builder_init(&removed, G_VARIANT_TYPE("a(ias)"));
            g_dbus_connection_emit_signal(g_bus,
                                          nullptr,
                                          MENU_PATH,
                                          MENU_INTERFACE,
                                          "ItemsPropertiesUpdated",
                                          g_variant_new("(a(ia{sv})a(ias))", &updated, &removed),
                                          nullptr);
        }

        void run_menu_item(int id);

        GVariant* menu_get_property(
            GDBusConnection*, const gchar*, const gchar*, const gchar*, const gchar* property, GError**, gpointer) {
            const std::string name = property;
            if (name == "Version")
                return g_variant_new_uint32(3);
            if (name == "TextDirection")
                return g_variant_new_string(gtk_widget_get_default_direction() == GTK_TEXT_DIR_RTL ? "rtl" : "ltr");
            if (name == "Status")
                return g_variant_new_string("normal");
            if (name == "IconThemePath")
                return g_variant_new("as", nullptr);
            return nullptr;
        }

        void menu_on_method(GDBusConnection*,
                            const gchar*,
                            const gchar*,
                            const gchar*,
                            const gchar* method,
                            GVariant* parameters,
                            GDBusMethodInvocation* invocation,
                            gpointer) {
            const std::string name = method;
            if (name == "GetLayout") {
                gint32 parent = 0;
                gint32 depth = -1;
                g_variant_get_child(parameters, 0, "i", &parent);
                g_variant_get_child(parameters, 1, "i", &depth);
                // The label of the first item depends on the window, so it is
                // refreshed every time the host asks. Depth 0 means the parent alone.
                g_dbus_method_invocation_return_value(
                    invocation,
                    g_variant_new("(u@(ia{sv}av))",
                                  g_menu_revision,
                                  build_menu_item(parent, parent == MENU_ROOT && depth != 0)));
                return;
            }
            if (name == "GetGroupProperties") {
                GVariantIter* ids = nullptr;
                g_variant_get_child(parameters, 0, "ai", &ids);
                GVariantBuilder result;
                g_variant_builder_init(&result, G_VARIANT_TYPE("a(ia{sv})"));
                std::vector<int> wanted;
                gint32 id = 0;
                while (ids && g_variant_iter_next(ids, "i", &id))
                    wanted.push_back(id);
                if (ids)
                    g_variant_iter_free(ids);
                // An empty list asks for every item, per spec.
                if (wanted.empty())
                    wanted = {MENU_ROOT, MENU_TOGGLE_WINDOW, MENU_CLIPBOARD, MENU_SEPARATOR, MENU_SETTINGS, MENU_QUIT};
                for (int item : wanted) {
                    GVariantBuilder props;
                    g_variant_builder_init(&props, G_VARIANT_TYPE("a{sv}"));
                    add_menu_item_properties(&props, item);
                    g_variant_builder_add(&result, "(ia{sv})", item, &props);
                }
                g_dbus_method_invocation_return_value(invocation, g_variant_new("(a(ia{sv}))", &result));
                return;
            }
            if (name == "GetProperty") {
                gint32 id = 0;
                const gchar* prop = nullptr;
                g_variant_get(parameters, "(i&s)", &id, &prop);
                GVariantBuilder props;
                g_variant_builder_init(&props, G_VARIANT_TYPE("a{sv}"));
                add_menu_item_properties(&props, id);
                GVariant* dict = g_variant_builder_end(&props);
                GVariant* value = prop ? g_variant_lookup_value(dict, prop, nullptr) : nullptr;
                if (value)
                    g_dbus_method_invocation_return_value(invocation, g_variant_new("(v)", value));
                else
                    g_dbus_method_invocation_return_dbus_error(
                        invocation, "com.canonical.dbusmenu.Error.UnknownProperty", "no such property");
                g_variant_unref(dict);
                return;
            }
            if (name == "Event") {
                gint32 id = 0;
                const gchar* event = nullptr;
                g_variant_get_child(parameters, 0, "i", &id);
                g_variant_get_child(parameters, 1, "&s", &event);
                g_dbus_method_invocation_return_value(invocation, nullptr);
                if (event && std::string(event) == "clicked")
                    run_menu_item(id);
                return;
            }
            if (name == "EventGroup") {
                GVariantIter* events = nullptr;
                g_variant_get_child(parameters, 0, "a(isvu)", &events);
                gint32 id = 0;
                const gchar* event = nullptr;
                GVariant* data = nullptr;
                guint32 stamp = 0;
                std::vector<int> clicked;
                while (events && g_variant_iter_next(events, "(i&svu)", &id, &event, &data, &stamp)) {
                    if (event && std::string(event) == "clicked")
                        clicked.push_back(id);
                    if (data)
                        g_variant_unref(data);
                }
                if (events)
                    g_variant_iter_free(events);
                g_dbus_method_invocation_return_value(invocation, g_variant_new("(ai)", nullptr));
                for (int item : clicked)
                    run_menu_item(item);
                return;
            }
            if (name == "AboutToShow") {
                g_dbus_method_invocation_return_value(invocation, g_variant_new("(b)", FALSE));
                return;
            }
            if (name == "AboutToShowGroup") {
                g_dbus_method_invocation_return_value(invocation, g_variant_new("(aiai)", nullptr, nullptr));
                return;
            }
            g_dbus_method_invocation_return_dbus_error(
                invocation, "org.freedesktop.DBus.Error.UnknownMethod", "no such method");
        }

        constexpr GDBusInterfaceVTable MENU_VTABLE = {menu_on_method, menu_get_property, nullptr, {nullptr}};

        GVariant* get_property(
            GDBusConnection*, const gchar*, const gchar*, const gchar*, const gchar* property, GError**, gpointer) {
            const std::string name = property;
            if (name == "Category")
                return g_variant_new_string("ApplicationStatus");
            if (name == "Id")
                return g_variant_new_string("tether");
            if (name == "Title")
                return g_variant_new_string("Tether");
            if (name == "Status")
                return g_variant_new_string(status_name());
            if (name == "IconName")
                return g_variant_new_string(g_icon.c_str());
            if (name == "ItemIsMenu")
                return g_variant_new_boolean(FALSE);
            if (name == "Menu")
                return g_variant_new_object_path(g_menu_exported ? MENU_PATH : "/NO_DBUSMENU");
            if (name == "ToolTip")
                return build_tooltip();
            return nullptr;
        }

        void on_method(GDBusConnection*,
                       const gchar*,
                       const gchar*,
                       const gchar*,
                       const gchar* method,
                       GVariant*,
                       GDBusMethodInvocation* invocation,
                       gpointer) {
            const std::string name = method;
            if (name == "Activate" || name == "SecondaryActivate")
                toggle_window();
            else if (name == "ContextMenu" && !g_menu_exported)
                // A host that calls this instead of drawing the menu gets the window.
                present_window();
            g_dbus_method_invocation_return_value(invocation, nullptr);
        }

        constexpr GDBusInterfaceVTable VTABLE = {on_method, get_property, nullptr, {nullptr}};

        void run_menu_item(int id) {
            switch (id) {
            case MENU_TOGGLE_WINDOW:
                toggle_window();
                emit_menu_item_updated(MENU_TOGGLE_WINDOW);
                break;
            case MENU_CLIPBOARD:
                if (!daemon_send({{"command", "set_clipboard_sync"}, {"enabled", !g_clipboard_sync}}))
                    emit_menu_item_updated(MENU_CLIPBOARD);
                break;
            case MENU_SETTINGS:
                present_window();
                settings_window_show();
                break;
            case MENU_QUIT:
                if (GApplication* app = g_application_get_default())
                    g_application_quit(app);
                break;
            default:
                break;
            }
        }

        void cancel_no_host_timer() {
            if (g_no_host_id != 0) {
                g_source_remove(g_no_host_id);
                g_no_host_id = 0;
            }
        }

        void register_with_watcher() {
            if (!g_exported)
                return;
            g_dbus_connection_call(g_bus,
                                   WATCHER_NAME,
                                   WATCHER_PATH,
                                   WATCHER_NAME,
                                   "RegisterStatusNotifierItem",
                                   g_variant_new("(s)", g_bus_name.c_str()),
                                   nullptr,
                                   G_DBUS_CALL_FLAGS_NONE,
                                   -1,
                                   nullptr,
                                   nullptr,
                                   nullptr);
        }

        void on_name_acquired(GDBusConnection* connection, const gchar*, gpointer) {
            if (g_exported)
                return;
            GError* error = nullptr;
            if (g_dbus_connection_register_object(
                    connection, SNI_PATH, g_node->interfaces[0], &VTABLE, nullptr, nullptr, &error) == 0) {
                debug::log(WARN, "tray: could not export the item ({})", error ? error->message : "unknown");
                g_clear_error(&error);
                return;
            }
            g_exported = true;
            if (g_menu_node &&
                g_dbus_connection_register_object(
                    connection, MENU_PATH, g_menu_node->interfaces[0], &MENU_VTABLE, nullptr, nullptr, &error) != 0) {
                g_menu_exported = true;
            } else {
                debug::log(WARN, "tray: could not export the menu ({})", error ? error->message : "unknown");
                g_clear_error(&error);
            }
            register_with_watcher();
        }

    } // namespace

    void tray_init() {
        if (g_own_id != 0)
            return;

        load_prefs();
        resolve_icon_style();
        g_icon += g_icon_suffix;

        GError* error = nullptr;
        g_bus = g_bus_get_sync(G_BUS_TYPE_SESSION, nullptr, &error);
        if (!g_bus) {
            debug::log(WARN, "tray: no session bus ({})", error ? error->message : "unknown");
            g_clear_error(&error);
            return;
        }

        g_node = g_dbus_node_info_new_for_xml(INTROSPECTION, &error);
        if (!g_node) {
            debug::log(ERR, "tray: bad introspection data ({})", error ? error->message : "unknown");
            g_clear_error(&error);
            return;
        }
        g_menu_node = g_dbus_node_info_new_for_xml(MENU_INTROSPECTION, &error);
        if (!g_menu_node) {
            debug::log(ERR, "tray: bad menu introspection data ({})", error ? error->message : "unknown");
            g_clear_error(&error);
        }

        // Keyed on the process, per spec, so two instances cannot collide.
        g_bus_name = "org.kde.StatusNotifierItem-" + std::to_string(getpid()) + "-1";
        g_own_id = g_bus_own_name_on_connection(
            g_bus, g_bus_name.c_str(), G_BUS_NAME_OWNER_FLAGS_NONE, on_name_acquired, nullptr, nullptr, nullptr);

        // Re-registering on reappearance is what survives a panel restart.
        g_watch_id = g_bus_watch_name_on_connection(
            g_bus,
            WATCHER_NAME,
            G_BUS_NAME_WATCHER_FLAGS_NONE,
            +[](GDBusConnection*, const gchar*, const gchar*, gpointer) {
                g_host_seen = true;
                cancel_no_host_timer();
                register_with_watcher();
            },
            +[](GDBusConnection*, const gchar*, gpointer) {
                // Called at once when no watcher exists at all. A panel that is
                // merely late gets a grace period before the window is forced up.
                if (g_host_seen || g_no_host_id != 0)
                    return;
                g_no_host_id = g_timeout_add_seconds(
                    NO_HOST_GRACE_SECONDS,
                    [](gpointer) -> gboolean {
                        g_no_host_id = 0;
                        if (!g_host_seen && g_on_no_host)
                            g_on_no_host();
                        return G_SOURCE_REMOVE;
                    },
                    nullptr);
            },
            nullptr,
            nullptr);

        tray_refresh();
    }

    void tray_on_no_host(std::function<void()> callback) { g_on_no_host = std::move(callback); }

    void tray_set_pending_pairs(int count) {
        if (count == g_pending_pairs)
            return;
        g_pending_pairs = count;
        if (g_exported)
            g_dbus_connection_emit_signal(g_bus,
                                          nullptr,
                                          SNI_PATH,
                                          SNI_INTERFACE,
                                          "NewStatus",
                                          g_variant_new("(s)", status_name()),
                                          nullptr);
        tray_refresh();
    }

    void tray_set_clipboard_sync(bool enabled) {
        if (enabled == g_clipboard_sync)
            return;
        g_clipboard_sync = enabled;
        tray_refresh();
    }

    void tray_set_route(Route route, bool ok, const std::string& detail) {
        state(route) = {ok, detail};
        tray_refresh();
    }

    void tray_refresh() {
        std::string tooltip;
        for (Route route : {Route::WiFi, Route::Bluetooth}) {
            const RouteState& r = state(route);
            const std::string status = r.ok ? _("connected") : r.detail.empty() ? _("not connected") : r.detail;
            // TRANSLATORS: {} is a transport name, "Wi-Fi" or "Bluetooth", or a device name.
            tooltip += tether::tr_format(_("{}: {}"), route_name(route), status) + "\n";
        }
        if (!g_airpods_battery.empty())
            tooltip += tether::tr_format(_("{}: {}"), g_airpods_name, g_airpods_battery) + "\n";
        if (!daemon_connected())
            tooltip += std::string(_("Daemon: not running")) + "\n";
        if (g_unread > 0)
            tooltip += tether::tr_format(P_("{} unread message", "{} unread messages", g_unread), g_unread) + "\n";
        if (g_pending_pairs > 0)
            tooltip += tether::tr_format(P_("{} pairing request waiting", "{} pairing requests waiting", g_pending_pairs),
                                         g_pending_pairs) +
                       "\n";
        if (!g_clipboard_sync)
            tooltip += std::string(_("Clipboard sync paused")) + "\n";
        if (!tooltip.empty())
            tooltip.pop_back();

        const bool any_up = g_state[0].ok || g_state[1].ok;
        // A waiting pairing request borrows the unread badge: it is the one
        // thing in the tray that wants a look.
        const bool badged = g_unread > 0 || g_pending_pairs > 0;
        const std::string icon =
            std::string(!any_up && g_pending_pairs == 0 ? "tether-offline" : (badged ? "tether-unread" : "tether")) +
            g_icon_suffix;
        // The window label changes with visibility, and the clipboard item
        // follows the daemon being reachable. Only on a change: refresh runs often.
        static bool last_visible = false;
        static bool last_daemon = false;
        static bool last_clipboard = true;
        if (window_visible() != last_visible) {
            last_visible = window_visible();
            emit_menu_item_updated(MENU_TOGGLE_WINDOW);
        }
        if (daemon_connected() != last_daemon || g_clipboard_sync != last_clipboard) {
            last_daemon = daemon_connected();
            last_clipboard = g_clipboard_sync;
            emit_menu_item_updated(MENU_CLIPBOARD);
        }

        if (icon != g_icon) {
            g_icon = icon;
            emit("NewIcon");
        }
        if (tooltip != g_tooltip) {
            g_tooltip = tooltip;
            emit("NewToolTip");
        }
    }

    void tray_set_airpods(const std::string& name, const std::string& battery) {
        if (name == g_airpods_name && battery == g_airpods_battery)
            return;
        g_airpods_name = name;
        g_airpods_battery = battery;
        tray_refresh();
    }

    void tray_set_unread(int count) {
        if (count == g_unread)
            return;
        g_unread = count;
        tray_refresh();
    }

    bool tray_close_to_tray() { return g_close_to_tray; }

    void tray_set_close_to_tray(bool enabled) {
        if (g_close_to_tray == enabled)
            return;
        g_close_to_tray = enabled;
        prefs()["close_to_tray"] = enabled;
        prefs_save();
    }

} // namespace tether::ui
