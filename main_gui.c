/*
 * main_gui.c  –  GTK4 GUI for Smart Traffic Flow Simulator
 * Targets GTK 4.24+ (uses GtkAlertDialog / async patterns,
 *                     no deprecated gtk_dialog_run).
 */

#include <gtk/gtk.h>
#include <math.h>
#include "traffic.h"

// ───────────────────────── App State ─────────────────────────

static Queue     g_north, g_south, g_east, g_west;
static Statistics g_stats;

// ───────────────────────── Widget Handles ────────────────────

static GtkWidget     *g_window;
static GtkWidget     *g_time_label;

static GtkWidget     *g_lane_count[4];
static GtkWidget     *g_lane_priority[4];
static GtkWidget     *g_lane_emergency[4];
static GtkWidget     *g_lane_status[4];

static GtkWidget     *g_heap_list;

static GtkWidget     *g_canvas;

static GtkWidget     *g_log_view;
static GtkTextBuffer *g_log_buf;

static GtkWidget     *g_stat_total;
static GtkWidget     *g_stat_passed;
static GtkWidget     *g_stat_emergency;
static GtkWidget     *g_stat_avg_wait;
static GtkWidget     *g_stat_max_wait;
static GtkWidget     *g_stat_n, *g_stat_s, *g_stat_e, *g_stat_w;

// ───────────────────────── Helpers ────────────────────────────

static Queue *dir_queue(int d)
{
    return getQueue(&g_north, &g_south, &g_east, &g_west, d);
}

static void log_append(const char *text)
{
    GtkTextIter end;
    gtk_text_buffer_get_end_iter(g_log_buf, &end);
    gtk_text_buffer_insert(g_log_buf, &end, text, -1);
    gtk_text_buffer_insert(g_log_buf, &end, "\n", -1);
    GtkTextMark *mark = gtk_text_buffer_get_insert(g_log_buf);
    gtk_text_view_scroll_mark_onscreen(GTK_TEXT_VIEW(g_log_view), mark);
}

static const char *dir_css_class(int d)
{
    const char *c[] = {"dir-north","dir-south","dir-east","dir-west"};
    return c[d];
}

static const char *density_label(int size)
{
    if (size == 0)  return "EMPTY";
    if (size <= 4)  return "LOW";
    if (size <= 10) return "MEDIUM";
    return "HIGH";
}

static const char *density_class(int size)
{
    if (size == 0)  return "density-empty";
    if (size <= 4)  return "density-low";
    if (size <= 10) return "density-medium";
    return "density-high";
}

// ───────────────────────── Refresh UI ─────────────────────────

static void refresh_ui(void);

static void refresh_lane_cards(void)
{
    for (int d = 0; d < 4; d++) {
        Queue *q  = dir_queue(d);
        int    pr = calculatePriority(q, d);
        int    em = countEmergency(q);
        char   buf[64];

        snprintf(buf, sizeof buf, "%d", q->size);
        gtk_label_set_text(GTK_LABEL(g_lane_count[d]), buf);

        snprintf(buf, sizeof buf, "Priority: %d", pr);
        gtk_label_set_text(GTK_LABEL(g_lane_priority[d]), buf);

        if (em > 0) snprintf(buf, sizeof buf, "🚨 %d Emergency", em);
        else        snprintf(buf, sizeof buf, "Normal");
        gtk_label_set_text(GTK_LABEL(g_lane_emergency[d]), buf);

        gtk_label_set_text(GTK_LABEL(g_lane_status[d]), density_label(q->size));
        gtk_widget_remove_css_class(g_lane_status[d], "density-empty");
        gtk_widget_remove_css_class(g_lane_status[d], "density-low");
        gtk_widget_remove_css_class(g_lane_status[d], "density-medium");
        gtk_widget_remove_css_class(g_lane_status[d], "density-high");
        gtk_widget_add_css_class(g_lane_status[d], density_class(q->size));
    }
}

static void refresh_heap_view(void)
{
    GtkWidget *child;
    while ((child = gtk_widget_get_first_child(g_heap_list)) != NULL)
        gtk_list_box_remove(GTK_LIST_BOX(g_heap_list), child);

    Heap h;
    buildHeap(&h, &g_north, &g_south, &g_east, &g_west);

    if (h.size == 0) {
        GtkWidget *row = gtk_list_box_row_new();
        GtkWidget *lbl = gtk_label_new("No vehicles");
        gtk_widget_add_css_class(lbl, "heap-empty");
        gtk_list_box_row_set_child(GTK_LIST_BOX_ROW(row), lbl);
        gtk_list_box_append(GTK_LIST_BOX(g_heap_list), row);
        return;
    }

    for (int i = 0; i < h.size; i++) {
        char buf[80];
        snprintf(buf, sizeof buf, "%s%s  —  Priority: %d",
                 i == 0 ? "🏆 " : "   ",
                 directionName(h.arr[i].direction),
                 h.arr[i].priority);

        GtkWidget *row = gtk_list_box_row_new();
        GtkWidget *lbl = gtk_label_new(buf);
        gtk_label_set_xalign(GTK_LABEL(lbl), 0.f);
        gtk_widget_set_margin_start(lbl, 12);
        gtk_widget_set_margin_end(lbl, 12);
        gtk_widget_set_margin_top(lbl, 6);
        gtk_widget_set_margin_bottom(lbl, 6);
        if (i == 0) gtk_widget_add_css_class(lbl, "heap-top");
        gtk_list_box_row_set_child(GTK_LIST_BOX_ROW(row), lbl);
        gtk_list_box_append(GTK_LIST_BOX(g_heap_list), row);
    }
}

