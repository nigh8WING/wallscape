/*
 * gui.c — Modern Grid Gallery & Control Panel for Live Wallpaper.
 *
 * Features:
 *   - Visual Grid Gallery using GtkFlowBox with video thumbnails
 *   - Custom GTK3 CSS styling (rounded cards, hover glow, active borders)
 *   - Active wallpaper indicator with green checkmark badge
 *   - Confirmation dialog before turning ON or OFF a wallpaper
 *   - Folder selector ("📁 Select Folder...")
 *   - Nice symbolic icons on buttons
 *   - Render timer driving ~60fps SDL2 output safely on the main thread
 */

#include "gui.h"
#include "decoder.h"
#include "config.h"
#include "thumbnail.h"

#include <gtk/gtk.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>

#define MAX_LOADED_WALLPAPERS 256
#define THUMB_WIDTH  180
#define THUMB_HEIGHT 110

/* CSS Stylesheet for polished modern UI */
static const char *CUSTOM_CSS =
"window.main-window {"
"    background-color: @theme_bg_color;"
"}"
".top-header {"
"    padding: 10px 14px;"
"    background-color: alpha(@theme_bg_color, 0.95);"
"    border-bottom: 1px solid alpha(@theme_fg_color, 0.12);"
"}"
".folder-banner {"
"    font-size: 11px;"
"    opacity: 0.85;"
"}"
".gallery-scroll {"
"    background-color: alpha(@theme_bg_color, 0.5);"
"}"
".wallpaper-card {"
"    background-color: alpha(@theme_base_color, 0.9);"
"    border-radius: 10px;"
"    border: 1px solid alpha(@theme_fg_color, 0.14);"
"    padding: 8px;"
"    margin: 6px;"
"    transition: all 150ms ease-in-out;"
"}"
".wallpaper-card:hover {"
"    border-color: #3584e4;"
"    background-color: alpha(#3584e4, 0.10);"
"}"
".wallpaper-card.active {"
"    border: 2px solid #2ec27e;"
"    background-color: alpha(#2ec27e, 0.14);"
"}"
".card-title {"
"    font-size: 11px;"
"    font-weight: 500;"
"    margin-top: 6px;"
"}"
".active-badge {"
"    background-color: #2ec27e;"
"    color: white;"
"    font-weight: bold;"
"    font-size: 10px;"
"    border-radius: 10px;"
"    padding: 2px 6px;"
"}"
".bottom-bar {"
"    padding: 10px 14px;"
"    background-color: alpha(@theme_bg_color, 0.95);"
"    border-top: 1px solid alpha(@theme_fg_color, 0.12);"
"}";

struct GuiCtx {
    AppState     *state;
    WallpaperCtx *wallpaper;

    GtkWidget    *window;
    GtkWidget    *folder_label;
    GtkWidget    *status_label;
    GtkWidget    *flow_box;
    GtkWidget    *scrolled_window;
    GtkWidget    *pause_btn;
    GtkWidget    *stop_btn;

    /* Stored wallpaper metadata */
    char          wallpaper_paths[MAX_LOADED_WALLPAPERS][LW_MAX_PATH];
    GtkWidget    *card_widgets[MAX_LOADED_WALLPAPERS];
    GtkWidget    *badge_widgets[MAX_LOADED_WALLPAPERS];
    GtkWidget    *title_widgets[MAX_LOADED_WALLPAPERS];
    int           wallpaper_count;
    int           active_idx;

    guint         render_timer_id;
};

/* Supported video file extensions */
static const char *const SUPPORTED_EXTS[] = {
    ".mp4", ".mkv", ".webm", ".avi", ".mov", NULL
};

