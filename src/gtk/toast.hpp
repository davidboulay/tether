#pragma once

#include <gtk/gtk.h>
#include <string>

namespace tether::ui {

    enum class ToastLevel { Info, Success, Error };

    // Wraps the window content so notices can float over it. Call once.
    GtkWidget* create_toast_overlay(GtkWidget* content);

    // A short notice at the top of the window. It goes away on its own, errors
    // later than the rest, and nothing else overwrites it in the meantime. Every
    // notice is also kept in a history the header button opens, so a reason
    // that was missed can still be read.
    void show_toast(const std::string& text, ToastLevel level = ToastLevel::Info);

    // The header-bar button that opens the history. Its icon changes while an
    // error sits in the history that nobody has opened yet.
    GtkWidget* create_history_button();

} // namespace tether::ui