static void refresh_stats(void)
{
    char  buf[64];
    float avg = g_stats.vehiclesPassed
        ? (float)g_stats.totalWaitingTime / g_stats.vehiclesPassed : 0.f;

    snprintf(buf,sizeof buf,"%d",  g_stats.totalVehicles);     gtk_label_set_text(GTK_LABEL(g_stat_total),     buf);
    snprintf(buf,sizeof buf,"%d",  g_stats.vehiclesPassed);    gtk_label_set_text(GTK_LABEL(g_stat_passed),    buf);
    snprintf(buf,sizeof buf,"%d",  g_stats.emergencyVehicles); gtk_label_set_text(GTK_LABEL(g_stat_emergency), buf);
    snprintf(buf,sizeof buf,"%.2f s", avg);                    gtk_label_set_text(GTK_LABEL(g_stat_avg_wait),  buf);
    snprintf(buf,sizeof buf,"%d s",g_stats.maxWaitingTime);    gtk_label_set_text(GTK_LABEL(g_stat_max_wait),  buf);
    snprintf(buf,sizeof buf,"%d",  g_stats.passedNorth);       gtk_label_set_text(GTK_LABEL(g_stat_n),         buf);
    snprintf(buf,sizeof buf,"%d",  g_stats.passedSouth);       gtk_label_set_text(GTK_LABEL(g_stat_s),         buf);
    snprintf(buf,sizeof buf,"%d",  g_stats.passedEast);        gtk_label_set_text(GTK_LABEL(g_stat_e),         buf);
    snprintf(buf,sizeof buf,"%d",  g_stats.passedWest);        gtk_label_set_text(GTK_LABEL(g_stat_w),         buf);
}

static void refresh_time_label(void)
{
    char buf[32];
    snprintf(buf, sizeof buf, "⏱  Time: %d s", currentTime);
    gtk_label_set_text(GTK_LABEL(g_time_label), buf);
}

static void refresh_ui(void)
{
    refresh_lane_cards();
    refresh_heap_view();
    refresh_stats();
    refresh_time_label();
    gtk_widget_queue_draw(g_canvas);
}

// ───────────────────────── Canvas Draw ───────────────────────

static void on_canvas_draw(GtkDrawingArea *area, cairo_t *cr,
                             int width, int height, gpointer data)
{
    (void)area; (void)data;

    double cx     = width  / 2.0;
    double cy     = height / 2.0;
    double road_w = 60.0;
    double light_r = 10.0;

    // Background
    cairo_set_source_rgb(cr, 0.10, 0.11, 0.14);
    cairo_paint(cr);

    // Roads
    cairo_set_source_rgb(cr, 0.22, 0.24, 0.28);
    cairo_rectangle(cr, cx - road_w/2, 0, road_w, height);
    cairo_fill(cr);
    cairo_rectangle(cr, 0, cy - road_w/2, width, road_w);
    cairo_fill(cr);

    // Dashed centre lines
    cairo_set_source_rgba(cr, 0.9, 0.8, 0.0, 0.5);
    cairo_set_line_width(cr, 2);
    double dashes[] = {12, 10};
    cairo_set_dash(cr, dashes, 2, 0);
    cairo_move_to(cr, cx, 0);               cairo_line_to(cr, cx, cy - road_w/2);
    cairo_move_to(cr, cx, cy + road_w/2);   cairo_line_to(cr, cx, height);
    cairo_move_to(cr, 0, cy);               cairo_line_to(cr, cx - road_w/2, cy);
    cairo_move_to(cr, cx + road_w/2, cy);   cairo_line_to(cr, width, cy);
    cairo_stroke(cr);
    cairo_set_dash(cr, NULL, 0, 0);

    // Intersection box
    cairo_set_source_rgb(cr, 0.27, 0.29, 0.33);
    cairo_rectangle(cr, cx - road_w/2, cy - road_w/2, road_w, road_w);
    cairo_fill(cr);

    // Determine green direction
    Heap h;
    buildHeap(&h, &g_north, &g_south, &g_east, &g_west);
    int green_dir = -1;
    int em_dir    = findEmergency(&g_north, &g_south, &g_east, &g_west);
    if      (em_dir != -1)  green_dir = em_dir;
    else if (h.size > 0)    green_dir = h.arr[0].direction;

    // Traffic lights
    struct { double x, y; int dir; } lights[4] = {
        { cx + road_w/2 + 14, cy - road_w/2 - 14, NORTH },
        { cx - road_w/2 - 14, cy + road_w/2 + 14, SOUTH },
        { cx + road_w/2 + 14, cy + road_w/2 + 14, EAST  },
        { cx - road_w/2 - 14, cy - road_w/2 - 14, WEST  },
    };
    for (int i = 0; i < 4; i++) {
        cairo_set_source_rgb(cr, 0.15, 0.16, 0.18);
        cairo_arc(cr, lights[i].x, lights[i].y, light_r + 3, 0, 2*G_PI);
        cairo_fill(cr);
        if (lights[i].dir == green_dir)
            cairo_set_source_rgb(cr, 0.05, 0.95, 0.35);
        else
            cairo_set_source_rgb(cr, 0.90, 0.15, 0.15);
        cairo_arc(cr, lights[i].x, lights[i].y, light_r, 0, 2*G_PI);
        cairo_fill(cr);
    }

    // Vehicle dots
    const double dir_x[4]  = { cx - 12, cx + 12, cx + road_w/2 + 30, cx - road_w/2 - 30 };
    const double dir_y[4]  = { cy - road_w/2 - 30, cy + road_w/2 + 30, cy - 12, cy + 12 };
    const double step_x[4] = { 0,  0, -18, 18 };
    const double step_y[4] = { 18,-18,  0,  0 };

    for (int d = 0; d < 4; d++) {
        Queue *q  = dir_queue(d);
        node  *it = q->front;
        int    n  = 0;
        while (it && n < 5) {
            double bx = dir_x[d] + n * step_x[d];
            double by = dir_y[d] + n * step_y[d];

            cairo_set_source_rgba(cr, 0, 0, 0, 0.4);
            cairo_arc(cr, bx + 2, by + 2, 7, 0, 2*G_PI);
            cairo_fill(cr);

            if (it->vehicle.emergency)
                cairo_set_source_rgb(cr, 1.0, 0.3, 0.1);
            else
                cairo_set_source_rgb(cr, 0.2, 0.6, 1.0);
            cairo_arc(cr, bx, by, 7, 0, 2*G_PI);
            cairo_fill(cr);

            it = it->next;
            n++;
        }
        if (q->size > 5) {
            char more[8];
            snprintf(more, sizeof more, "+%d", q->size - 5);
            double tx = dir_x[d] + 5 * step_x[d];
            double ty = dir_y[d] + 5 * step_y[d];
            cairo_set_source_rgb(cr, 1, 1, 1);
            cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
            cairo_set_font_size(cr, 11);
            cairo_move_to(cr, tx - 8, ty + 4);
            cairo_show_text(cr, more);
        }
    }

    // Direction labels
    cairo_set_source_rgba(cr, 1, 1, 1, 0.8);
    cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
    cairo_set_font_size(cr, 13);
    cairo_move_to(cr, cx - 6, 18);           cairo_show_text(cr, "N");
    cairo_move_to(cr, cx - 6, height - 6);   cairo_show_text(cr, "S");
    cairo_move_to(cr, width - 20, cy + 5);   cairo_show_text(cr, "E");
    cairo_move_to(cr, 6,          cy + 5);   cairo_show_text(cr, "W");
}