/* Forward declarations */
static gboolean on_render_tick(gpointer user_data);
static void on_choose_folder_clicked(GtkButton *button, gpointer user_data);
static void on_card_clicked(GtkButton *button, gpointer user_data);
static void on_pause_toggled(GtkButton *button, gpointer user_data);
static void on_stop_clicked(GtkButton *button, gpointer user_data);
static void on_quit_clicked(GtkButton *button, gpointer user_data);
static gboolean on_window_delete_event(GtkWidget *widget, GdkEvent *event, gpointer user_data);
static void populate_folder_wallpapers(GuiCtx *ctx, const char *folder_path, const char *active_filepath);
static void update_card_visuals(GuiCtx *ctx, int active_idx);
static void start_wallpaper_playback(GuiCtx *ctx, const char *filepath);
static void stop_wallpaper_playback(GuiCtx *ctx);

/* ─────────────────────────────────────────────────────────────────────────────
 * Check if a filename ends with a supported video extension
 * ──────────────────────────────────────────────────────────────────────────── */
static gboolean is_video_file(const char *filename)
{
    if (!filename) return FALSE;
    size_t name_len = strlen(filename);

    for (int i = 0; SUPPORTED_EXTS[i] != NULL; i++) {
        size_t ext_len = strlen(SUPPORTED_EXTS[i]);
        if (name_len >= ext_len) {
            if (strcasecmp(filename + (name_len - ext_len), SUPPORTED_EXTS[i]) == 0) {
                return TRUE;
            }
        }
    }
    return FALSE;
}

/* ─────────────────────────────────────────────────────────────────────────────
 * Confirmation Dialogs
 * ──────────────────────────────────────────────────────────────────────────── */
static gboolean confirm_apply_wallpaper(GtkWindow *parent, const char *filename)
{
    char *base = g_path_get_basename(filename);
    GtkWidget *dialog = gtk_message_dialog_new(
        parent,
        GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
        GTK_MESSAGE_QUESTION,
        GTK_BUTTONS_CANCEL,
        "Apply Live Wallpaper?");

    gtk_message_dialog_format_secondary_text(
        GTK_MESSAGE_DIALOG(dialog),
        "Do you want to set \"%s\" as your active desktop wallpaper?",
        base);

    gtk_dialog_add_button(GTK_DIALOG(dialog), "Apply Wallpaper", GTK_RESPONSE_ACCEPT);
    gtk_dialog_set_default_response(GTK_DIALOG(dialog), GTK_RESPONSE_ACCEPT);

    gint result = gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);
    g_free(base);

    return (result == GTK_RESPONSE_ACCEPT);
}

static gboolean confirm_turn_off_wallpaper(GtkWindow *parent)
{
    GtkWidget *dialog = gtk_message_dialog_new(
        parent,
        GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
        GTK_MESSAGE_QUESTION,
        GTK_BUTTONS_CANCEL,
        "Turn Off Live Wallpaper?");

    gtk_message_dialog_format_secondary_text(
        GTK_MESSAGE_DIALOG(dialog),
        "Are you sure you want to stop the wallpaper playback and restore the default desktop?");

    gtk_dialog_add_button(GTK_DIALOG(dialog), "Turn Off", GTK_RESPONSE_ACCEPT);
    gtk_dialog_set_default_response(GTK_DIALOG(dialog), GTK_RESPONSE_ACCEPT);

    gint result = gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);

    return (result == GTK_RESPONSE_ACCEPT);
}

/* ─────────────────────────────────────────────────────────────────────────────
 * Playback Control
 * ──────────────────────────────────────────────────────────────────────────── */
