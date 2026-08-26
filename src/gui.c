/*
 * gui.c — GTK3 control panel implementation for live-wallpaper.
 *
 * This module creates a compact GUI control panel that allows the user to:
 *   - Select video files (.mp4, .mkv, .webm) via GtkFileChooserDialog
 *   - Pause / Resume playback
 *   - Stop wallpaper playback (hides wallpaper window)
 *   - Quit the application cleanly
 *   - View the currently loaded file path
 *
 * It also manages a high-resolution GTK render timer (g_timeout_add) that
 * pulls decoded frames from the bounded queue and renders them through SDL2
 * on the main GTK thread.
 */

#include "gui.h"
#include "decoder.h"
#include "config.h"
#include <gtk/gtk.h>
#include <stdio.h>

struct GuiCtx {
    AppState     *state;
    WallpaperCtx *wallpaper;

    GtkWidget    *window;
    GtkWidget    *status_label;
    GtkWidget    *pause_btn;
    GtkWidget    *stop_btn;

    guint         render_timer_id;
};

/* Forward declarations */
static gboolean on_render_tick(gpointer user_data);
static void on_choose_video_clicked(GtkButton *button, gpointer user_data);
static void on_pause_toggled(GtkButton *button, gpointer user_data);
static void on_stop_clicked(GtkButton *button, gpointer user_data);
static void on_quit_clicked(GtkButton *button, gpointer user_data);
static gboolean on_window_delete_event(GtkWidget *widget, GdkEvent *event, gpointer user_data);

/* ─────────────────────────────────────────────────────────────────────────────
 * Load and start playing a selected video file.
 * ──────────────────────────────────────────────────────────────────────────── */
static void load_and_play_video(GuiCtx *ctx, const char *filepath)
{
    if (!filepath || !filepath[0]) return;

    /* Stop previous playback if running */
    gui_stop_render_timer(ctx);
    decoder_stop(ctx->state);

    /* Start decoding the new video */
    if (decoder_start(ctx->state, filepath) != 0) {
        gui_set_status(ctx, "Error opening video file.");
        return;
    }

    /* Wait a short moment for decoder to read headers & set dimensions */
    for (int i = 0; i < 50 && (ctx->state->video_width <= 0); i++) {
        g_usleep(10000); /* 10ms */
    }

    if (ctx->state->video_width > 0 && ctx->state->video_height > 0) {
        wallpaper_set_video_size(ctx->wallpaper,
                                 ctx->state->video_width,
                                 ctx->state->video_height);
    }

    wallpaper_show(ctx->wallpaper);
    gui_start_render_timer(ctx);

    /* Save to persistent config */
    config_save(filepath);

    /* Update GUI */
    char label_buf[LW_MAX_PATH + 32];
    snprintf(label_buf, sizeof(label_buf), "Playing:\n%s", filepath);
    gui_set_status(ctx, label_buf);

    if (ctx->pause_btn) {
        gtk_button_set_label(GTK_BUTTON(ctx->pause_btn), "Pause");
    }
}

/* ─────────────────────────────────────────────────────────────────────────────
 * Render timer callback (~60fps) running on GTK main loop.
 * ──────────────────────────────────────────────────────────────────────────── */
static gboolean on_render_tick(gpointer user_data)
{
    GuiCtx *ctx = (GuiCtx *)user_data;

    /* Process any pending SDL events */
    if (!wallpaper_process_events(ctx->wallpaper)) {
        gtk_main_quit();
        return G_SOURCE_REMOVE;
    }

    if (atomic_load(&ctx->state->quit)) {
        return G_SOURCE_REMOVE;
    }

    if (!atomic_load(&ctx->state->paused) && atomic_load(&ctx->state->playing)) {
        VideoFrame frame;
        if (frame_queue_pop(&ctx->state->queue, &frame)) {
            wallpaper_render_frame(ctx->wallpaper, &frame);
            video_frame_free(&frame);
        }
    }

    return G_SOURCE_CONTINUE;
}