// ══════════════════════════════════════════════════════════════
//  Add Vehicle  (uses GtkWindow as form, no deprecated dialog)
// ══════════════════════════════════════════════════════════════

typedef struct {
    GtkWidget *window;
    GtkWidget *dir_combo;
    GtkWidget *type_entry;
    GtkWidget *em_check;
} AddVehicleData;

static void on_add_vehicle_confirm(GtkButton *btn, gpointer user_data)
{
    (void)btn;
    AddVehicleData *d = user_data;

    int   dir = gtk_combo_box_get_active(GTK_COMBO_BOX(d->dir_combo));
    const char *type = gtk_entry_buffer_get_text(
                           gtk_entry_get_buffer(GTK_ENTRY(d->type_entry)));
    int   em  = gtk_check_button_get_active(GTK_CHECK_BUTTON(d->em_check));

    if (dir < 0) dir = 0;
    if (!type || type[0] == '\0') type = "Unknown";

    Vehicle v;
    v.id          = vehicleID++;
    v.arrivalTime = currentTime;
    v.emergency   = em;
    strncpy(v.type, type, sizeof(v.type) - 1);
    v.type[sizeof(v.type)-1] = '\0';

    Queue *q = dir_queue(dir);
    enqueue(q, v);
    g_stats.totalVehicles++;
    if (em) g_stats.emergencyVehicles++;

    char msg[128];
    snprintf(msg, sizeof msg, "✅ Vehicle %d (%s)%s → %s",
             v.id, v.type, em ? " [EMERGENCY]" : "", directionName(dir));
    log_append(msg);

    char logmsg[100];
    snprintf(logmsg, sizeof logmsg, "Vehicle %d entered %s", v.id, directionName(dir));
    logTraffic(logmsg);

    refresh_ui();
    gtk_window_close(GTK_WINDOW(d->window));
    g_free(d);
}

static void on_add_vehicle_cancel(GtkButton *btn, gpointer user_data)
{
    (void)btn;
    AddVehicleData *d = user_data;
    gtk_window_close(GTK_WINDOW(d->window));
    g_free(d);
}