static void start_wallpaper_playback(GuiCtx *ctx, const char *filepath)
{
    if (!filepath || !filepath[0]) return;

    gui_stop_render_timer(ctx);
    decoder_stop(ctx->state);

    if (decoder_start(ctx->state, filepath) != 0) {
        gui_set_status(ctx, "Error: Failed to decode video file.");
        return;
    }

    for (int i = 0; i < 50 && (ctx->state->video_width <= 0); i++) {
        g_usleep(10000);
    }

    if (ctx->state->video_width > 0 && ctx->state->video_height > 0) {
        wallpaper_set_video_size(ctx->wallpaper,
                                 ctx->state->video_width,
                                 ctx->state->video_height);
    }

    wallpaper_show(ctx->wallpaper);
    gui_start_render_timer(ctx);

    config_save(filepath);

    if (ctx->pause_btn) {
        gtk_button_set_label(GTK_BUTTON(ctx->pause_btn), "Pause");
        gtk_button_set_image(GTK_BUTTON(ctx->pause_btn),
                             gtk_image_new_from_icon_name("media-playback-pause-symbolic", GTK_ICON_SIZE_BUTTON));
    }

    char *base = g_path_get_basename(filepath);
    char status_buf[512];
    if (ctx->state->video_width > 0) {
        snprintf(status_buf, sizeof(status_buf),
                 "Playing: %s (%dx%d @ %.0ffps)",
                 base, ctx->state->video_width, ctx->state->video_height,
                 ctx->state->video_fps > 0 ? ctx->state->video_fps : 30.0);
    } else {
        snprintf(status_buf, sizeof(status_buf), "Playing: %s", base);
    }
    gui_set_status(ctx, status_buf);
    g_free(base);
}

static void stop_wallpaper_playback(GuiCtx *ctx)
{
    gui_stop_render_timer(ctx);
    decoder_stop(ctx->state);
    wallpaper_hide(ctx->wallpaper);

    update_card_visuals(ctx, -1);

    if (ctx->pause_btn) {
        gtk_button_set_label(GTK_BUTTON(ctx->pause_btn), "Pause");
        gtk_button_set_image(GTK_BUTTON(ctx->pause_btn),
                             gtk_image_new_from_icon_name("media-playback-pause-symbolic", GTK_ICON_SIZE_BUTTON));
    }
    gui_set_status(ctx, "Live wallpaper turned off.");
}

/* ─────────────────────────────────────────────────────────────────────────────
 * Update Card Visuals (CSS active class & Checkmark Badge)
 * ──────────────────────────────────────────────────────────────────────────── */
static void update_card_visuals(GuiCtx *ctx, int active_idx)
{
    ctx->active_idx = active_idx;

    for (int i = 0; i < ctx->wallpaper_count; i++) {
        GtkWidget *card  = ctx->card_widgets[i];
        GtkWidget *badge = ctx->badge_widgets[i];
        GtkWidget *title = ctx->title_widgets[i];
        if (!card) continue;

        GtkStyleContext *sc = gtk_widget_get_style_context(card);

        if (i == active_idx) {
            gtk_style_context_add_class(sc, "active");
            if (badge) gtk_widget_set_visible(badge, TRUE);
            if (title) {
                char *fname = (char *)g_object_get_data(G_OBJECT(card), "fname");
                if (fname) {
                    char markup[512];
                    snprintf(markup, sizeof(markup), "<b>%s</b>", fname);
                    gtk_label_set_markup(GTK_LABEL(title), markup);
                }
            }
        } else {
            gtk_style_context_remove_class(sc, "active");
            if (badge) gtk_widget_set_visible(badge, FALSE);
            if (title) {
                char *fname = (char *)g_object_get_data(G_OBJECT(card), "fname");
                if (fname) {
                    gtk_label_set_text(GTK_LABEL(title), fname);
                }
            }
        }
    }
}

/* ─────────────────────────────────────────────────────────────────────────────
 * Wallpaper Card Clicked
 * ──────────────────────────────────────────────────────────────────────────── */
static void on_card_clicked(GtkButton *button, gpointer user_data)
{
    GuiCtx *ctx = (GuiCtx *)user_data;
    int idx = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(button), "idx"));

    if (idx < 0 || idx >= ctx->wallpaper_count) return;

    const char *target_path = ctx->wallpaper_paths[idx];

    if (idx == ctx->active_idx && atomic_load(&ctx->state->playing)) {
        /* Already active -> Confirm Turn Off */
        if (confirm_turn_off_wallpaper(GTK_WINDOW(ctx->window))) {
            stop_wallpaper_playback(ctx);
        }
    } else {
        /* Inactive -> Confirm Turn On */
        if (confirm_apply_wallpaper(GTK_WINDOW(ctx->window), target_path)) {
            start_wallpaper_playback(ctx, target_path);
            update_card_visuals(ctx, idx);
        }
    }
}

