#include "overlay_ui.h"

#include <unordered_map>

namespace {
struct OverlayState {
    GtkWidget* window = nullptr;
    GdkRectangle monitor_geometry = {0, 0, 0, 0};
    RunState status = RunState::Idle;
};

static std::unordered_map<AppState*, OverlayState> kOverlayStates;

static constexpr double kBorderThicknessPx = 5.0; 
static constexpr double kBorderAlpha = 1.0;
static constexpr double kRecordingR = 1.0;
static constexpr double kRecordingG = 0.0;
static constexpr double kRecordingB = 0.0;
static constexpr double kTranscribingR = 0.95;
static constexpr double kTranscribingG = 0.58;
static constexpr double kTranscribingB = 0.16;

static bool overlay_ui_pick_initial_monitor_geometry(GdkRectangle* out_geometry) {
    if (!out_geometry) return false;
    GdkDisplay* display = gdk_display_get_default();
    if (!display) return false;

    GdkMonitor* monitor = gdk_display_get_primary_monitor(display);
    GdkSeat* seat = gdk_display_get_default_seat(display);
    if (seat) {
        GdkDevice* pointer = gdk_seat_get_pointer(seat);
        if (pointer) {
            gint x = 0;
            gint y = 0;
            gdk_device_get_position(pointer, nullptr, &x, &y);
            if (GdkMonitor* at_point = gdk_display_get_monitor_at_point(display, x, y)) {
                monitor = at_point;
            }
        }
    }
    if (!monitor && gdk_display_get_n_monitors(display) > 0) {
        monitor = gdk_display_get_monitor(display, 0);
    }
    if (!monitor) return false;

    gdk_monitor_get_workarea(monitor, out_geometry);
    return out_geometry->width > 0 && out_geometry->height > 0;
}

static gboolean overlay_ui_on_draw(GtkWidget* widget, cairo_t* cr, gpointer user_data) {
    auto* app = static_cast<AppState*>(user_data);
    auto it = kOverlayStates.find(app);
    if (it == kOverlayStates.end()) return FALSE;

    GtkAllocation allocation;
    gtk_widget_get_allocation(widget, &allocation);

    cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
    cairo_set_source_rgba(cr, 0.0, 0.0, 0.0, 0.0);
    cairo_paint(cr);
    cairo_set_operator(cr, CAIRO_OPERATOR_OVER);

    if (it->second.status == RunState::Idle) return FALSE;

    double r = kRecordingR;
    double g = kRecordingG;
    double b = kRecordingB;
    if (it->second.status == RunState::Transcribing) {
        r = kTranscribingR;
        g = kTranscribingG;
        b = kTranscribingB;
    }

    const double half = kBorderThicknessPx / 2.0;
    const double width = static_cast<double>(allocation.width);
    const double height = static_cast<double>(allocation.height);
    if (width <= kBorderThicknessPx || height <= kBorderThicknessPx) return FALSE;

    cairo_set_source_rgba(cr, r, g, b, kBorderAlpha);
    cairo_set_line_width(cr, kBorderThicknessPx);
    cairo_rectangle(cr, half, half, width - kBorderThicknessPx, height - kBorderThicknessPx);
    cairo_stroke(cr);
    return FALSE;
}

static void overlay_ui_make_click_through(GtkWidget* window) {
    if (!window) return;
    gtk_widget_realize(window);
    if (GdkWindow* gdk_window = gtk_widget_get_window(window)) {
        gdk_window_set_pass_through(gdk_window, TRUE);
    }
    cairo_region_t* empty = cairo_region_create();
    gtk_widget_input_shape_combine_region(window, empty);
    cairo_region_destroy(empty);
}
}  // namespace

void overlay_ui_init(AppState* app) {
    if (!app) return;
    if (kOverlayStates.find(app) != kOverlayStates.end()) return;

    OverlayState state;
    if (!overlay_ui_pick_initial_monitor_geometry(&state.monitor_geometry)) {
        g_printerr("overlay: monitor geometry unavailable\n");
        return;
    }

    GtkWidget* window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_decorated(GTK_WINDOW(window), FALSE);
    gtk_window_set_resizable(GTK_WINDOW(window), FALSE);
    gtk_window_set_skip_taskbar_hint(GTK_WINDOW(window), TRUE);
    gtk_window_set_skip_pager_hint(GTK_WINDOW(window), TRUE);
    gtk_window_set_keep_above(GTK_WINDOW(window), TRUE);
    gtk_window_set_accept_focus(GTK_WINDOW(window), FALSE);
    gtk_window_set_focus_on_map(GTK_WINDOW(window), FALSE);
    gtk_window_set_type_hint(GTK_WINDOW(window), GDK_WINDOW_TYPE_HINT_NOTIFICATION);
    gtk_widget_set_app_paintable(window, TRUE);

    GdkScreen* screen = gtk_widget_get_screen(window);
    if (screen && gdk_screen_is_composited(screen)) {
        if (GdkVisual* visual = gdk_screen_get_rgba_visual(screen)) {
            gtk_widget_set_visual(window, visual);
        }
    }

    gtk_window_set_default_size(GTK_WINDOW(window), state.monitor_geometry.width, state.monitor_geometry.height);
    gtk_window_move(GTK_WINDOW(window), state.monitor_geometry.x, state.monitor_geometry.y);
    gtk_window_resize(GTK_WINDOW(window), state.monitor_geometry.width, state.monitor_geometry.height);
    g_signal_connect(window, "draw", G_CALLBACK(overlay_ui_on_draw), app);

    state.window = window;
    state.status = RunState::Idle;
    kOverlayStates.insert({app, state});
    overlay_ui_make_click_through(window);
    gtk_widget_hide(window);
}

void overlay_ui_set_status(AppState* app, RunState status) {
    auto it = kOverlayStates.find(app);
    if (!app || it == kOverlayStates.end()) return;

    it->second.status = status;
    if (status == RunState::Idle) {
        gtk_widget_hide(it->second.window);
        return;
    }

    gtk_widget_show(it->second.window);
    gtk_widget_queue_draw(it->second.window);
}

void overlay_ui_shutdown(AppState* app) {
    auto it = kOverlayStates.find(app);
    if (!app || it == kOverlayStates.end()) return;

    if (it->second.window) {
        gtk_widget_destroy(it->second.window);
        it->second.window = nullptr;
    }
    kOverlayStates.erase(it);
}