static void on_add_vehicle(GtkButton *btn, gpointer data)
{
    (void)btn; (void)data;

    GtkWidget *win = gtk_window_new();
    gtk_window_set_title(GTK_WINDOW(win), "Add Vehicle");
    gtk_window_set_transient_for(GTK_WINDOW(win), GTK_WINDOW(g_window));
    gtk_window_set_modal(GTK_WINDOW(win), TRUE);
    gtk_window_set_default_size(GTK_WINDOW(win), 360, 240);

    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_widget_set_margin_start(vbox, 20);
    gtk_widget_set_margin_end(vbox, 20);
    gtk_widget_set_margin_top(vbox, 16);
    gtk_widget_set_margin_bottom(vbox, 16);
    gtk_window_set_child(GTK_WINDOW(win), vbox);

    GtkWidget *grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(grid), 10);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 14);
    gtk_box_append(GTK_BOX(vbox), grid);

    // Direction
    GtkWidget *dir_lbl = gtk_label_new("Direction:");
    gtk_label_set_xalign(GTK_LABEL(dir_lbl), 0);
    gtk_grid_attach(GTK_GRID(grid), dir_lbl, 0, 0, 1, 1);

    GtkWidget *dir_combo = gtk_combo_box_text_new();
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(dir_combo), "North");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(dir_combo), "South");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(dir_combo), "East");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(dir_combo), "West");
    gtk_combo_box_set_active(GTK_COMBO_BOX(dir_combo), 0);
    gtk_grid_attach(GTK_GRID(grid), dir_combo, 1, 0, 1, 1);

    // Type
    GtkWidget *type_lbl = gtk_label_new("Vehicle Type:");
    gtk_label_set_xalign(GTK_LABEL(type_lbl), 0);
    gtk_grid_attach(GTK_GRID(grid), type_lbl, 0, 1, 1, 1);

    GtkWidget *type_entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(type_entry), "e.g. Car, Truck, Bus");
    gtk_grid_attach(GTK_GRID(grid), type_entry, 1, 1, 1, 1);

    // Emergency
    GtkWidget *em_check = gtk_check_button_new_with_label("Emergency Vehicle 🚨");
    gtk_grid_attach(GTK_GRID(grid), em_check, 0, 2, 2, 1);

    // Buttons
    GtkWidget *btn_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_set_halign(btn_row, GTK_ALIGN_END);
    gtk_box_append(GTK_BOX(vbox), btn_row);

    GtkWidget *cancel_btn = gtk_button_new_with_label("Cancel");
    gtk_widget_add_css_class(cancel_btn, "small-btn");
    gtk_box_append(GTK_BOX(btn_row), cancel_btn);

    GtkWidget *ok_btn = gtk_button_new_with_label("Add Vehicle");
    gtk_widget_add_css_class(ok_btn, "green-btn");
    gtk_box_append(GTK_BOX(btn_row), ok_btn);

    AddVehicleData *avd = g_malloc(sizeof(AddVehicleData));
    avd->window     = win;
    avd->dir_combo  = dir_combo;
    avd->type_entry = type_entry;
    avd->em_check   = em_check;

    g_signal_connect(ok_btn,     "clicked", G_CALLBACK(on_add_vehicle_confirm), avd);
    g_signal_connect(cancel_btn, "clicked", G_CALLBACK(on_add_vehicle_cancel),  avd);

    gtk_window_present(GTK_WINDOW(win));
}

// ══════════════════════════════════════════════════════════════
//  Run Simulation
// ══════════════════════════════════════════════════════════════

static void on_run_simulation(GtkButton *btn, gpointer data)
{
    (void)btn; (void)data;

    int em_dir = findEmergency(&g_north, &g_south, &g_east, &g_west);
    if (em_dir != -1) {
        char msg[128];
        snprintf(msg, sizeof msg, "🚨 EMERGENCY in %s — Forcing GREEN!", directionName(em_dir));
        log_append(msg);
    }

    Heap h;
    buildHeap(&h, &g_north, &g_south, &g_east, &g_west);

    if (h.size == 0 && em_dir == -1) {
        log_append("⚠️  No vehicles waiting. Time +5 s.");
        currentTime += 5;
        refresh_ui();
        return;
    }

    if (h.size > 0) {
        log_append("📊 Max-Heap order:");
        for (int i = 0; i < h.size; i++) {
            char msg[64];
            snprintf(msg, sizeof msg, "   %s  Priority=%d",
                     directionName(h.arr[i].direction), h.arr[i].priority);
            log_append(msg);
        }
    }

    runSimulation(&g_north, &g_south, &g_east, &g_west, &g_stats);
    log_append("✅ Simulation step complete.");
    refresh_ui();
}

// ══════════════════════════════════════════════════════════════
//  Detect Traffic Jam
// ══════════════════════════════════════════════════════════════

static void on_detect_jam(GtkButton *btn, gpointer data)
{
    (void)btn; (void)data;
    gboolean found = FALSE;
    for (int d = 0; d < 4; d++) {
        Queue *q = dir_queue(d);
        if (q->size >= 15) {
            char msg[64];
            snprintf(msg, sizeof msg, "🚧 TRAFFIC JAM at %s! (%d vehicles)", directionName(d), q->size);
            log_append(msg);
            found = TRUE;
        }
    }
    if (!found) log_append("✅ No traffic jam detected.");
}

// ══════════════════════════════════════════════════════════════
//  Save Report
// ══════════════════════════════════════════════════════════════

static void on_save_report(GtkButton *btn, gpointer data)
{
    (void)btn; (void)data;
    saveReport(&g_stats);
    log_append("💾 Report saved to simulation_report.txt");
}

// ══════════════════════════════════════════════════════════════
//  Reset  (GtkAlertDialog – modern GTK4 non-deprecated API)
// ══════════════════════════════════════════════════════════════

typedef struct {
    GtkAlertDialog *alert;
} ResetCtx;

static void on_reset_response(GObject *source, GAsyncResult *result, gpointer user_data)
{
    (void)user_data;
    GtkAlertDialog *alert = GTK_ALERT_DIALOG(source);
    int choice = gtk_alert_dialog_choose_finish(alert, result, NULL);
    if (choice == 0) { /* "Reset" button (first button) */
        resetSimulation(&g_north, &g_south, &g_east, &g_west, &g_stats);
        gtk_text_buffer_set_text(g_log_buf, "", -1);
        log_append("🔄 Simulation reset successfully.");
        refresh_ui();
    }
}