/* ─────────────────────────────────────────────────────────────────────────────
 * Scan Folder & Build Grid Gallery
 * ──────────────────────────────────────────────────────────────────────────── */
static void populate_folder_wallpapers(GuiCtx *ctx, const char *folder_path, const char *active_filepath)
{
    if (!folder_path || !ctx->flow_box) return;

    /* Clear existing grid children */
    GList *children = gtk_container_get_children(GTK_CONTAINER(ctx->flow_box));
    for (GList *l = children; l != NULL; l = l->next) {
        gtk_widget_destroy(GTK_WIDGET(l->data));
    }
    g_list_free(children);

    ctx->wallpaper_count = 0;
    ctx->active_idx = -1;

    /* Read directory */
    GDir *dir = g_dir_open(folder_path, 0, NULL);
    if (!dir) return;

    GList *filenames = NULL;
    const char *name = NULL;
    while ((name = g_dir_read_name(dir)) != NULL) {
        if (is_video_file(name)) {
            filenames = g_list_prepend(filenames, g_strdup(name));
        }
    }
    g_dir_close(dir);

    filenames = g_list_sort(filenames, (GCompareFunc)g_strcmp0);

    guint total = g_list_length(filenames);
    char folder_text[LW_MAX_PATH + 64];
    snprintf(folder_text, sizeof(folder_text),
             "📁 <b>Wallpapers Folder:</b> %s  <span alpha='60%%'>(%u videos)</span>",
             folder_path, total);
    gtk_label_set_markup(GTK_LABEL(ctx->folder_label), folder_text);

    int target_active_idx = -1;

    for (GList *l = filenames; l != NULL; l = l->next) {
        if (ctx->wallpaper_count >= MAX_LOADED_WALLPAPERS) break;

        const char *fname = (const char *)l->data;
        char *full_path = g_build_filename(folder_path, fname, NULL);
        int idx = ctx->wallpaper_count;

        snprintf(ctx->wallpaper_paths[idx], LW_MAX_PATH, "%s", full_path);

        gboolean is_active = (active_filepath && strcmp(full_path, active_filepath) == 0);
        if (is_active) {
            target_active_idx = idx;
        }

        /* ── Card Button Container ── */
        GtkWidget *card_btn = gtk_button_new();
        GtkStyleContext *sc = gtk_widget_get_style_context(card_btn);
        gtk_style_context_add_class(sc, "wallpaper-card");
        gtk_widget_set_size_request(card_btn, THUMB_WIDTH + 16, THUMB_HEIGHT + 48);

        g_object_set_data(G_OBJECT(card_btn), "idx", GINT_TO_POINTER(idx));
        g_object_set_data_full(G_OBJECT(card_btn), "fname", g_strdup(fname), g_free);
        g_signal_connect(card_btn, "clicked", G_CALLBACK(on_card_clicked), ctx);

        /* VBox inside card */
        GtkWidget *card_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
        gtk_container_add(GTK_CONTAINER(card_btn), card_vbox);

        /* Overlay for thumbnail + checkmark badge */
        GtkWidget *overlay = gtk_overlay_new();
        gtk_box_pack_start(GTK_BOX(card_vbox), overlay, FALSE, FALSE, 0);

        /* Thumbnail extraction */
        GdkPixbuf *thumb = thumbnail_generate(full_path, THUMB_WIDTH, THUMB_HEIGHT);
        GtkWidget *thumb_img = NULL;
        if (thumb) {
            thumb_img = gtk_image_new_from_pixbuf(thumb);
            g_object_unref(thumb);
        } else {
            thumb_img = gtk_image_new_from_icon_name("video-x-generic", GTK_ICON_SIZE_DIALOG);
            gtk_widget_set_size_request(thumb_img, THUMB_WIDTH, THUMB_HEIGHT);
        }
        gtk_container_add(GTK_CONTAINER(overlay), thumb_img);

        /* Green Checkmark Badge overlay */
        GtkWidget *badge_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 3);
        GtkStyleContext *badge_sc = gtk_widget_get_style_context(badge_box);
        gtk_style_context_add_class(badge_sc, "active-badge");

        GtkWidget *tick_icon = gtk_image_new_from_icon_name("emblem-ok-symbolic", GTK_ICON_SIZE_MENU);
        gtk_box_pack_start(GTK_BOX(badge_box), tick_icon, FALSE, FALSE, 0);

        GtkWidget *badge_lbl = gtk_label_new("Active");
        gtk_box_pack_start(GTK_BOX(badge_box), badge_lbl, FALSE, FALSE, 0);

        gtk_widget_set_halign(badge_box, GTK_ALIGN_END);
        gtk_widget_set_valign(badge_box, GTK_ALIGN_START);
        gtk_widget_set_margin_top(badge_box, 6);
        gtk_widget_set_margin_end(badge_box, 6);
        gtk_overlay_add_overlay(GTK_OVERLAY(overlay), badge_box);
        gtk_widget_set_visible(badge_box, is_active);

        /* Filename title label */
        GtkWidget *title_lbl = gtk_label_new(fname);
        GtkStyleContext *title_sc = gtk_widget_get_style_context(title_lbl);
        gtk_style_context_add_class(title_sc, "card-title");
        gtk_label_set_ellipsize(GTK_LABEL(title_lbl), PANGO_ELLIPSIZE_MIDDLE);
        gtk_label_set_max_width_chars(GTK_LABEL(title_lbl), 22);
        gtk_box_pack_start(GTK_BOX(card_vbox), title_lbl, FALSE, FALSE, 0);

        /* Save references */
        ctx->card_widgets[idx]  = card_btn;
        ctx->badge_widgets[idx] = badge_box;
        ctx->title_widgets[idx] = title_lbl;

        /* Insert into FlowBox */
        gtk_flow_box_insert(GTK_FLOW_BOX(ctx->flow_box), card_btn, -1);

        ctx->wallpaper_count++;
        g_free(full_path);
    }

    g_list_free_full(filenames, g_free);

    gtk_widget_show_all(ctx->flow_box);

    if (target_active_idx >= 0) {
        update_card_visuals(ctx, target_active_idx);
    }
}