/* ─────────────────────────────────────────────────────────────────────────────
 * GTK Signal Callbacks
 * ──────────────────────────────────────────────────────────────────────────── */
static void on_choose_video_clicked(GtkButton *button, gpointer user_data)
{
    (void)button;
    GuiCtx *ctx = (GuiCtx *)user_data;

    GtkWidget *dialog = gtk_file_chooser_dialog_new(
        "Select Live Wallpaper Video",
        GTK_WINDOW(ctx->window),
        GTK_FILE_CHOOSER_ACTION_OPEN,
        "_Cancel", GTK_RESPONSE_CANCEL,
        "_Open", GTK_RESPONSE_ACCEPT,
        NULL);

    /* Add file filter for common video formats */
    GtkFileFilter *filter = gtk_file_filter_new();
    gtk_file_filter_set_name(filter, "Video Files (*.mp4, *.mkv, *.webm, *.avi, *.mov)");
    gtk_file_filter_add_pattern(filter, "*.mp4");
    gtk_file_filter_add_pattern(filter, "*.mkv");
    gtk_file_filter_add_pattern(filter, "*.webm");
    gtk_file_filter_add_pattern(filter, "*.avi");
    gtk_file_filter_add_pattern(filter, "*.mov");
    gtk_file_filter_add_pattern(filter, "*.MP4");
    gtk_file_filter_add_pattern(filter, "*.MKV");
    gtk_file_filter_add_pattern(filter, "*.WEBM");
    gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(dialog), filter);

    GtkFileFilter *all_filter = gtk_file_filter_new();
    gtk_file_filter_set_name(all_filter, "All Files");
    gtk_file_filter_add_pattern(all_filter, "*");
    gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(dialog), all_filter);

    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
        char *filename = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));
        if (filename) {
            load_and_play_video(ctx, filename);
            g_free(filename);
        }
    }

    gtk_widget_destroy(dialog);
}

static void on_pause_toggled(GtkButton *button, gpointer user_data)
{
    GuiCtx *ctx = (GuiCtx *)user_data;

    if (!atomic_load(&ctx->state->playing)) return;

    if (atomic_load(&ctx->state->paused)) {
        decoder_resume(ctx->state);
        gtk_button_set_label(button, "Pause");
        gui_set_status(ctx, "Playback resumed.");
    } else {
        decoder_pause(ctx->state);
        gtk_button_set_label(button, "Resume");
        gui_set_status(ctx, "Playback paused.");
    }
}

static void on_stop_clicked(GtkButton *button, gpointer user_data)
{
    (void)button;
    GuiCtx *ctx = (GuiCtx *)user_data;

    gui_stop_render_timer(ctx);
    decoder_stop(ctx->state);
    wallpaper_hide(ctx->wallpaper);

    if (ctx->pause_btn) {
        gtk_button_set_label(GTK_BUTTON(ctx->pause_btn), "Pause");
    }
    gui_set_status(ctx, "Wallpaper stopped.");
}

static void on_quit_clicked(GtkButton *button, gpointer user_data)
{
    (void)button;
    (void)user_data;
    gtk_main_quit();
}