static void on_reset(GtkButton *btn, gpointer data)
{
    (void)btn; (void)data;

    GtkAlertDialog *alert = gtk_alert_dialog_new("Reset Simulation?");
    gtk_alert_dialog_set_detail(alert,
        "This will clear all queues and reset statistics.");
    const char *buttons[] = { "Reset", "Cancel", NULL };
    gtk_alert_dialog_set_buttons(alert, buttons);
    gtk_alert_dialog_set_cancel_button(alert, 1);
    gtk_alert_dialog_set_default_button(alert, 1);
    gtk_alert_dialog_choose(alert, GTK_WINDOW(g_window),
                            NULL, on_reset_response, NULL);
    g_object_unref(alert);
}

// ══════════════════════════════════════════════════════════════
//  View Queue (popup window, no deprecated dialog)
// ══════════════════════════════════════════════════════════════

static void show_queue_window(int d)
{
    Queue *q = dir_queue(d);

    char title[48];
    snprintf(title, sizeof title, "%s Queue  (%d vehicles)", directionName(d), q->size);

    GtkWidget *win = gtk_window_new();
    gtk_window_set_title(GTK_WINDOW(win), title);
    gtk_window_set_transient_for(GTK_WINDOW(win), GTK_WINDOW(g_window));
    gtk_window_set_modal(GTK_WINDOW(win), TRUE);
    gtk_window_set_default_size(GTK_WINDOW(win), 400, 300);

    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_window_set_child(GTK_WINDOW(win), vbox);

    GtkWidget *scroll = gtk_scrolled_window_new();
    gtk_widget_set_vexpand(scroll, TRUE);
    gtk_box_append(GTK_BOX(vbox), scroll);

    GtkWidget *list = gtk_list_box_new();
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(list), GTK_SELECTION_NONE);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), list);

    if (q->size == 0) {
        GtkWidget *row = gtk_list_box_row_new();
        GtkWidget *lbl = gtk_label_new("Queue is empty.");
        gtk_widget_set_margin_top(lbl, 16);
        gtk_list_box_row_set_child(GTK_LIST_BOX_ROW(row), lbl);
        gtk_list_box_append(GTK_LIST_BOX(list), row);
    } else {
        node *it = q->front;
        while (it) {
            char buf[128];
            snprintf(buf, sizeof buf,
                     "  #%-4d  %-14s  %s  (arr: %d s)",
                     it->vehicle.id, it->vehicle.type,
                     it->vehicle.emergency ? "🚨 EMERGENCY" : "Normal",
                     it->vehicle.arrivalTime);

            GtkWidget *row = gtk_list_box_row_new();
            GtkWidget *lbl = gtk_label_new(buf);
            gtk_label_set_xalign(GTK_LABEL(lbl), 0);
            gtk_widget_set_margin_start(lbl, 8);
            gtk_widget_set_margin_end(lbl, 8);
            gtk_widget_set_margin_top(lbl, 4);
            gtk_widget_set_margin_bottom(lbl, 4);
            if (it->vehicle.emergency)
                gtk_widget_add_css_class(lbl, "em-label");
            gtk_list_box_row_set_child(GTK_LIST_BOX_ROW(row), lbl);
            gtk_list_box_append(GTK_LIST_BOX(list), row);
            it = it->next;
        }
    }

    GtkWidget *close_btn = gtk_button_new_with_label("Close");
    gtk_widget_add_css_class(close_btn, "small-btn");
    gtk_widget_set_margin_start(close_btn, 12);
    gtk_widget_set_margin_end(close_btn, 12);
    gtk_widget_set_margin_top(close_btn, 8);
    gtk_widget_set_margin_bottom(close_btn, 8);
    gtk_widget_set_halign(close_btn, GTK_ALIGN_END);
    g_signal_connect_swapped(close_btn, "clicked", G_CALLBACK(gtk_window_close), win);
    gtk_box_append(GTK_BOX(vbox), close_btn);

    gtk_window_present(GTK_WINDOW(win));
}

static void on_view_north(GtkButton *b, gpointer d) {(void)b;(void)d; show_queue_window(NORTH);}
static void on_view_south(GtkButton *b, gpointer d) {(void)b;(void)d; show_queue_window(SOUTH);}
static void on_view_east (GtkButton *b, gpointer d) {(void)b;(void)d; show_queue_window(EAST); }
static void on_view_west (GtkButton *b, gpointer d) {(void)b;(void)d; show_queue_window(WEST); }

// ══════════════════════════════════════════════════════════════
//  UI Builder helpers
// ══════════════════════════════════════════════════════════════

static GtkWidget *make_label(const char *text, const char *css_class)
{
    GtkWidget *lbl = gtk_label_new(text);
    if (css_class) gtk_widget_add_css_class(lbl, css_class);
    gtk_label_set_xalign(GTK_LABEL(lbl), 0.5f);
    return lbl;
}

static GtkWidget *build_lane_card(int d)
{
    GtkWidget *card = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_widget_add_css_class(card, "lane-card");
    gtk_widget_add_css_class(card, dir_css_class(d));
    gtk_widget_set_margin_bottom(card, 8);

    gtk_box_append(GTK_BOX(card), make_label(directionName(d), "lane-dir-title"));

    g_lane_count[d] = make_label("0", "lane-count");
    gtk_box_append(GTK_BOX(card), g_lane_count[d]);

    g_lane_priority[d] = make_label("Priority: 0", "lane-info");
    gtk_box_append(GTK_BOX(card), g_lane_priority[d]);

    g_lane_emergency[d] = make_label("Normal", "lane-info");
    gtk_box_append(GTK_BOX(card), g_lane_emergency[d]);

    g_lane_status[d] = make_label("EMPTY", "density-empty");
    gtk_widget_add_css_class(g_lane_status[d], "density-badge");
    gtk_box_append(GTK_BOX(card), g_lane_status[d]);

    GtkWidget *view_btn = gtk_button_new_with_label("View Queue");
    gtk_widget_add_css_class(view_btn, "small-btn");
    gtk_widget_set_margin_top(view_btn, 4);

    typedef void (*cbfn)(GtkButton*, gpointer);
    cbfn cbs[] = {on_view_north, on_view_south, on_view_east, on_view_west};
    g_signal_connect(view_btn, "clicked", G_CALLBACK(cbs[d]), NULL);
    gtk_box_append(GTK_BOX(card), view_btn);

    return card;
}