/* ─────────────────────────────────────────────────────────────────────────────
 * Render Timer Callback (~60fps)
 * ──────────────────────────────────────────────────────────────────────────── */
static gboolean on_render_tick(gpointer user_data)
{
    GuiCtx *ctx = (GuiCtx *)user_data;

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
 * Top / Bottom Action Buttons
 * ──────────────────────────────────────────────────────────────────────────── */
static void on_choose_folder_clicked(GtkButton *button, gpointer user_data)
{
    (void)button;
    GuiCtx *ctx = (GuiCtx *)user_data;

    GtkWidget *dialog = gtk_file_chooser_dialog_new(
        "Select Wallpapers Folder",
        GTK_WINDOW(ctx->window),
        GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER,
        "_Cancel", GTK_RESPONSE_CANCEL,
        "_Select Folder", GTK_RESPONSE_ACCEPT,
        NULL);

    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
        char *folder = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));
        if (folder) {
            populate_folder_wallpapers(ctx, folder, NULL);
            if (ctx->wallpaper_count == 0) {
                gui_set_status(ctx, "No video files found in selected folder.");
            } else {
                gui_set_status(ctx, "Folder loaded. Click any wallpaper card to set it as background.");
            }
            g_free(folder);
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
        gtk_button_set_image(button, gtk_image_new_from_icon_name("media-playback-pause-symbolic", GTK_ICON_SIZE_BUTTON));
        gui_set_status(ctx, "Playback resumed.");
    } else {
        decoder_pause(ctx->state);
        gtk_button_set_label(button, "Resume");
        gtk_button_set_image(button, gtk_image_new_from_icon_name("media-playback-start-symbolic", GTK_ICON_SIZE_BUTTON));
        gui_set_status(ctx, "Playback paused.");
    }
}