static gboolean on_window_delete_event(GtkWidget *widget, GdkEvent *event, gpointer user_data)
{
    (void)widget;
    (void)event;
    (void)user_data;
    /* Clean quit on window close */
    gtk_main_quit();
    return FALSE;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Public API
 * ═══════════════════════════════════════════════════════════════════════════ */

GuiCtx *gui_create(AppState *state, WallpaperCtx *wallpaper)
{
    GuiCtx *ctx = (GuiCtx *)calloc(1, sizeof(GuiCtx));
    if (!ctx) return NULL;

    ctx->state = state;
    ctx->wallpaper = wallpaper;
    ctx->render_timer_id = 0;

    /* Create main window */
    ctx->window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(ctx->window), "Live Wallpaper Control Panel");
    gtk_window_set_default_size(GTK_WINDOW(ctx->window), 380, 260);
    gtk_window_set_position(GTK_WINDOW(ctx->window), GTK_WIN_POS_CENTER);
    gtk_container_set_border_width(GTK_CONTAINER(ctx->window), 16);

    g_signal_connect(ctx->window, "delete-event", G_CALLBACK(on_window_delete_event), ctx);

    /* Main vertical container */
    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_container_add(GTK_CONTAINER(ctx->window), vbox);

    /* Title label */
    GtkWidget *title = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(title), "<b><big>Live Wallpaper</big></b>");
    gtk_box_pack_start(GTK_BOX(vbox), title, FALSE, FALSE, 0);

    /* Status / File label */
    ctx->status_label = gtk_label_new("No video loaded.\nClick 'Choose Video' to start.");
    gtk_label_set_line_wrap(GTK_LABEL(ctx->status_label), TRUE);
    gtk_label_set_max_width_chars(GTK_LABEL(ctx->status_label), 40);
    gtk_label_set_justify(GTK_LABEL(ctx->status_label), GTK_JUSTIFY_CENTER);
    gtk_box_pack_start(GTK_BOX(vbox), ctx->status_label, TRUE, TRUE, 4);

    /* Separator */
    GtkWidget *sep = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_box_pack_start(GTK_BOX(vbox), sep, FALSE, FALSE, 2);

    /* Buttons box */
    GtkWidget *btn_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_box_pack_start(GTK_BOX(vbox), btn_box, FALSE, FALSE, 0);

    /* Choose video button */
    GtkWidget *choose_btn = gtk_button_new_with_label("Choose Video...");
    g_signal_connect(choose_btn, "clicked", G_CALLBACK(on_choose_video_clicked), ctx);
    gtk_box_pack_start(GTK_BOX(btn_box), choose_btn, FALSE, FALSE, 0);

    /* Row for Pause / Stop buttons */
    GtkWidget *hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_box_pack_start(GTK_BOX(btn_box), hbox, FALSE, FALSE, 0);

    ctx->pause_btn = gtk_button_new_with_label("Pause");
    g_signal_connect(ctx->pause_btn, "clicked", G_CALLBACK(on_pause_toggled), ctx);
    gtk_box_pack_start(GTK_BOX(hbox), ctx->pause_btn, TRUE, TRUE, 0);

    ctx->stop_btn = gtk_button_new_with_label("Stop");
    g_signal_connect(ctx->stop_btn, "clicked", G_CALLBACK(on_stop_clicked), ctx);
    gtk_box_pack_start(GTK_BOX(hbox), ctx->stop_btn, TRUE, TRUE, 0);

    /* Quit button */
    GtkWidget *quit_btn = gtk_button_new_with_label("Quit");
    g_signal_connect(quit_btn, "clicked", G_CALLBACK(on_quit_clicked), ctx);
    gtk_box_pack_start(GTK_BOX(btn_box), quit_btn, FALSE, FALSE, 0);

    gtk_widget_show_all(ctx->window);
    return ctx;
}

void gui_destroy(GuiCtx *ctx)
{
    if (!ctx) return;
    gui_stop_render_timer(ctx);
    if (ctx->window) {
        gtk_widget_destroy(ctx->window);
        ctx->window = NULL;
    }
    free(ctx);
}

void gui_show(GuiCtx *ctx)
{
    if (ctx && ctx->window) {
        gtk_widget_show_all(ctx->window);
        gtk_window_present(GTK_WINDOW(ctx->window));
    }
}

void gui_start_render_timer(GuiCtx *ctx)
{
    if (!ctx) return;
    if (ctx->render_timer_id == 0) {
        /* ~60 FPS timer (16ms) */
        ctx->render_timer_id = g_timeout_add(16, on_render_tick, ctx);
    }
}

void gui_stop_render_timer(GuiCtx *ctx)
{
    if (!ctx) return;
    if (ctx->render_timer_id != 0) {
        g_source_remove(ctx->render_timer_id);
        ctx->render_timer_id = 0;
    }
}

void gui_set_status(GuiCtx *ctx, const char *text)
{
    if (ctx && ctx->status_label && text) {
        gtk_label_set_text(GTK_LABEL(ctx->status_label), text);
    }
}