static void make_stat_row(GtkWidget *grid, int row, const char *label, GtkWidget **val_out)
{
    GtkWidget *key = gtk_label_new(label);
    gtk_label_set_xalign(GTK_LABEL(key), 0);
    gtk_widget_add_css_class(key, "stat-key");
    gtk_grid_attach(GTK_GRID(grid), key, 0, row, 1, 1);

    *val_out = gtk_label_new("0");
    gtk_label_set_xalign(GTK_LABEL(*val_out), 1);
    gtk_widget_add_css_class(*val_out, "stat-val");
    gtk_widget_set_hexpand(*val_out, TRUE);
    gtk_grid_attach(GTK_GRID(grid), *val_out, 1, row, 1, 1);
}

// ══════════════════════════════════════════════════════════════
//  App Activate
// ══════════════════════════════════════════════════════════════

static const char *APP_CSS =
    "window { background:#0d0e11; color:#e2e8f0; }\n"
    "headerbar { background:#1a1d26; border-bottom:1px solid #2d3040; }\n"
    ".header-title { font-size:20px; font-weight:800; color:#fff; }\n"
    ".header-sub   { font-size:11px; color:#7c8599; }\n"
    ".time-badge   { background:#1e2130; border:1px solid #2a2f42; border-radius:20px;"
    "                padding:4px 14px; color:#64b5f6; font-size:13px; font-weight:600; }\n"
    /* Lane cards */
    ".lane-card      { background:#13151f; border-radius:12px; padding:12px 10px;"
    "                  border:1px solid #1e2130; }\n"
    ".dir-north { border-top:3px solid #3b82f6; }\n"
    ".dir-south { border-top:3px solid #22c55e; }\n"
    ".dir-east  { border-top:3px solid #f59e0b; }\n"
    ".dir-west  { border-top:3px solid #ec4899; }\n"
    ".lane-dir-title { font-size:11px; font-weight:700; letter-spacing:2px; color:#7c8599; }\n"
    ".lane-count     { font-size:32px; font-weight:800; color:#e2e8f0; }\n"
    ".lane-info      { font-size:11px; color:#7c8599; }\n"
    /* Density */
    ".density-badge  { border-radius:20px; padding:2px 10px; font-size:10px; font-weight:700;"
    "                  letter-spacing:1px; }\n"
    ".density-empty  { background:#1e2130; color:#7c8599; }\n"
    ".density-low    { background:#14532d; color:#4ade80; }\n"
    ".density-medium { background:#78350f; color:#fbbf24; }\n"
    ".density-high   { background:#7f1d1d; color:#f87171; }\n"
    /* Heap */
    ".heap-section { background:#13151f; border-radius:12px; border:1px solid #1e2130; padding:10px 0; }\n"
    ".heap-title   { font-size:11px; font-weight:700; letter-spacing:2px; color:#7c8599; padding:0 12px 6px; }\n"
    ".heap-top     { color:#fbbf24; font-weight:700; }\n"
    ".heap-empty   { color:#7c8599; font-style:italic; padding:8px 12px; }\n"
    /* Stats */
    ".stats-panel { background:#13151f; border-radius:12px; border:1px solid #1e2130; padding:14px; }\n"
    ".stats-title { font-size:11px; font-weight:700; letter-spacing:2px; color:#7c8599; margin-bottom:6px; }\n"
    ".stat-key    { font-size:12px; color:#7c8599; }\n"
    ".stat-val    { font-size:13px; font-weight:700; color:#e2e8f0; }\n"
    /* Buttons */
    "button { border-radius:8px; }\n"
    ".action-btn { background:linear-gradient(135deg,#3b82f6,#2563eb); color:#fff;"
    "              font-weight:700; font-size:13px; border:none; padding:8px 18px; border-radius:8px; }\n"
    ".action-btn:hover { background:linear-gradient(135deg,#60a5fa,#3b82f6); }\n"
    ".danger-btn { background:linear-gradient(135deg,#ef4444,#b91c1c); color:#fff;"
    "              font-weight:700; font-size:13px; border:none; padding:8px 18px; border-radius:8px; }\n"
    ".danger-btn:hover { background:linear-gradient(135deg,#f87171,#ef4444); }\n"
    ".green-btn  { background:linear-gradient(135deg,#22c55e,#15803d); color:#fff;"
    "              font-weight:700; font-size:13px; border:none; padding:8px 18px; border-radius:8px; }\n"
    ".green-btn:hover  { background:linear-gradient(135deg,#4ade80,#22c55e); }\n"
    ".amber-btn  { background:linear-gradient(135deg,#f59e0b,#d97706); color:#000;"
    "              font-weight:700; font-size:13px; border:none; padding:8px 18px; border-radius:8px; }\n"
    ".amber-btn:hover  { background:linear-gradient(135deg,#fbbf24,#f59e0b); }\n"
    ".small-btn  { background:#1e2130; color:#94a3b8; font-size:11px; border:1px solid #2a2f42;"
    "              padding:4px 10px; border-radius:6px; }\n"
    ".small-btn:hover  { background:#252a3a; color:#e2e8f0; }\n"
    /* Log */
    ".log-section  { background:#0a0b0e; border-radius:10px; border:1px solid #1e2130; }\n"
    ".section-title{ font-size:11px; font-weight:700; letter-spacing:2px; color:#7c8599; padding:8px 12px 0; }\n"
    ".log-view, .log-view > text { background:#0a0b0e; color:#4ade80; font-family:monospace; font-size:12px; }\n"
    ".log-view > text { caret-color:#4ade80; }\n"
    /* Canvas */
    ".canvas-frame { background:#0d0e11; border-radius:12px; border:1px solid #1e2130; }\n"
    ".em-label { color:#f87171; }\n";