static void on_stop_clicked(GtkButton *button, gpointer user_data)
{
    (void)button;
    GuiCtx *ctx = (GuiCtx *)user_data;

    if (!atomic_load(&ctx->state->playing)) return;

    if (confirm_turn_off_wallpaper(GTK_WINDOW(ctx->window))) {
        stop_wallpaper_playback(ctx);
    }
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
    ctx->wallpaper_count = 0;
    ctx->active_idx = -1;

    /* Apply custom modern CSS styling */
    GtkCssProvider *provider = gtk_css_provider_new();
    gtk_css_provider_load_from_data(provider, CUSTOM_CSS, -1, NULL);
    gtk_style_context_add_provider_for_screen(
        gdk_screen_get_default(),
        GTK_STYLE_PROVIDER(provider),
        GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_unref(provider);

    /* Main Window */
    ctx->window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(ctx->window), "Live Wallpaper Studio");
    gtk_window_set_default_size(GTK_WINDOW(ctx->window), 680, 520);
    gtk_window_set_position(GTK_WINDOW(ctx->window), GTK_WIN_POS_CENTER);

    GtkStyleContext *win_sc = gtk_widget_get_style_context(ctx->window);
    gtk_style_context_add_class(win_sc, "main-window");

    g_signal_connect(ctx->window, "delete-event", G_CALLBACK(on_window_delete_event), ctx);

    /* Main Layout */
    GtkWidget *root_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_container_add(GTK_CONTAINER(ctx->window), root_vbox);

    /* ── Top Header Bar ── */
    GtkWidget *top_header = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    GtkStyleContext *hdr_sc = gtk_widget_get_style_context(top_header);
    gtk_style_context_add_class(hdr_sc, "top-header");
    gtk_box_pack_start(GTK_BOX(root_vbox), top_header, FALSE, FALSE, 0);

    /* Title + Folder path */
    GtkWidget *header_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    gtk_box_pack_start(GTK_BOX(top_header), header_vbox, TRUE, TRUE, 0);

    GtkWidget *title_lbl = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(title_lbl), "<b><big>Live Wallpaper Studio</big></b>");
    gtk_label_set_xalign(GTK_LABEL(title_lbl), 0.0);
    gtk_box_pack_start(GTK_BOX(header_vbox), title_lbl, FALSE, FALSE, 0);

    ctx->folder_label = gtk_label_new("📁 <i>No folder loaded</i>");
    gtk_label_set_use_markup(GTK_LABEL(ctx->folder_label), TRUE);
    gtk_label_set_xalign(GTK_LABEL(ctx->folder_label), 0.0);
    GtkStyleContext *fb_sc = gtk_widget_get_style_context(ctx->folder_label);
    gtk_style_context_add_class(fb_sc, "folder-banner");
    gtk_box_pack_start(GTK_BOX(header_vbox), ctx->folder_label, FALSE, FALSE, 0);

    /* Select Folder Button */
    GtkWidget *choose_folder_btn = gtk_button_new_with_label("Select Folder...");
    gtk_button_set_image(GTK_BUTTON(choose_folder_btn),
                         gtk_image_new_from_icon_name("folder-open-symbolic", GTK_ICON_SIZE_BUTTON));
    gtk_button_set_always_show_image(GTK_BUTTON(choose_folder_btn), TRUE);
    g_signal_connect(choose_folder_btn, "clicked", G_CALLBACK(on_choose_folder_clicked), ctx);
    gtk_box_pack_end(GTK_BOX(top_header), choose_folder_btn, FALSE, FALSE, 0);

    /* ── Center Grid Gallery (Scrollable GtkFlowBox) ── */
    ctx->scrolled_window = gtk_scrolled_window_new(NULL, NULL);
    GtkStyleContext *scr_sc = gtk_widget_get_style_context(ctx->scrolled_window);
    gtk_style_context_add_class(scr_sc, "gallery-scroll");
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(ctx->scrolled_window),
                                   GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_box_pack_start(GTK_BOX(root_vbox), ctx->scrolled_window, TRUE, TRUE, 0);

    ctx->flow_box = gtk_flow_box_new();
    gtk_widget_set_valign(ctx->flow_box, GTK_ALIGN_START);
    gtk_flow_box_set_max_children_per_line(GTK_FLOW_BOX(ctx->flow_box), 6);
    gtk_flow_box_set_min_children_per_line(GTK_FLOW_BOX(ctx->flow_box), 2);
    gtk_flow_box_set_selection_mode(GTK_FLOW_BOX(ctx->flow_box), GTK_SELECTION_NONE);
    gtk_flow_box_set_column_spacing(GTK_FLOW_BOX(ctx->flow_box), 10);
    gtk_flow_box_set_row_spacing(GTK_FLOW_BOX(ctx->flow_box), 10);
    gtk_container_set_border_width(GTK_CONTAINER(ctx->flow_box), 12);
    gtk_container_add(GTK_CONTAINER(ctx->scrolled_window), ctx->flow_box);

    /* ── Bottom Control Bar ── */
    GtkWidget *bottom_bar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    GtkStyleContext *btm_sc = gtk_widget_get_style_context(bottom_bar);
    gtk_style_context_add_class(btm_sc, "bottom-bar");
    gtk_box_pack_end(GTK_BOX(root_vbox), bottom_bar, FALSE, FALSE, 0);

    /* Status Label */
    ctx->status_label = gtk_label_new("Click any wallpaper to set as desktop background.");
    gtk_label_set_ellipsize(GTK_LABEL(ctx->status_label), PANGO_ELLIPSIZE_END);
    gtk_label_set_xalign(GTK_LABEL(ctx->status_label), 0.0);
    gtk_box_pack_start(GTK_BOX(bottom_bar), ctx->status_label, TRUE, TRUE, 0);

    /* Pause Button */
    ctx->pause_btn = gtk_button_new_with_label("Pause");
    gtk_button_set_image(GTK_BUTTON(ctx->pause_btn),
                         gtk_image_new_from_icon_name("media-playback-pause-symbolic", GTK_ICON_SIZE_BUTTON));
    gtk_button_set_always_show_image(GTK_BUTTON(ctx->pause_btn), TRUE);
    g_signal_connect(ctx->pause_btn, "clicked", G_CALLBACK(on_pause_toggled), ctx);
    gtk_box_pack_start(GTK_BOX(bottom_bar), ctx->pause_btn, FALSE, FALSE, 0);

    /* Stop Button */
    ctx->stop_btn = gtk_button_new_with_label("Turn Off");
    gtk_button_set_image(GTK_BUTTON(ctx->stop_btn),
                         gtk_image_new_from_icon_name("media-playback-stop-symbolic", GTK_ICON_SIZE_BUTTON));
    gtk_button_set_always_show_image(GTK_BUTTON(ctx->stop_btn), TRUE);
    g_signal_connect(ctx->stop_btn, "clicked", G_CALLBACK(on_stop_clicked), ctx);
    gtk_box_pack_start(GTK_BOX(bottom_bar), ctx->stop_btn, FALSE, FALSE, 0);

    /* Quit Button */
    GtkWidget *quit_btn = gtk_button_new_with_label("Quit");
    gtk_button_set_image(GTK_BUTTON(quit_btn),
                         gtk_image_new_from_icon_name("application-exit-symbolic", GTK_ICON_SIZE_BUTTON));
    gtk_button_set_always_show_image(GTK_BUTTON(quit_btn), TRUE);
    g_signal_connect(quit_btn, "clicked", G_CALLBACK(on_quit_clicked), ctx);
    gtk_box_pack_start(GTK_BOX(bottom_bar), quit_btn, FALSE, FALSE, 0);

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

void gui_load_video_and_scan_folder(GuiCtx *ctx, const char *filepath)
{
    if (!ctx || !filepath || !filepath[0]) return;
    char *dirname = g_path_get_dirname(filepath);
    start_wallpaper_playback(ctx, filepath);
    populate_folder_wallpapers(ctx, dirname, filepath);
    g_free(dirname);
}