static void activate(GtkApplication *app, gpointer user_data)
{
    (void)user_data;

    // CSS
    GtkCssProvider *css = gtk_css_provider_new();
    gtk_css_provider_load_from_string(css, APP_CSS);
    gtk_style_context_add_provider_for_display(
        gdk_display_get_default(),
        GTK_STYLE_PROVIDER(css),
        GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);

    // Window
    g_window = gtk_application_window_new(app);
    gtk_window_set_title(GTK_WINDOW(g_window), "Smart Traffic Flow Simulator");
    gtk_window_set_default_size(GTK_WINDOW(g_window), 1280, 820);

    // Header bar
    GtkWidget *headerbar = gtk_header_bar_new();
    gtk_header_bar_set_show_title_buttons(GTK_HEADER_BAR(headerbar), TRUE);
    gtk_window_set_titlebar(GTK_WINDOW(g_window), headerbar);

    GtkWidget *title_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    GtkWidget *h_title   = gtk_label_new("🚦 Smart Traffic Flow");
    gtk_widget_add_css_class(h_title, "header-title");
    GtkWidget *h_sub = gtk_label_new("DSA Priority Queue Simulator");
    gtk_widget_add_css_class(h_sub, "header-sub");
    gtk_box_append(GTK_BOX(title_box), h_title);
    gtk_box_append(GTK_BOX(title_box), h_sub);
    gtk_header_bar_set_title_widget(GTK_HEADER_BAR(headerbar), title_box);

    g_time_label = gtk_label_new("⏱  Time: 0 s");
    gtk_widget_add_css_class(g_time_label, "time-badge");
    gtk_header_bar_pack_end(GTK_HEADER_BAR(headerbar), g_time_label);

    // Root layout
    GtkWidget *root_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_window_set_child(GTK_WINDOW(g_window), root_vbox);

    // Paned
    GtkWidget *hpaned = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_widget_set_vexpand(hpaned, TRUE);
    gtk_paned_set_position(GTK_PANED(hpaned), 310);
    gtk_box_append(GTK_BOX(root_vbox), hpaned);

    // ──────── LEFT PANEL ────────
    GtkWidget *left_scroll = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(left_scroll),
                                   GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_widget_set_size_request(left_scroll, 280, -1);

    GtkWidget *left_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_widget_set_margin_start(left_box, 12);
    gtk_widget_set_margin_end(left_box, 12);
    gtk_widget_set_margin_top(left_box, 12);
    gtk_widget_set_margin_bottom(left_box, 12);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(left_scroll), left_box);
    gtk_paned_set_start_child(GTK_PANED(hpaned), left_scroll);

    GtkWidget *lanes_title = gtk_label_new("LANE STATUS");
    gtk_widget_add_css_class(lanes_title, "section-title");
    gtk_label_set_xalign(GTK_LABEL(lanes_title), 0);
    gtk_box_append(GTK_BOX(left_box), lanes_title);

    for (int d = 0; d < 4; d++)
        gtk_box_append(GTK_BOX(left_box), build_lane_card(d));

    // Heap section
    GtkWidget *heap_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_add_css_class(heap_box, "heap-section");
    gtk_box_append(GTK_BOX(left_box), heap_box);

    GtkWidget *heap_lbl = gtk_label_new("MAX HEAP  (priority order)");
    gtk_widget_add_css_class(heap_lbl, "heap-title");
    gtk_label_set_xalign(GTK_LABEL(heap_lbl), 0);
    gtk_box_append(GTK_BOX(heap_box), heap_lbl);

    g_heap_list = gtk_list_box_new();
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(g_heap_list), GTK_SELECTION_NONE);
    gtk_box_append(GTK_BOX(heap_box), g_heap_list);

    // Stats section
    GtkWidget *stats_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_widget_add_css_class(stats_box, "stats-panel");
    gtk_box_append(GTK_BOX(left_box), stats_box);

    GtkWidget *stats_title = gtk_label_new("STATISTICS");
    gtk_widget_add_css_class(stats_title, "stats-title");
    gtk_label_set_xalign(GTK_LABEL(stats_title), 0);
    gtk_box_append(GTK_BOX(stats_box), stats_title);

    GtkWidget *stat_grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(stat_grid), 4);
    gtk_grid_set_column_spacing(GTK_GRID(stat_grid), 20);
    gtk_widget_set_hexpand(stat_grid, TRUE);
    gtk_box_append(GTK_BOX(stats_box), stat_grid);

    make_stat_row(stat_grid, 0, "Total Vehicles",  &g_stat_total);
    make_stat_row(stat_grid, 1, "Vehicles Passed",  &g_stat_passed);
    make_stat_row(stat_grid, 2, "Emergency",         &g_stat_emergency);
    make_stat_row(stat_grid, 3, "Avg Wait",          &g_stat_avg_wait);
    make_stat_row(stat_grid, 4, "Max Wait",          &g_stat_max_wait);
    make_stat_row(stat_grid, 5, "North Passed",      &g_stat_n);
    make_stat_row(stat_grid, 6, "South Passed",      &g_stat_s);
    make_stat_row(stat_grid, 7, "East Passed",       &g_stat_e);
    make_stat_row(stat_grid, 8, "West Passed",       &g_stat_w);

    // ──────── RIGHT PANEL ────────
    GtkWidget *right_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_hexpand(right_box, TRUE);
    gtk_paned_set_end_child(GTK_PANED(hpaned), right_box);

    // Canvas
    GtkWidget *canvas_frame = gtk_frame_new(NULL);
    gtk_widget_add_css_class(canvas_frame, "canvas-frame");
    gtk_widget_set_margin_start(canvas_frame, 10);
    gtk_widget_set_margin_end(canvas_frame, 10);
    gtk_widget_set_margin_top(canvas_frame, 10);
    gtk_widget_set_vexpand(canvas_frame, TRUE);

    g_canvas = gtk_drawing_area_new();
    gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(g_canvas), on_canvas_draw, NULL, NULL);
    gtk_widget_set_vexpand(g_canvas, TRUE);
    gtk_frame_set_child(GTK_FRAME(canvas_frame), g_canvas);
    gtk_box_append(GTK_BOX(right_box), canvas_frame);

    // Log section
    GtkWidget *log_section = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_add_css_class(log_section, "log-section");
    gtk_widget_set_margin_start(log_section, 10);
    gtk_widget_set_margin_end(log_section, 10);
    gtk_widget_set_margin_top(log_section, 6);
    gtk_widget_set_margin_bottom(log_section, 6);

    GtkWidget *log_title = gtk_label_new("OUTPUT LOG");
    gtk_widget_add_css_class(log_title, "section-title");
    gtk_label_set_xalign(GTK_LABEL(log_title), 0);
    gtk_box_append(GTK_BOX(log_section), log_title);

    GtkWidget *log_scroll = gtk_scrolled_window_new();
    gtk_widget_set_size_request(log_scroll, -1, 160);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(log_scroll),
                                   GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);

    g_log_buf  = gtk_text_buffer_new(NULL);
    g_log_view = gtk_text_view_new_with_buffer(g_log_buf);
    gtk_widget_add_css_class(g_log_view, "log-view");
    gtk_text_view_set_editable(GTK_TEXT_VIEW(g_log_view), FALSE);
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(g_log_view), GTK_WRAP_WORD_CHAR);
    gtk_widget_set_margin_start(g_log_view, 8);
    gtk_widget_set_margin_end(g_log_view, 8);
    gtk_widget_set_margin_top(g_log_view, 6);
    gtk_widget_set_margin_bottom(g_log_view, 6);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(log_scroll), g_log_view);
    gtk_box_append(GTK_BOX(log_section), log_scroll);
    gtk_box_append(GTK_BOX(right_box), log_section);

    // ──────── BOTTOM TOOLBAR ────────
    GtkWidget *toolbar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_set_margin_start(toolbar, 12);
    gtk_widget_set_margin_end(toolbar, 12);
    gtk_widget_set_margin_top(toolbar, 6);
    gtk_widget_set_margin_bottom(toolbar, 10);
    gtk_box_append(GTK_BOX(root_vbox), toolbar);

    struct { const char *label; const char *css; GCallback cb; } buttons[] = {
        {"➕  Add Vehicle",    "action-btn", G_CALLBACK(on_add_vehicle)    },
        {"▶️  Run Simulation", "green-btn",  G_CALLBACK(on_run_simulation) },
        {"🚧  Detect Jam",     "amber-btn",  G_CALLBACK(on_detect_jam)     },
        {"💾  Save Report",    "action-btn", G_CALLBACK(on_save_report)    },
        {"🔄  Reset",          "danger-btn", G_CALLBACK(on_reset)          },
    };
    for (int i = 0; i < (int)(sizeof(buttons)/sizeof(buttons[0])); i++) {
        GtkWidget *btn = gtk_button_new_with_label(buttons[i].label);
        gtk_widget_add_css_class(btn, buttons[i].css);
        gtk_widget_set_hexpand(btn, TRUE);
        g_signal_connect(btn, "clicked", buttons[i].cb, NULL);
        gtk_box_append(GTK_BOX(toolbar), btn);
    }

    log_append("🚦 Smart Traffic Flow Simulator ready.");
    log_append("   Use ➕ Add Vehicle to begin.");
    refresh_ui();

    gtk_window_present(GTK_WINDOW(g_window));
}

// ══════════════════════════════════════════════════════════════
//  main
// ══════════════════════════════════════════════════════════════

int main(int argc, char *argv[])
{
    initializeQueue(&g_north);
    initializeQueue(&g_south);
    initializeQueue(&g_east);
    initializeQueue(&g_west);
    memset(&g_stats, 0, sizeof(g_stats));

    GtkApplication *app = gtk_application_new(
        "com.traffic.simulator",
        G_APPLICATION_DEFAULT_FLAGS);

    g_signal_connect(app, "activate", G_CALLBACK(activate), NULL);
    int status = g_application_run(G_APPLICATION(app), argc, argv);
    g_object_unref(app);
    return status;
}
