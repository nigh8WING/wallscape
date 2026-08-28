/*
 * gui.c — Modern Dual-Tab Wallpaper Studio (Live & Static Wallpapers).
 *
 * Features:
 *   - Left-hand sidebar navigation tabs:
 *       1. 🎬 Live Wallpapers (videos: .mp4, .mkv, .webm, .avi, .mov)
 *       2. 🖼️ Static Wallpapers (images: .jpg, .png, .webp, .svg, .bmp, .gif)
 *   - Compact 130x75 thumbnail grid (GtkFlowBox) with fast thumbnail generation
 *   - Active wallpaper indicator with green checkmark badge (✔ Active)
 *   - Confirmation modals before turning ON or turning OFF wallpapers
 *   - Native GNOME GSettings integration for static backgrounds
 *   - Zero memory leaks: strict g_free, g_object_unref, and cleanup callbacks
 *   - Custom polished GTK3 CSS styling and SVG branding
 */

/* GtkStatusIcon and gtk_menu_popup are deprecated in GTK 3.14+ in favour of
 * libayatana-appindicator, but are still fully functional in GTK3 and are
 * the zero-dependency option for Zorin OS.  Suppress the deprecation warnings
 * so the build output stays clean. */
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"

#include "gui.h"
#include "decoder.h"
#include "config.h"
#include "thumbnail.h"
#include "static_wallpaper.h"
#include "updater.h"

#include <gtk/gtk.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#define MAX_WALLPAPERS 256

/* Custom CSS Stylesheet */
static const char *STUDIO_CSS =
"window.studio-window {"
"    background-color: @theme_bg_color;"
"}"
".sidebar {"
"    background-color: alpha(@theme_base_color, 0.4);"
"    border-right: 1px solid alpha(@theme_fg_color, 0.12);"
"    padding: 12px 8px;"
"}"
".sidebar-brand {"
"    padding: 6px 8px 16px 8px;"
"    border-bottom: 1px solid alpha(@theme_fg_color, 0.10);"
"    margin-bottom: 10px;"
"}"
".sidebar-title {"
"    font-weight: 700;"
"    font-size: 13px;"
"}"
".sidebar-subtitle {"
"    font-size: 10px;"
"    opacity: 0.65;"
"}"
".sidebar-version {"
"    font-size: 10px;"
"    opacity: 0.50;"
"    margin-bottom: 2px;"
"}"
".update-btn {"
"    border-radius: 6px;"
"    padding: 5px 8px;"
"    font-size: 11px;"
"    background-color: alpha(#3584e4, 0.12);"
"    border: 1px solid alpha(#3584e4, 0.35);"
"    color: alpha(@theme_fg_color, 0.85);"
"    transition: all 120ms ease-in-out;"
"}"
".update-btn:hover {"
"    background-color: alpha(#3584e4, 0.25);"
"    border-color: #3584e4;"
"}"
".update-btn.available {"
"    background-color: #2ec27e;"
"    color: white;"
"    font-weight: bold;"
"    border-color: #26a269;"
"}"
".update-btn.available:hover {"
"    background-color: #26a269;"
"}"
".nav-btn {"
"    border-radius: 8px;"
"    padding: 8px 12px;"
"    margin-bottom: 4px;"
"    font-weight: 500;"
"    font-size: 12px;"
"    border: 1px solid transparent;"
"    background: transparent;"
"    transition: all 120ms ease-in-out;"
"}"
".nav-btn:hover {"
"    background-color: alpha(#3584e4, 0.12);"
"}"
".nav-btn.active {"
"    background-color: alpha(#3584e4, 0.22);"
"    border-color: alpha(#3584e4, 0.5);"
"    color: #3584e4;"
"    font-weight: 700;"
"}"
".header-bar {"
"    padding: 10px 16px;"
"    background-color: alpha(@theme_bg_color, 0.9);"
"    border-bottom: 1px solid alpha(@theme_fg_color, 0.10);"
"}"
".gallery-scroll {"
"    background-color: alpha(@theme_base_color, 0.2);"
"}"
".wallpaper-card {"
"    background-color: alpha(@theme_base_color, 0.85);"
"    border-radius: 8px;"
"    border: 1px solid alpha(@theme_fg_color, 0.12);"
"    padding: 6px;"
"    margin: 4px;"
"    transition: all 120ms ease-in-out;"
"}"
".wallpaper-card:hover {"
"    border-color: #3584e4;"
"    background-color: alpha(#3584e4, 0.08);"
"}"
".wallpaper-card.active {"
"    border: 2px solid #2ec27e;"
"    background-color: alpha(#2ec27e, 0.12);"
"}"
".card-title {"
"    font-size: 10px;"
"    font-weight: 500;"
"    margin-top: 4px;"
"}"
".active-badge {"
"    background-color: #2ec27e;"
"    color: white;"
"    font-weight: bold;"
"    font-size: 9px;"
"    border-radius: 8px;"
"    padding: 1px 5px;"
"}"
".footer-bar {"
"    padding: 10px 16px;"
"    background-color: alpha(@theme_bg_color, 0.95);"
"    border-top: 1px solid alpha(@theme_fg_color, 0.10);"
"}"
".empty-state {"
"    padding: 40px;"
"}"
".empty-state-icon {"
"    opacity: 0.25;"
"}"
".empty-state-title {"
"    font-size: 18px;"
"    font-weight: 700;"
"    margin-top: 16px;"
"    opacity: 0.7;"
"}"
".empty-state-subtitle {"
"    font-size: 13px;"
"    margin-top: 6px;"
"    opacity: 0.45;"
"}"
".folder-card {"
"    background-color: alpha(@theme_base_color, 0.85);"
"    border-radius: 10px;"
"    border: 1px solid alpha(@theme_fg_color, 0.12);"
"    padding: 10px 8px;"
"    margin: 4px;"
"    transition: all 120ms ease-in-out;"
"}"
".folder-card:hover {"
"    border-color: #3584e4;"
"    background-color: alpha(#3584e4, 0.08);"
"}"
".folder-card.active {"
"    border: 2px solid #2ec27e;"
"    background-color: alpha(#2ec27e, 0.10);"
"}"
".folder-card.add-card {"
"    border: 1.5px dashed alpha(@theme_fg_color, 0.28);"
"    background-color: alpha(@theme_base_color, 0.35);"
"}"
".folder-card.add-card:hover {"
"    border-color: #3584e4;"
"    background-color: alpha(#3584e4, 0.12);"
"}"
".folder-card-title {"
"    font-size: 11px;"
"    font-weight: 600;"
"    margin-top: 4px;"
"}"
".folder-card-sub {"
"    font-size: 9px;"
"    opacity: 0.60;"
"}"
".btn-delete-folder {"
"    border-radius: 12px;"
"    padding: 1px 3px;"
"    background-color: alpha(@theme_base_color, 0.7);"
"    border: 1px solid alpha(@theme_fg_color, 0.15);"
"    opacity: 0.75;"
"    transition: all 100ms ease;"
"}"
".btn-delete-folder:hover {"
"    background-color: #e01b24;"
"    color: white;"
"    opacity: 1.0;"
"}"
".back-btn {"
"    border-radius: 6px;"
"    padding: 4px 8px;"
"    font-size: 11px;"
"    font-weight: 600;"
"}";

#define MAX_FOLDERS 32

/* Structure to manage a multi-folder grid view (Live or Static) */
typedef struct {
    bool       is_video;
    char       folders[MAX_FOLDERS][LW_MAX_PATH];
    int        folder_count;
    int        current_folder_idx; /* -1 = viewing folder list, >=0 = viewing items inside folder */

    GtkWidget *back_btn;
    GtkWidget *folder_label;
    GtkWidget *add_folder_btn;

    GtkWidget *page_stack;        /* Switches between empty_state, folders, and gallery */
    GtkWidget *empty_state;       /* Shown when no folder is loaded */

    /* Folders grid (root album view) */
    GtkWidget *folders_flow_box;

    /* Items gallery grid (inside folder view) */
    GtkWidget *gallery_flow_box;
    char       items[MAX_WALLPAPERS][LW_MAX_PATH];
    GtkWidget *card_widgets[MAX_WALLPAPERS];
    GtkWidget *badge_widgets[MAX_WALLPAPERS];
    GtkWidget *title_widgets[MAX_WALLPAPERS];
    int        count;
    int        active_idx;
} GridView;

struct GuiCtx {
    AppState     *state;
    WallpaperCtx *wallpaper;

    GtkWidget    *window;
    GtkWidget    *stack;
    GtkWidget    *nav_live_btn;
    GtkWidget    *nav_static_btn;
    GtkWidget    *update_btn;
    GtkWidget    *status_label;
    GtkWidget    *pause_btn;
    GtkWidget    *stop_btn;

    /* System tray icon */
    GtkStatusIcon *tray_icon;

    /* Live video grid */
    GridView      live_grid;

    /* Static image grid */
    GridView      static_grid;
    char          active_static_path[LW_MAX_PATH];
    GSettings    *bg_settings;

    /* Update status */
    UpdateInfo    last_update_info;

    guint         render_timer_id;
    double        render_fps;       /* current render timer target FPS */
};

/* Forward declarations */
static gboolean on_render_tick(gpointer user_data);
static void on_back_btn_clicked(GtkButton *button, gpointer user_data);
static void on_add_folder_btn_clicked(GtkButton *button, gpointer user_data);
static void on_folder_card_clicked(GtkButton *button, gpointer user_data);
static void on_remove_folder_clicked(GtkWidget *button, gpointer user_data);
static void on_live_card_clicked(GtkButton *button, gpointer user_data);
static void on_static_card_clicked(GtkButton *button, gpointer user_data);
static void on_nav_tab_clicked(GtkButton *button, gpointer user_data);
static void on_pause_toggled(GtkButton *button, gpointer user_data);
static void on_stop_clicked(GtkButton *button, gpointer user_data);
static void on_quit_clicked(GtkButton *button, gpointer user_data);
static void on_update_btn_clicked(GtkButton *button, gpointer user_data);
static gboolean on_window_delete_event(GtkWidget *widget, GdkEvent *event, gpointer user_data);
static void on_gnome_bg_changed(GSettings *settings, const gchar *key, gpointer user_data);

static int scan_folder_item_count(const char *folder, bool is_video);
static bool is_folder_active(GuiCtx *ctx, GridView *grid, const char *folder);
static void show_folders_view(GuiCtx *ctx, GridView *grid);
static void open_folder_view(GuiCtx *ctx, GridView *grid, int folder_idx);

static void populate_live_grid(GuiCtx *ctx, const char *folder, const char *active_file);
static void populate_static_grid(GuiCtx *ctx, const char *folder, const char *active_file);
static void update_grid_visuals(GridView *grid, int active_idx);
static void start_live_wallpaper(GuiCtx *ctx, const char *filepath);
static void stop_live_wallpaper(GuiCtx *ctx);
static void apply_static_wallpaper(GuiCtx *ctx, const char *filepath);
static void gui_start_render_timer_at_fps(GuiCtx *ctx, double fps);

/* Tray icon callbacks */
static void on_tray_activate(GtkStatusIcon *icon, gpointer user_data);
static void on_tray_popup_menu(GtkStatusIcon *icon, guint button,
                               guint activate_time, gpointer user_data);

/* ─────────────────────────────────────────────────────────────────────────────
 * Async Thumbnail Loader
 *
 * Thumbnails are generated lazily via g_idle_add() so grid population never
 * blocks the GTK main thread.  Each card initially shows a generic icon;
 * the idle callback replaces it with the real thumbnail when the CPU is idle.
 * ──────────────────────────────────────────────────────────────────────────── */
typedef struct {
    char        filepath[LW_MAX_PATH];
    int         target_w;
    int         target_h;
    GtkWidget  *image_widget; /* the GtkImage inside the card to update */
    bool        is_video;
} ThumbLoadCtx;

static gboolean thumb_load_idle(gpointer user_data)
{
    ThumbLoadCtx *tctx = (ThumbLoadCtx *)user_data;

    /* Widget might have been destroyed (e.g. user changed folder quickly) */
    if (!GTK_IS_IMAGE(tctx->image_widget)) {
        free(tctx);
        return G_SOURCE_REMOVE;
    }

    GdkPixbuf *thumb = thumbnail_generate(tctx->filepath,
                                          tctx->target_w, tctx->target_h);
    if (thumb) {
        gtk_image_set_from_pixbuf(GTK_IMAGE(tctx->image_widget), thumb);
        g_object_unref(thumb);
    }
    free(tctx);
    return G_SOURCE_REMOVE;
}

/* Schedule an asynchronous thumbnail load for an image widget. */
static void schedule_thumb_load(GtkWidget *image_widget, const char *filepath,
                                int target_w, int target_h)
{
    ThumbLoadCtx *tctx = (ThumbLoadCtx *)malloc(sizeof(ThumbLoadCtx));
    if (!tctx) return;
    snprintf(tctx->filepath, LW_MAX_PATH, "%s", filepath);
    tctx->target_w     = target_w;
    tctx->target_h     = target_h;
    tctx->image_widget = image_widget;
    g_idle_add(thumb_load_idle, tctx);
}

/* ─────────────────────────────────────────────────────────────────────────────
 * Confirmation Dialogs
 * ──────────────────────────────────────────────────────────────────────────── */
static gboolean confirm_action(GtkWindow *parent, const char *title, const char *msg, const char *btn_text)
{
    GtkWidget *dialog = gtk_message_dialog_new(
        parent,
        GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
        GTK_MESSAGE_QUESTION,
        GTK_BUTTONS_CANCEL,
        "%s", title);

    gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(dialog), "%s", msg);
    gtk_dialog_add_button(GTK_DIALOG(dialog), btn_text, GTK_RESPONSE_ACCEPT);
    gtk_dialog_set_default_response(GTK_DIALOG(dialog), GTK_RESPONSE_ACCEPT);

    gint res = gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);
    return (res == GTK_RESPONSE_ACCEPT);
}

/* ─────────────────────────────────────────────────────────────────────────────
 * Empty State Widget Factory
 * ──────────────────────────────────────────────────────────────────────────── */
static GtkWidget *create_empty_state(const char *icon_name,
                                     const char *title,
                                     const char *subtitle)
{
    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_style_context_add_class(gtk_widget_get_style_context(vbox), "empty-state");
    gtk_widget_set_halign(vbox, GTK_ALIGN_CENTER);
    gtk_widget_set_valign(vbox, GTK_ALIGN_CENTER);

    GtkWidget *icon = gtk_image_new_from_icon_name(icon_name, GTK_ICON_SIZE_INVALID);
    gtk_image_set_pixel_size(GTK_IMAGE(icon), 72);
    gtk_style_context_add_class(gtk_widget_get_style_context(icon), "empty-state-icon");
    gtk_box_pack_start(GTK_BOX(vbox), icon, FALSE, FALSE, 0);

    GtkWidget *lbl_title = gtk_label_new(title);
    gtk_style_context_add_class(gtk_widget_get_style_context(lbl_title), "empty-state-title");
    gtk_box_pack_start(GTK_BOX(vbox), lbl_title, FALSE, FALSE, 0);

    GtkWidget *lbl_sub = gtk_label_new(subtitle);
    gtk_style_context_add_class(gtk_widget_get_style_context(lbl_sub), "empty-state-subtitle");
    gtk_label_set_justify(GTK_LABEL(lbl_sub), GTK_JUSTIFY_CENTER);
    gtk_label_set_line_wrap(GTK_LABEL(lbl_sub), TRUE);
    gtk_box_pack_start(GTK_BOX(vbox), lbl_sub, FALSE, FALSE, 0);

    return vbox;
}

/* ─────────────────────────────────────────────────────────────────────────────
 * Live Wallpaper Controls
 * ──────────────────────────────────────────────────────────────────────────── */
static void start_live_wallpaper(GuiCtx *ctx, const char *filepath)
{
    if (!filepath || !filepath[0]) return;

    gui_stop_render_timer(ctx);
    decoder_stop(ctx->state);

    if (decoder_start(ctx->state, filepath) != 0) {
        gui_set_status(ctx, "Error: Could not open video file.");
        return;
    }

    /* wallpaper_render_frame() auto-sizes the texture on the very first frame,
     * so we can show the window and start the render timer immediately without
     * polling for video_width/height (which would block the GTK main thread). */
    wallpaper_show(ctx->wallpaper);
    gui_start_render_timer(ctx);

    config_save(filepath);
    config_save_static_path("");

    /* Clear static active state */
    ctx->active_static_path[0] = '\0';
    update_grid_visuals(&ctx->static_grid, -1);

    if (ctx->pause_btn) {
        gtk_button_set_label(GTK_BUTTON(ctx->pause_btn), "Pause");
        gtk_button_set_image(GTK_BUTTON(ctx->pause_btn),
                             gtk_image_new_from_icon_name("media-playback-pause-symbolic", GTK_ICON_SIZE_BUTTON));
    }

    char *base = g_path_get_basename(filepath);
    gui_set_status(ctx, "🎬 Loading live wallpaper…");
    g_free(base);
}

static void stop_live_wallpaper(GuiCtx *ctx)
{
    gui_stop_render_timer(ctx);
    decoder_stop(ctx->state);
    atomic_store(&ctx->state->playing, false);
    ctx->state->video_path[0] = '\0';
    config_save("");
    wallpaper_hide(ctx->wallpaper);

    update_grid_visuals(&ctx->live_grid, -1);

    if (ctx->pause_btn) {
        gtk_button_set_label(GTK_BUTTON(ctx->pause_btn), "Pause");
        gtk_button_set_image(GTK_BUTTON(ctx->pause_btn),
                             gtk_image_new_from_icon_name("media-playback-pause-symbolic", GTK_ICON_SIZE_BUTTON));
    }
    gui_set_status(ctx, "Live wallpaper turned off.");
}

/* ─────────────────────────────────────────────────────────────────────────────
 * Static Wallpaper Controls (GNOME GSettings)
 * ──────────────────────────────────────────────────────────────────────────── */
static void apply_static_wallpaper(GuiCtx *ctx, const char *filepath)
{
    if (!filepath || !filepath[0]) return;

    /* Stop the video decoder and hide the surface directly — without going
     * through the confirmation-dialog path of stop_live_wallpaper(), since
     * the user already confirmed the static wallpaper action. (UX-3) */
    gui_stop_render_timer(ctx);
    decoder_stop(ctx->state);
    atomic_store(&ctx->state->playing, false);
    ctx->state->video_path[0] = '\0';
    config_save("");
    wallpaper_hide(ctx->wallpaper);
    update_grid_visuals(&ctx->live_grid, -1);

    if (ctx->pause_btn) {
        gtk_button_set_label(GTK_BUTTON(ctx->pause_btn), "Pause");
        gtk_button_set_image(GTK_BUTTON(ctx->pause_btn),
                             gtk_image_new_from_icon_name("media-playback-pause-symbolic", GTK_ICON_SIZE_BUTTON));
    }

    if (static_wallpaper_apply(filepath)) {
        snprintf(ctx->active_static_path, sizeof(ctx->active_static_path), "%s", filepath);
        config_save_static_path(filepath);

        char *base = g_path_get_basename(filepath);
        char buf[512];
        snprintf(buf, sizeof(buf), "🖼️ Static Wallpaper: %s (Applied to GNOME)", base);
        gui_set_status(ctx, buf);
        g_free(base);
    } else {
        gui_set_status(ctx, "Error: Failed to apply static wallpaper.");
    }
}

/* ─────────────────────────────────────────────────────────────────────────────
 * Visual Active Indicator & Card Classes
 * ──────────────────────────────────────────────────────────────────────────── */
static void update_grid_visuals(GridView *grid, int active_idx)
{
    grid->active_idx = active_idx;

    for (int i = 0; i < grid->count; i++) {
        GtkWidget *card  = grid->card_widgets[i];
        GtkWidget *badge = grid->badge_widgets[i];
        GtkWidget *title = grid->title_widgets[i];
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
 * Card Click Handlers
 * ──────────────────────────────────────────────────────────────────────────── */
static void on_live_card_clicked(GtkButton *button, gpointer user_data)
{
    GuiCtx *ctx = (GuiCtx *)user_data;
    int idx = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(button), "idx"));
    if (idx < 0 || idx >= ctx->live_grid.count) return;

    const char *target = ctx->live_grid.items[idx];
    char *base = g_path_get_basename(target);

    if (idx == ctx->live_grid.active_idx && atomic_load(&ctx->state->playing)) {
        if (confirm_action(GTK_WINDOW(ctx->window), "Turn Off Live Wallpaper?",
                           "Stop wallpaper playback and restore desktop background?", "Turn Off")) {
            stop_live_wallpaper(ctx);
        }
    } else {
        char msg[512];
        snprintf(msg, sizeof(msg), "Set \"%s\" as your live desktop wallpaper?", base);
        if (confirm_action(GTK_WINDOW(ctx->window), "Apply Live Wallpaper", msg, "Apply Wallpaper")) {
            start_live_wallpaper(ctx, target);
            update_grid_visuals(&ctx->live_grid, idx);
        }
    }
    g_free(base);
}

static void on_static_card_clicked(GtkButton *button, gpointer user_data)
{
    GuiCtx *ctx = (GuiCtx *)user_data;
    int idx = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(button), "idx"));
    if (idx < 0 || idx >= ctx->static_grid.count) return;

    const char *target = ctx->static_grid.items[idx];
    char *base = g_path_get_basename(target);

    if (idx == ctx->static_grid.active_idx) {
        if (confirm_action(GTK_WINDOW(ctx->window), "Clear Wallpaper?",
                           "Reset desktop background?", "Clear")) {
            ctx->active_static_path[0] = '\0';
            config_save_static_path("");
            update_grid_visuals(&ctx->static_grid, -1);
            static_wallpaper_clear();
            gui_set_status(ctx, "Static wallpaper cleared.");
        }
    } else {
        char msg[512];
        snprintf(msg, sizeof(msg), "Apply \"%s\" as your desktop background?", base);
        if (confirm_action(GTK_WINDOW(ctx->window), "Set Desktop Background", msg, "Set Background")) {
            apply_static_wallpaper(ctx, target);
            update_grid_visuals(&ctx->static_grid, idx);
        }
    }
    g_free(base);
}

/* ─────────────────────────────────────────────────────────────────────────────
 * Multi-Folder Scanning & Navigation Helpers
 * ──────────────────────────────────────────────────────────────────────────── */

static int scan_folder_item_count(const char *folder, bool is_video)
{
    if (!folder || !folder[0]) return 0;
    GDir *dir = g_dir_open(folder, 0, NULL);
    if (!dir) return 0;

    int total = 0;
    const char *name = NULL;
    const char *exts[] = { ".mp4", ".mkv", ".webm", ".avi", ".mov", NULL };

    while ((name = g_dir_read_name(dir)) != NULL) {
        if (is_video) {
            size_t nlen = strlen(name);
            for (int i = 0; exts[i] != NULL; i++) {
                size_t elen = strlen(exts[i]);
                if (nlen >= elen && strcasecmp(name + (nlen - elen), exts[i]) == 0) {
                    total++;
                    break;
                }
            }
        } else {
            if (static_wallpaper_is_supported(name)) {
                total++;
            }
        }
    }
    g_dir_close(dir);
    return total;
}

static bool is_folder_active(GuiCtx *ctx, GridView *grid, const char *folder)
{
    if (!folder || !folder[0]) return false;
    size_t flen = strlen(folder);
    if (grid->is_video) {
        if (!atomic_load(&ctx->state->playing) || ctx->state->video_path[0] == '\0') return false;
        return (strncmp(ctx->state->video_path, folder, flen) == 0);
    } else {
        if (ctx->active_static_path[0] == '\0') return false;
        return (strncmp(ctx->active_static_path, folder, flen) == 0);
    }
}

static void show_folders_view(GuiCtx *ctx, GridView *grid)
{
    grid->current_folder_idx = -1;
    if (grid->back_btn) {
        gtk_widget_set_visible(grid->back_btn, FALSE);
    }

    if (grid->folder_count == 0) {
        char hdr[256];
        snprintf(hdr, sizeof(hdr), "📁 <b>No %s Folders Loaded</b>", grid->is_video ? "Video" : "Image");
        gtk_label_set_markup(GTK_LABEL(grid->folder_label), hdr);
        gtk_widget_set_tooltip_text(grid->folder_label, "Click 'Add Folder...' to import a folder.");
        if (grid->page_stack) {
            gtk_stack_set_visible_child_name(GTK_STACK(grid->page_stack), "empty");
        }
        return;
    }

    char hdr[256];
    snprintf(hdr, sizeof(hdr), "📁 <b>%s Collections</b>  <span alpha='60%%'>(%d %s)</span>",
             grid->is_video ? "Live Video" : "Static Image",
             grid->folder_count,
             grid->folder_count == 1 ? "folder" : "folders");
    gtk_label_set_markup(GTK_LABEL(grid->folder_label), hdr);
    gtk_widget_set_tooltip_text(grid->folder_label, "Select a folder to browse wallpapers.");

    /* Clear folders flow box */
    GList *children = gtk_container_get_children(GTK_CONTAINER(grid->folders_flow_box));
    for (GList *l = children; l != NULL; l = l->next) {
        gtk_widget_destroy(GTK_WIDGET(l->data));
    }
    g_list_free(children);

    for (int i = 0; i < grid->folder_count; i++) {
        const char *fpath = grid->folders[i];
        int num_items = scan_folder_item_count(fpath, grid->is_video);
        bool active = is_folder_active(ctx, grid, fpath);
        char *base = g_path_get_basename(fpath);

        GtkWidget *card = gtk_button_new();
        GtkStyleContext *sc = gtk_widget_get_style_context(card);
        gtk_style_context_add_class(sc, "folder-card");
        if (active) {
            gtk_style_context_add_class(sc, "active");
        }
        gtk_widget_set_size_request(card, 150, 115);
        g_object_set_data(G_OBJECT(card), "grid", grid);
        g_object_set_data(G_OBJECT(card), "fidx", GINT_TO_POINTER(i));
        g_signal_connect(card, "clicked", G_CALLBACK(on_folder_card_clicked), ctx);

        GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
        gtk_container_add(GTK_CONTAINER(card), vbox);

        GtkWidget *overlay = gtk_overlay_new();
        gtk_box_pack_start(GTK_BOX(vbox), overlay, FALSE, FALSE, 0);

        GtkWidget *icon = gtk_image_new_from_icon_name(
            grid->is_video ? "folder-videos-symbolic" : "folder-pictures-symbolic",
            GTK_ICON_SIZE_DIALOG);
        gtk_image_set_pixel_size(GTK_IMAGE(icon), 46);
        gtk_widget_set_size_request(icon, 130, 50);
        gtk_container_add(GTK_CONTAINER(overlay), icon);

        /* Delete button (top-right overlay) */
        GtkWidget *del_btn = gtk_button_new();
        gtk_button_set_image(GTK_BUTTON(del_btn),
            gtk_image_new_from_icon_name("window-close-symbolic", GTK_ICON_SIZE_MENU));
        gtk_style_context_add_class(gtk_widget_get_style_context(del_btn), "btn-delete-folder");
        gtk_widget_set_tooltip_text(del_btn, "Remove this folder from library");
        gtk_widget_set_halign(del_btn, GTK_ALIGN_END);
        gtk_widget_set_valign(del_btn, GTK_ALIGN_START);
        g_object_set_data(G_OBJECT(del_btn), "grid", grid);
        g_object_set_data(G_OBJECT(del_btn), "fidx", GINT_TO_POINTER(i));
        g_signal_connect(del_btn, "clicked", G_CALLBACK(on_remove_folder_clicked), ctx);
        gtk_overlay_add_overlay(GTK_OVERLAY(overlay), del_btn);

        /* Active Badge (top-left overlay) */
        if (active) {
            GtkWidget *badge = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 2);
            gtk_style_context_add_class(gtk_widget_get_style_context(badge), "active-badge");
            gtk_box_pack_start(GTK_BOX(badge),
                gtk_image_new_from_icon_name("emblem-ok-symbolic", GTK_ICON_SIZE_MENU), FALSE, FALSE, 0);
            gtk_box_pack_start(GTK_BOX(badge), gtk_label_new("Active"), FALSE, FALSE, 0);
            gtk_widget_set_halign(badge, GTK_ALIGN_START);
            gtk_widget_set_valign(badge, GTK_ALIGN_START);
            gtk_overlay_add_overlay(GTK_OVERLAY(overlay), badge);
        }

        /* Title */
        GtkWidget *title = gtk_label_new(base);
        gtk_style_context_add_class(gtk_widget_get_style_context(title), "folder-card-title");
        gtk_label_set_ellipsize(GTK_LABEL(title), PANGO_ELLIPSIZE_MIDDLE);
        gtk_label_set_max_width_chars(GTK_LABEL(title), 16);
        gtk_box_pack_start(GTK_BOX(vbox), title, FALSE, FALSE, 0);

        /* Subtitle */
        char sub[64];
        snprintf(sub, sizeof(sub), "%d %s", num_items, grid->is_video ? "videos" : "images");
        GtkWidget *lbl_sub = gtk_label_new(sub);
        gtk_style_context_add_class(gtk_widget_get_style_context(lbl_sub), "folder-card-sub");
        gtk_box_pack_start(GTK_BOX(vbox), lbl_sub, FALSE, FALSE, 0);

        gtk_flow_box_insert(GTK_FLOW_BOX(grid->folders_flow_box), card, -1);
        g_free(base);
    }

    /* Add "+ Add Folder" card */
    GtkWidget *add_card = gtk_button_new();
    GtkStyleContext *sc_add = gtk_widget_get_style_context(add_card);
    gtk_style_context_add_class(sc_add, "folder-card");
    gtk_style_context_add_class(sc_add, "add-card");
    gtk_widget_set_size_request(add_card, 150, 115);
    g_object_set_data(G_OBJECT(add_card), "grid", grid);
    g_signal_connect(add_card, "clicked", G_CALLBACK(on_add_folder_btn_clicked), ctx);

    GtkWidget *add_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_container_add(GTK_CONTAINER(add_card), add_vbox);

    GtkWidget *add_icon = gtk_image_new_from_icon_name("list-add-symbolic", GTK_ICON_SIZE_DIALOG);
    gtk_image_set_pixel_size(GTK_IMAGE(add_icon), 40);
    gtk_widget_set_size_request(add_icon, 130, 50);
    gtk_box_pack_start(GTK_BOX(add_vbox), add_icon, FALSE, FALSE, 0);

    GtkWidget *add_title = gtk_label_new("Add Folder…");
    gtk_style_context_add_class(gtk_widget_get_style_context(add_title), "folder-card-title");
    gtk_box_pack_start(GTK_BOX(add_vbox), add_title, FALSE, FALSE, 0);

    GtkWidget *add_sub = gtk_label_new("Import directory");
    gtk_style_context_add_class(gtk_widget_get_style_context(add_sub), "folder-card-sub");
    gtk_box_pack_start(GTK_BOX(add_vbox), add_sub, FALSE, FALSE, 0);

    gtk_flow_box_insert(GTK_FLOW_BOX(grid->folders_flow_box), add_card, -1);

    gtk_widget_show_all(grid->folders_flow_box);
    if (grid->page_stack) {
        gtk_stack_set_visible_child_name(GTK_STACK(grid->page_stack), "folders");
    }
}

static void open_folder_view(GuiCtx *ctx, GridView *grid, int folder_idx)
{
    if (folder_idx < 0 || folder_idx >= grid->folder_count) return;
    grid->current_folder_idx = folder_idx;

    if (grid->back_btn) {
        gtk_widget_set_visible(grid->back_btn, TRUE);
    }

    const char *folder = grid->folders[folder_idx];

    if (grid->is_video) {
        const char *active = (atomic_load(&ctx->state->playing) && ctx->state->video_path[0])
                             ? ctx->state->video_path : NULL;
        populate_live_grid(ctx, folder, active);
    } else {
        const char *active = (ctx->active_static_path[0] != '\0')
                             ? ctx->active_static_path : NULL;
        populate_static_grid(ctx, folder, active);
    }
}

/* ─────────────────────────────────────────────────────────────────────────────
 * Populate Live Video Grid
 * ──────────────────────────────────────────────────────────────────────────── */
static void populate_live_grid(GuiCtx *ctx, const char *folder, const char *active_file)
{
    if (!folder || !ctx->live_grid.gallery_flow_box) return;

    GList *children = gtk_container_get_children(GTK_CONTAINER(ctx->live_grid.gallery_flow_box));
    for (GList *l = children; l != NULL; l = l->next) {
        gtk_widget_destroy(GTK_WIDGET(l->data));
    }
    g_list_free(children);

    ctx->live_grid.count = 0;
    ctx->live_grid.active_idx = -1;
    memset(ctx->live_grid.card_widgets,  0, sizeof(ctx->live_grid.card_widgets));
    memset(ctx->live_grid.badge_widgets, 0, sizeof(ctx->live_grid.badge_widgets));
    memset(ctx->live_grid.title_widgets, 0, sizeof(ctx->live_grid.title_widgets));

    GDir *dir = g_dir_open(folder, 0, NULL);
    if (!dir) {
        if (ctx->live_grid.page_stack)
            gtk_stack_set_visible_child_name(GTK_STACK(ctx->live_grid.page_stack), "empty");
        return;
    }

    GList *filenames = NULL;
    const char *name = NULL;
    while ((name = g_dir_read_name(dir)) != NULL) {
        const char *exts[] = { ".mp4", ".mkv", ".webm", ".avi", ".mov", NULL };
        size_t nlen = strlen(name);
        for (int i = 0; exts[i] != NULL; i++) {
            size_t elen = strlen(exts[i]);
            if (nlen >= elen && strcasecmp(name + (nlen - elen), exts[i]) == 0) {
                filenames = g_list_prepend(filenames, g_strdup(name));
                break;
            }
        }
    }
    g_dir_close(dir);

    filenames = g_list_sort(filenames, (GCompareFunc)g_strcmp0);

    guint total = g_list_length(filenames);
    char *folder_base = g_path_get_basename(folder);
    char hdr[512];
    snprintf(hdr, sizeof(hdr), "📁 <b>%s</b>  <span alpha='60%%'>(%u videos)</span>", folder_base, total);
    gtk_label_set_markup(GTK_LABEL(ctx->live_grid.folder_label), hdr);
    gtk_widget_set_tooltip_text(ctx->live_grid.folder_label, folder);
    g_free(folder_base);

    if (total == 0) {
        g_list_free_full(filenames, g_free);
        if (ctx->live_grid.page_stack)
            gtk_stack_set_visible_child_name(GTK_STACK(ctx->live_grid.page_stack), "gallery");
        return;
    }

    /* Switch to gallery view */
    if (ctx->live_grid.page_stack)
        gtk_stack_set_visible_child_name(GTK_STACK(ctx->live_grid.page_stack), "gallery");

    int target_active = -1;

    for (GList *l = filenames; l != NULL; l = l->next) {
        if (ctx->live_grid.count >= MAX_WALLPAPERS) break;

        const char *fname = (const char *)l->data;
        char *fpath = g_build_filename(folder, fname, NULL);
        int idx = ctx->live_grid.count;
        snprintf(ctx->live_grid.items[idx], LW_MAX_PATH, "%s", fpath);

        gboolean is_active = (active_file && strcmp(fpath, active_file) == 0);
        if (is_active) target_active = idx;

        /* Compact Card Button */
        GtkWidget *card = gtk_button_new();
        GtkStyleContext *sc = gtk_widget_get_style_context(card);
        gtk_style_context_add_class(sc, "wallpaper-card");
        gtk_widget_set_size_request(card, THUMB_WIDTH + 12, THUMB_HEIGHT + 36);

        g_object_set_data(G_OBJECT(card), "idx", GINT_TO_POINTER(idx));
        g_object_set_data_full(G_OBJECT(card), "fname", g_strdup(fname), g_free);
        g_signal_connect(card, "clicked", G_CALLBACK(on_live_card_clicked), ctx);

        GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
        gtk_container_add(GTK_CONTAINER(card), vbox);

        GtkWidget *overlay = gtk_overlay_new();
        gtk_box_pack_start(GTK_BOX(vbox), overlay, FALSE, FALSE, 0);

        GtkWidget *img = gtk_image_new_from_icon_name("video-x-generic",
                                                       GTK_ICON_SIZE_DIALOG);
        gtk_widget_set_size_request(img, THUMB_WIDTH, THUMB_HEIGHT);
        gtk_container_add(GTK_CONTAINER(overlay), img);

        /* Schedule async thumbnail load */
        schedule_thumb_load(img, fpath, THUMB_WIDTH, THUMB_HEIGHT);

        /* Active Badge */
        GtkWidget *badge = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 2);
        gtk_style_context_add_class(gtk_widget_get_style_context(badge), "active-badge");
        gtk_box_pack_start(GTK_BOX(badge), gtk_image_new_from_icon_name("emblem-ok-symbolic", GTK_ICON_SIZE_MENU), FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(badge), gtk_label_new("Active"), FALSE, FALSE, 0);
        gtk_widget_set_halign(badge, GTK_ALIGN_END);
        gtk_widget_set_valign(badge, GTK_ALIGN_START);
        gtk_widget_set_margin_top(badge, 4);
        gtk_widget_set_margin_end(badge, 4);
        gtk_overlay_add_overlay(GTK_OVERLAY(overlay), badge);
        gtk_widget_set_no_show_all(badge, TRUE);
        gtk_widget_set_visible(badge, is_active);

        /* Title */
        GtkWidget *title = gtk_label_new(fname);
        gtk_style_context_add_class(gtk_widget_get_style_context(title), "card-title");
        gtk_label_set_ellipsize(GTK_LABEL(title), PANGO_ELLIPSIZE_MIDDLE);
        gtk_label_set_max_width_chars(GTK_LABEL(title), 18);
        gtk_box_pack_start(GTK_BOX(vbox), title, FALSE, FALSE, 0);

        ctx->live_grid.card_widgets[idx]  = card;
        ctx->live_grid.badge_widgets[idx] = badge;
        ctx->live_grid.title_widgets[idx] = title;

        gtk_flow_box_insert(GTK_FLOW_BOX(ctx->live_grid.gallery_flow_box), card, -1);
        ctx->live_grid.count++;
        g_free(fpath);
    }

    g_list_free_full(filenames, g_free);
    gtk_widget_show_all(ctx->live_grid.gallery_flow_box);
    update_grid_visuals(&ctx->live_grid, target_active);
}

/* ─────────────────────────────────────────────────────────────────────────────
 * Populate Static Image Grid
 * ──────────────────────────────────────────────────────────────────────────── */
static void populate_static_grid(GuiCtx *ctx, const char *folder, const char *active_file)
{
    if (!folder || !ctx->static_grid.gallery_flow_box) return;

    GList *children = gtk_container_get_children(GTK_CONTAINER(ctx->static_grid.gallery_flow_box));
    for (GList *l = children; l != NULL; l = l->next) {
        gtk_widget_destroy(GTK_WIDGET(l->data));
    }
    g_list_free(children);

    ctx->static_grid.count = 0;
    ctx->static_grid.active_idx = -1;
    memset(ctx->static_grid.card_widgets,  0, sizeof(ctx->static_grid.card_widgets));
    memset(ctx->static_grid.badge_widgets, 0, sizeof(ctx->static_grid.badge_widgets));
    memset(ctx->static_grid.title_widgets, 0, sizeof(ctx->static_grid.title_widgets));

    GDir *dir = g_dir_open(folder, 0, NULL);
    if (!dir) {
        if (ctx->static_grid.page_stack)
            gtk_stack_set_visible_child_name(GTK_STACK(ctx->static_grid.page_stack), "empty");
        return;
    }

    GList *filenames = NULL;
    const char *name = NULL;
    while ((name = g_dir_read_name(dir)) != NULL) {
        if (static_wallpaper_is_supported(name)) {
            filenames = g_list_prepend(filenames, g_strdup(name));
        }
    }
    g_dir_close(dir);

    filenames = g_list_sort(filenames, (GCompareFunc)g_strcmp0);

    guint total = g_list_length(filenames);
    char *folder_base = g_path_get_basename(folder);
    char hdr[512];
    snprintf(hdr, sizeof(hdr), "📁 <b>%s</b>  <span alpha='60%%'>(%u images)</span>", folder_base, total);
    gtk_label_set_markup(GTK_LABEL(ctx->static_grid.folder_label), hdr);
    gtk_widget_set_tooltip_text(ctx->static_grid.folder_label, folder);
    g_free(folder_base);

    if (total == 0) {
        g_list_free_full(filenames, g_free);
        if (ctx->static_grid.page_stack)
            gtk_stack_set_visible_child_name(GTK_STACK(ctx->static_grid.page_stack), "gallery");
        return;
    }

    /* Switch to gallery view */
    if (ctx->static_grid.page_stack)
        gtk_stack_set_visible_child_name(GTK_STACK(ctx->static_grid.page_stack), "gallery");

    int target_active = -1;

    for (GList *l = filenames; l != NULL; l = l->next) {
        if (ctx->static_grid.count >= MAX_WALLPAPERS) break;

        const char *fname = (const char *)l->data;
        char *fpath = g_build_filename(folder, fname, NULL);
        int idx = ctx->static_grid.count;
        snprintf(ctx->static_grid.items[idx], LW_MAX_PATH, "%s", fpath);

        gboolean is_active = (active_file && strcmp(fpath, active_file) == 0);
        if (is_active) target_active = idx;

        /* Compact Card Button */
        GtkWidget *card = gtk_button_new();
        GtkStyleContext *sc = gtk_widget_get_style_context(card);
        gtk_style_context_add_class(sc, "wallpaper-card");
        gtk_widget_set_size_request(card, THUMB_WIDTH + 12, THUMB_HEIGHT + 36);

        g_object_set_data(G_OBJECT(card), "idx", GINT_TO_POINTER(idx));
        g_object_set_data_full(G_OBJECT(card), "fname", g_strdup(fname), g_free);
        g_signal_connect(card, "clicked", G_CALLBACK(on_static_card_clicked), ctx);

        GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
        gtk_container_add(GTK_CONTAINER(card), vbox);

        GtkWidget *overlay = gtk_overlay_new();
        gtk_box_pack_start(GTK_BOX(vbox), overlay, FALSE, FALSE, 0);

        GtkWidget *img = gtk_image_new_from_icon_name("image-x-generic",
                                                       GTK_ICON_SIZE_DIALOG);
        gtk_widget_set_size_request(img, THUMB_WIDTH, THUMB_HEIGHT);
        gtk_container_add(GTK_CONTAINER(overlay), img);

        schedule_thumb_load(img, fpath, THUMB_WIDTH, THUMB_HEIGHT);

        /* Active Badge */
        GtkWidget *badge = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 2);
        gtk_style_context_add_class(gtk_widget_get_style_context(badge), "active-badge");
        gtk_box_pack_start(GTK_BOX(badge), gtk_image_new_from_icon_name("emblem-ok-symbolic", GTK_ICON_SIZE_MENU), FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(badge), gtk_label_new("Active"), FALSE, FALSE, 0);
        gtk_widget_set_halign(badge, GTK_ALIGN_END);
        gtk_widget_set_valign(badge, GTK_ALIGN_START);
        gtk_widget_set_margin_top(badge, 4);
        gtk_widget_set_margin_end(badge, 4);
        gtk_overlay_add_overlay(GTK_OVERLAY(overlay), badge);
        gtk_widget_set_no_show_all(badge, TRUE);
        gtk_widget_set_visible(badge, is_active);

        /* Title */
        GtkWidget *title = gtk_label_new(fname);
        gtk_style_context_add_class(gtk_widget_get_style_context(title), "card-title");
        gtk_label_set_ellipsize(GTK_LABEL(title), PANGO_ELLIPSIZE_MIDDLE);
        gtk_label_set_max_width_chars(GTK_LABEL(title), 18);
        gtk_box_pack_start(GTK_BOX(vbox), title, FALSE, FALSE, 0);

        ctx->static_grid.card_widgets[idx]  = card;
        ctx->static_grid.badge_widgets[idx] = badge;
        ctx->static_grid.title_widgets[idx] = title;

        gtk_flow_box_insert(GTK_FLOW_BOX(ctx->static_grid.gallery_flow_box), card, -1);
        ctx->static_grid.count++;
        g_free(fpath);
    }

    g_list_free_full(filenames, g_free);
    gtk_widget_show_all(ctx->static_grid.gallery_flow_box);
    update_grid_visuals(&ctx->static_grid, target_active);
}

/* ─────────────────────────────────────────────────────────────────────────────
 * Folder Navigation Button Callbacks
 * ──────────────────────────────────────────────────────────────────────────── */

static void on_back_btn_clicked(GtkButton *button, gpointer user_data)
{
    GuiCtx *ctx = (GuiCtx *)user_data;
    GridView *grid = (GridView *)g_object_get_data(G_OBJECT(button), "grid");
    if (ctx && grid) {
        show_folders_view(ctx, grid);
    }
}

static void on_folder_card_clicked(GtkButton *button, gpointer user_data)
{
    GuiCtx *ctx = (GuiCtx *)user_data;
    GridView *grid = (GridView *)g_object_get_data(G_OBJECT(button), "grid");
    int fidx = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(button), "fidx"));
    if (ctx && grid) {
        open_folder_view(ctx, grid, fidx);
    }
}

static void on_add_folder_btn_clicked(GtkButton *button, gpointer user_data)
{
    GuiCtx *ctx = (GuiCtx *)user_data;
    GridView *grid = (GridView *)g_object_get_data(G_OBJECT(button), "grid");
    if (!ctx || !grid) return;

    GtkWidget *dialog = gtk_file_chooser_dialog_new(
        grid->is_video ? "Select Video Wallpapers Folder" : "Select Static Image Wallpapers Folder",
        GTK_WINDOW(ctx->window),
        GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER,
        "_Cancel", GTK_RESPONSE_CANCEL,
        "_Add Folder", GTK_RESPONSE_ACCEPT,
        NULL);

    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
        char *folder = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));
        if (folder && folder[0]) {
            int existing_idx = -1;
            for (int i = 0; i < grid->folder_count; i++) {
                if (strcmp(grid->folders[i], folder) == 0) {
                    existing_idx = i;
                    break;
                }
            }

            if (existing_idx < 0) {
                if (grid->folder_count < MAX_FOLDERS) {
                    snprintf(grid->folders[grid->folder_count], LW_MAX_PATH, "%s", folder);
                    existing_idx = grid->folder_count;
                    grid->folder_count++;

                    if (grid->is_video) {
                        config_save_live_folders(grid->folders, grid->folder_count);
                    } else {
                        config_save_static_folders(grid->folders, grid->folder_count);
                    }
                }
            }

            if (existing_idx >= 0) {
                open_folder_view(ctx, grid, existing_idx);
            }
            g_free(folder);
        }
    }
    gtk_widget_destroy(dialog);
}

static void on_remove_folder_clicked(GtkWidget *button, gpointer user_data)
{
    GuiCtx *ctx = (GuiCtx *)user_data;
    GridView *grid = (GridView *)g_object_get_data(G_OBJECT(button), "grid");
    int fidx = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(button), "fidx"));
    if (!ctx || !grid || fidx < 0 || fidx >= grid->folder_count) return;

    char *base = g_path_get_basename(grid->folders[fidx]);
    char msg[512];
    snprintf(msg, sizeof(msg), "Remove folder \"%s\" from WallScape library?\n\n(No files on your computer will be deleted.)", base);

    if (confirm_action(GTK_WINDOW(ctx->window), "Remove Folder", msg, "Remove")) {
        for (int i = fidx; i < grid->folder_count - 1; i++) {
            snprintf(grid->folders[i], LW_MAX_PATH, "%s", grid->folders[i + 1]);
        }
        grid->folder_count--;

        if (grid->is_video) {
            config_save_live_folders(grid->folders, grid->folder_count);
        } else {
            config_save_static_folders(grid->folders, grid->folder_count);
        }

        show_folders_view(ctx, grid);
    }
    g_free(base);
}

/* ─────────────────────────────────────────────────────────────────────────────
 * Sidebar Tab Switching
 * ──────────────────────────────────────────────────────────────────────────── */
static void on_nav_tab_clicked(GtkButton *button, gpointer user_data)
{
    GuiCtx *ctx = (GuiCtx *)user_data;
    const char *tab_name = (const char *)g_object_get_data(G_OBJECT(button), "tab");

    if (!tab_name) return;

    gtk_stack_set_visible_child_name(GTK_STACK(ctx->stack), tab_name);

    if (strcmp(tab_name, "live") == 0) {
        gtk_style_context_add_class(gtk_widget_get_style_context(ctx->nav_live_btn), "active");
        gtk_style_context_remove_class(gtk_widget_get_style_context(ctx->nav_static_btn), "active");
    } else {
        gtk_style_context_add_class(gtk_widget_get_style_context(ctx->nav_static_btn), "active");
        gtk_style_context_remove_class(gtk_widget_get_style_context(ctx->nav_live_btn), "active");
    }
}

/* ─────────────────────────────────────────────────────────────────────────────
 * Control Callbacks
 * ──────────────────────────────────────────────────────────────────────────── */
static void on_pause_toggled(GtkButton *button, gpointer user_data)
{
    GuiCtx *ctx = (GuiCtx *)user_data;
    if (!atomic_load(&ctx->state->playing)) return;

    if (atomic_load(&ctx->state->paused)) {
        decoder_resume(ctx->state);
        gtk_button_set_label(button, "Pause");
        gtk_button_set_image(button, gtk_image_new_from_icon_name("media-playback-pause-symbolic", GTK_ICON_SIZE_BUTTON));
        gui_set_status(ctx, "Live playback resumed.");
    } else {
        decoder_pause(ctx->state);
        gtk_button_set_label(button, "Resume");
        gtk_button_set_image(button, gtk_image_new_from_icon_name("media-playback-start-symbolic", GTK_ICON_SIZE_BUTTON));
        gui_set_status(ctx, "Live playback paused.");
    }
}

static void on_stop_clicked(GtkButton *button, gpointer user_data)
{
    (void)button;
    GuiCtx *ctx = (GuiCtx *)user_data;

    if (!atomic_load(&ctx->state->playing) && ctx->active_static_path[0] == '\0') return;

    if (confirm_action(GTK_WINDOW(ctx->window), "Turn Off Wallpaper?", "Turn off the current desktop wallpaper?", "Turn Off")) {
        stop_live_wallpaper(ctx);
        ctx->active_static_path[0] = '\0';
        update_grid_visuals(&ctx->static_grid, -1);
    }
}

static void on_quit_clicked(GtkButton *button, gpointer user_data)
{
    (void)button;
    (void)user_data;
    GApplication *app = g_application_get_default();
    if (app) {
        g_application_quit(app);
    } else {
        gtk_main_quit();
    }
}

static gboolean on_window_delete_event(GtkWidget *widget, GdkEvent *event, gpointer user_data)
{
    (void)event;
    (void)user_data;
    /* UX-5: Hide the control panel rather than quitting.
     * The live wallpaper keeps playing in the background.
     * Users can re-show the panel by re-launching the binary,
     * and quit explicitly via the "Quit" button. */
    gtk_widget_hide(widget);
    return TRUE;  /* TRUE = don't destroy the window */
}

/* ─────────────────────────────────────────────────────────────────────────────
 * Render Timer Callback (~60fps)
 * ──────────────────────────────────────────────────────────────────────────── */
static gboolean on_render_tick(gpointer user_data)
{
    GuiCtx *ctx = (GuiCtx *)user_data;

    if (!wallpaper_process_events(ctx->wallpaper)) {
        GApplication *app = g_application_get_default();
        if (app) {
            g_application_quit(app);
        } else {
            gtk_main_quit();
        }
        return G_SOURCE_REMOVE;
    }

    if (atomic_load(&ctx->state->quit)) {
        return G_SOURCE_REMOVE;
    }

    /* UX-4: Once the decoder signals ready, update the status bar with accurate
     * video metadata, and switch the render timer to match the video's FPS. */
    if (atomic_load(&ctx->state->decoder_ready)) {
        /* Check if status still shows the loading placeholder */
        const char *cur = gtk_label_get_text(GTK_LABEL(ctx->status_label));
        if (cur && strstr(cur, "Loading live wallpaper")) {
            int w = atomic_load(&ctx->state->video_width);
            int h = atomic_load(&ctx->state->video_height);
            double fps = ctx->state->video_fps;
            if (fps <= 0.0) fps = 30.0;

            char *base = g_path_get_basename(ctx->state->video_path);
            char buf[512];
            const char *orient = (h > w) ? " (portrait)" : "";
            snprintf(buf, sizeof(buf), "🎬 Live Wallpaper: %s (%dx%d @ %.0ffps%s)",
                     base, w, h, fps, orient);
            gui_set_status(ctx, buf);
            g_free(base);

            /* Re-start the render timer at the video's actual FPS so we don't
             * over-poll (wastes CPU) or under-poll (drops frames). */
            if (ctx->render_fps != fps) {
                gui_start_render_timer_at_fps(ctx, fps);
                return G_SOURCE_REMOVE; /* old timer must stop; new one was added */
            }
        }
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
 * Update Management & Dialogs
 * ──────────────────────────────────────────────────────────────────────────── */
static void on_update_download_complete(bool success, const char *deb_path, const char *error_msg, gpointer user_data)
{
    GuiCtx *ctx = (GuiCtx *)user_data;
    if (!ctx || !ctx->window) return;

    if (success) {
        gui_set_status(ctx, "Installer launched! Please complete the installation in the installer window.");
        GtkWidget *dialog = gtk_message_dialog_new(
            GTK_WINDOW(ctx->window),
            GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
            GTK_MESSAGE_INFO,
            GTK_BUTTONS_OK,
            "Update Package Downloaded");
        gtk_message_dialog_format_secondary_text(
            GTK_MESSAGE_DIALOG(dialog),
            "The installer has been launched automatically (%s).\nPlease follow the on-screen prompt to finish updating WallScape.",
            deb_path ? deb_path : "/tmp/wallscape-latest.deb");
        gtk_dialog_run(GTK_DIALOG(dialog));
        gtk_widget_destroy(dialog);
    } else {
        gui_set_status(ctx, "Update download failed.");
        GtkWidget *dialog = gtk_message_dialog_new(
            GTK_WINDOW(ctx->window),
            GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
            GTK_MESSAGE_ERROR,
            GTK_BUTTONS_OK,
            "Update Failed");
        gtk_message_dialog_format_secondary_text(
            GTK_MESSAGE_DIALOG(dialog),
            "%s", error_msg && error_msg[0] ? error_msg : "Could not download the update package.");
        gtk_dialog_run(GTK_DIALOG(dialog));
        gtk_widget_destroy(dialog);
    }
}

static void show_update_dialog(GuiCtx *ctx, const UpdateInfo *info)
{
    GtkWidget *dialog = gtk_dialog_new_with_buttons(
        "WallScape Update Available",
        GTK_WINDOW(ctx->window),
        GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
        "Later", GTK_RESPONSE_CANCEL,
        "View on GitHub", GTK_RESPONSE_HELP,
        "Download & Install Update", GTK_RESPONSE_ACCEPT,
        NULL);

    gtk_dialog_set_default_response(GTK_DIALOG(dialog), GTK_RESPONSE_ACCEPT);
    gtk_window_set_default_size(GTK_WINDOW(dialog), 420, 240);

    GtkWidget *content_area = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    gtk_container_set_border_width(GTK_CONTAINER(content_area), 16);

    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_container_add(GTK_CONTAINER(content_area), vbox);

    char title_str[256];
    snprintf(title_str, sizeof(title_str),
             "<span font='13' weight='bold'>🎉 New Version Available: v%s</span>",
             info->latest_version);
    GtkWidget *title_lbl = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(title_lbl), title_str);
    gtk_label_set_xalign(GTK_LABEL(title_lbl), 0.0);
    gtk_box_pack_start(GTK_BOX(vbox), title_lbl, FALSE, FALSE, 0);

    char desc_str[512];
    snprintf(desc_str, sizeof(desc_str),
             "A newer version of WallScape is available on GitHub.\n"
             "Installed: <b>v%s</b>  ➜  Latest: <b>v%s</b>",
             info->current_version, info->latest_version);
    GtkWidget *desc_lbl = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(desc_lbl), desc_str);
    gtk_label_set_xalign(GTK_LABEL(desc_lbl), 0.0);
    gtk_box_pack_start(GTK_BOX(vbox), desc_lbl, FALSE, FALSE, 0);

    if (info->release_notes[0]) {
        GtkWidget *notes_scr = gtk_scrolled_window_new(NULL, NULL);
        gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(notes_scr), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
        gtk_widget_set_size_request(notes_scr, -1, 90);

        GtkWidget *notes_lbl = gtk_label_new(info->release_notes);
        gtk_label_set_line_wrap(GTK_LABEL(notes_lbl), TRUE);
        gtk_label_set_xalign(GTK_LABEL(notes_lbl), 0.0);
        gtk_label_set_yalign(GTK_LABEL(notes_lbl), 0.0);
        gtk_container_add(GTK_CONTAINER(notes_scr), notes_lbl);
        gtk_box_pack_start(GTK_BOX(vbox), notes_scr, TRUE, TRUE, 0);
    }

    gtk_widget_show_all(dialog);
    gint res = gtk_dialog_run(GTK_DIALOG(dialog));

    if (res == GTK_RESPONSE_ACCEPT) {
        gui_set_status(ctx, "Downloading WallScape update package...");
        updater_download_and_install_async(info->deb_download_url, NULL, on_update_download_complete, ctx);
    } else if (res == GTK_RESPONSE_HELP) {
        if (info->release_url[0]) {
            gtk_show_uri_on_window(GTK_WINDOW(ctx->window), info->release_url, GDK_CURRENT_TIME, NULL);
        }
    }

    gtk_widget_destroy(dialog);
}

static void on_bg_update_check_result(const UpdateInfo *info, gpointer user_data)
{
    GuiCtx *ctx = (GuiCtx *)user_data;
    if (!ctx || !ctx->window) return;

    if (ctx->update_btn) {
        gtk_button_set_label(GTK_BUTTON(ctx->update_btn), "Check for Updates");
    }

    if (info->update_available) {
        memcpy(&ctx->last_update_info, info, sizeof(UpdateInfo));
        if (ctx->update_btn) {
            char btn_lbl[64];
            snprintf(btn_lbl, sizeof(btn_lbl), "Update v%s", info->latest_version);
            gtk_button_set_label(GTK_BUTTON(ctx->update_btn), btn_lbl);
            gtk_style_context_add_class(gtk_widget_get_style_context(ctx->update_btn), "available");
        }
        show_update_dialog(ctx, info);
    }
}

static void on_manual_update_check_result(const UpdateInfo *info, gpointer user_data)
{
    GuiCtx *ctx = (GuiCtx *)user_data;
    if (!ctx || !ctx->window) return;

    if (ctx->update_btn) {
        gtk_button_set_label(GTK_BUTTON(ctx->update_btn), "Check for Updates");
    }

    if (info->update_available) {
        memcpy(&ctx->last_update_info, info, sizeof(UpdateInfo));
        if (ctx->update_btn) {
            char btn_lbl[64];
            snprintf(btn_lbl, sizeof(btn_lbl), "Update v%s", info->latest_version);
            gtk_button_set_label(GTK_BUTTON(ctx->update_btn), btn_lbl);
            gtk_style_context_add_class(gtk_widget_get_style_context(ctx->update_btn), "available");
        }
        show_update_dialog(ctx, info);
    } else {
        GtkWidget *dialog = gtk_message_dialog_new(
            GTK_WINDOW(ctx->window),
            GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
            GTK_MESSAGE_INFO,
            GTK_BUTTONS_OK,
            info->error_msg[0] ? "Update Check" : "Up to Date");
        gtk_message_dialog_format_secondary_text(
            GTK_MESSAGE_DIALOG(dialog),
            "%s", info->error_msg[0] ? info->error_msg : "WallScape is up to date (v" WALLSCAPE_CURRENT_VERSION ").");
        gtk_dialog_run(GTK_DIALOG(dialog));
        gtk_widget_destroy(dialog);
    }
}

static void on_update_btn_clicked(GtkButton *btn, gpointer user_data)
{
    GuiCtx *ctx = (GuiCtx *)user_data;
    if (ctx->last_update_info.update_available) {
        show_update_dialog(ctx, &ctx->last_update_info);
        return;
    }
    gtk_button_set_label(btn, "Checking...");
    updater_check_async(on_manual_update_check_result, ctx);
}

static void on_gnome_bg_changed(GSettings *settings, const gchar *key, gpointer user_data)
{
    (void)key;
    (void)settings;
    GuiCtx *ctx = (GuiCtx *)user_data;
    if (!ctx) return;

    char current_bg[LW_MAX_PATH] = {0};
    if (!static_wallpaper_get_current(current_bg, sizeof(current_bg)) || current_bg[0] == '\0') {
        ctx->active_static_path[0] = '\0';
        config_save_static_path("");
        update_grid_visuals(&ctx->static_grid, -1);
        return;
    }

    if (strcmp(ctx->active_static_path, current_bg) != 0) {
        snprintf(ctx->active_static_path, sizeof(ctx->active_static_path), "%s", current_bg);
        if (!atomic_load(&ctx->state->playing)) {
            config_save_static_path(current_bg);
        }
    }

    int match = -1;
    for (int i = 0; i < ctx->static_grid.count; i++) {
        if (strcmp(ctx->static_grid.items[i], current_bg) == 0) {
            match = i;
            break;
        }
    }
    update_grid_visuals(&ctx->static_grid, match);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Deferred folder restore (runs after window is fully realized)
 * ═══════════════════════════════════════════════════════════════════════════ */
static gboolean on_restore_folders_idle(gpointer user_data)
{
    GuiCtx *ctx = (GuiCtx *)user_data;

    /* 1. Live Wallpapers Multi-Folder */
    ctx->live_grid.is_video = true;
    int live_count = 0;
    config_load_live_folders(ctx->live_grid.folders, MAX_FOLDERS, &live_count);
    ctx->live_grid.folder_count = live_count;

    char saved_video[LW_MAX_PATH] = {0};
    bool has_saved_video = config_load(saved_video, sizeof(saved_video)) &&
                           saved_video[0] != '\0' &&
                           access(saved_video, R_OK) == 0;

    int open_live_idx = -1;
    if (has_saved_video) {
        for (int i = 0; i < ctx->live_grid.folder_count; i++) {
            if (strncmp(saved_video, ctx->live_grid.folders[i], strlen(ctx->live_grid.folders[i])) == 0) {
                open_live_idx = i;
                break;
            }
        }
        if (open_live_idx < 0 && ctx->live_grid.folder_count < MAX_FOLDERS) {
            char *dname = g_path_get_dirname(saved_video);
            snprintf(ctx->live_grid.folders[ctx->live_grid.folder_count], LW_MAX_PATH, "%s", dname);
            open_live_idx = ctx->live_grid.folder_count;
            ctx->live_grid.folder_count++;
            config_save_live_folders(ctx->live_grid.folders, ctx->live_grid.folder_count);
            g_free(dname);
        }

        if (!atomic_load(&ctx->state->playing)) {
            start_live_wallpaper(ctx, saved_video);
        }
    }

    if (open_live_idx >= 0) {
        open_folder_view(ctx, &ctx->live_grid, open_live_idx);
    } else {
        show_folders_view(ctx, &ctx->live_grid);
    }

    /* 2. Static Wallpapers Multi-Folder */
    ctx->static_grid.is_video = false;
    int static_count = 0;
    config_load_static_folders(ctx->static_grid.folders, MAX_FOLDERS, &static_count);
    ctx->static_grid.folder_count = static_count;

    char saved_static_path[LW_MAX_PATH] = {0};
    bool has_saved_static = config_load_static_path(saved_static_path, sizeof(saved_static_path)) &&
                            saved_static_path[0] != '\0' &&
                            g_file_test(saved_static_path, G_FILE_TEST_IS_REGULAR);

    if (!has_saved_static && !has_saved_video) {
        static_wallpaper_get_current(saved_static_path, sizeof(saved_static_path));
        if (saved_static_path[0] != '\0' && g_file_test(saved_static_path, G_FILE_TEST_IS_REGULAR)) {
            has_saved_static = true;
        }
    }

    int open_static_idx = -1;
    if (has_saved_static && !has_saved_video) {
        snprintf(ctx->active_static_path, sizeof(ctx->active_static_path), "%s", saved_static_path);
        for (int i = 0; i < ctx->static_grid.folder_count; i++) {
            if (strncmp(saved_static_path, ctx->static_grid.folders[i], strlen(ctx->static_grid.folders[i])) == 0) {
                open_static_idx = i;
                break;
            }
        }
        if (open_static_idx < 0 && ctx->static_grid.folder_count < MAX_FOLDERS) {
            char *dname = g_path_get_dirname(saved_static_path);
            snprintf(ctx->static_grid.folders[ctx->static_grid.folder_count], LW_MAX_PATH, "%s", dname);
            open_static_idx = ctx->static_grid.folder_count;
            ctx->static_grid.folder_count++;
            config_save_static_folders(ctx->static_grid.folders, ctx->static_grid.folder_count);
            g_free(dname);
        }
    }

    if (open_static_idx >= 0) {
        open_folder_view(ctx, &ctx->static_grid, open_static_idx);
    } else {
        show_folders_view(ctx, &ctx->static_grid);
    }

    /* Check for updates in background */
    updater_check_async(on_bg_update_check_result, ctx);

    return G_SOURCE_REMOVE; /* one-shot */
}

/* ═══════════════════════════════════════════════════════════════════════════
 * System Tray Icon Callbacks
 * ═══════════════════════════════════════════════════════════════════════════ */

static void on_tray_activate(GtkStatusIcon *icon, gpointer user_data)
{
    (void)icon;
    GuiCtx *ctx = (GuiCtx *)user_data;
    if (!ctx->window) return;

    if (gtk_widget_get_visible(ctx->window)) {
        gtk_widget_hide(ctx->window);
    } else {
        gtk_widget_show_all(ctx->window);
        gtk_window_present(GTK_WINDOW(ctx->window));
    }
}

static void on_tray_popup_menu(GtkStatusIcon *icon, guint button,
                               guint activate_time, gpointer user_data)
{
    GuiCtx *ctx = (GuiCtx *)user_data;
    GtkWidget *menu = gtk_menu_new();

    /* Show/Hide window */
    const char *vis_label = gtk_widget_get_visible(ctx->window)
                            ? "Hide WallScape" : "Show WallScape";
    GtkWidget *item_show = gtk_menu_item_new_with_label(vis_label);
    g_signal_connect_swapped(item_show, "activate",
                             G_CALLBACK(on_tray_activate), ctx);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), item_show);

    gtk_menu_shell_append(GTK_MENU_SHELL(menu),
                          gtk_separator_menu_item_new());

    /* Turn Off Wallpaper */
    GtkWidget *item_stop = gtk_menu_item_new_with_label("Turn Off Wallpaper");
    g_signal_connect_swapped(item_stop, "activate",
                             G_CALLBACK(stop_live_wallpaper), ctx);
    gtk_widget_set_sensitive(item_stop,
                             atomic_load(&ctx->state->playing));
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), item_stop);

    gtk_menu_shell_append(GTK_MENU_SHELL(menu),
                          gtk_separator_menu_item_new());

    /* Quit */
    GtkWidget *item_quit = gtk_menu_item_new_with_label("Quit WallScape");
    g_signal_connect(item_quit, "activate", G_CALLBACK(on_quit_clicked), ctx);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), item_quit);

    gtk_widget_show_all(menu);
    gtk_menu_popup(GTK_MENU(menu), NULL, NULL,
                   gtk_status_icon_position_menu, icon,
                   button, activate_time);
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

    /* Apply custom modern CSS styling */
    GtkCssProvider *provider = gtk_css_provider_new();
    gtk_css_provider_load_from_data(provider, STUDIO_CSS, -1, NULL);
    gtk_style_context_add_provider_for_screen(
        gdk_screen_get_default(),
        GTK_STYLE_PROVIDER(provider),
        GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_unref(provider);

    /* Main Window */
    ctx->window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(ctx->window), "WallScape");
    gtk_window_set_default_size(GTK_WINDOW(ctx->window), 780, 520);
    gtk_window_set_position(GTK_WINDOW(ctx->window), GTK_WIN_POS_CENTER);

    /* Set Application Icon — MEM-2: Use the icon theme (works after install and
     * during development if the icon is installed in the hicolor theme).
     * Fall back to a generic wallpaper icon if not found. */
    GtkIconTheme *icon_theme = gtk_icon_theme_get_default();
    GdkPixbuf *app_icon = gtk_icon_theme_load_icon(icon_theme, "live-wallpaper",
                                                    64, GTK_ICON_LOOKUP_USE_BUILTIN, NULL);
    if (!app_icon) {
        /* Development fallback: try the local assets/ path relative to CWD */
        GError *err = NULL;
        app_icon = gdk_pixbuf_new_from_file("assets/live-wallpaper.svg", &err);
        if (err) g_error_free(err);
    }
    if (app_icon) {
        gtk_window_set_icon(GTK_WINDOW(ctx->window), app_icon);
    }

    /* Keep the control panel out of the taskbar — it lives in the tray */
    gtk_window_set_skip_taskbar_hint(GTK_WINDOW(ctx->window), TRUE);
    gtk_window_set_skip_pager_hint(GTK_WINDOW(ctx->window), TRUE);

    gtk_style_context_add_class(gtk_widget_get_style_context(ctx->window), "studio-window");
    g_signal_connect(ctx->window, "delete-event", G_CALLBACK(on_window_delete_event), ctx);

    /* ── System Tray Icon ──────────────────────────────────────────────────── */
    ctx->tray_icon = gtk_status_icon_new();
    gtk_status_icon_set_tooltip_text(ctx->tray_icon,
                                     "WallScape — Live Wallpaper Manager");
    if (app_icon) {
        gtk_status_icon_set_from_pixbuf(ctx->tray_icon, app_icon);
    } else {
        gtk_status_icon_set_from_icon_name(ctx->tray_icon,
                                           "preferences-desktop-wallpaper");
    }
    gtk_status_icon_set_visible(ctx->tray_icon, TRUE);
    g_signal_connect(ctx->tray_icon, "activate",
                     G_CALLBACK(on_tray_activate), ctx);
    g_signal_connect(ctx->tray_icon, "popup-menu",
                     G_CALLBACK(on_tray_popup_menu), ctx);

    /* UX-6: Keyboard accelerators ─────────────────────────────────────────── */
    GtkAccelGroup *accel_group = gtk_accel_group_new();
    gtk_window_add_accel_group(GTK_WINDOW(ctx->window), accel_group);
    /* Ctrl+Q → Quit */
    gtk_accel_group_connect(accel_group,
        GDK_KEY_q, GDK_CONTROL_MASK, GTK_ACCEL_VISIBLE,
        g_cclosure_new(G_CALLBACK(on_quit_clicked), ctx, NULL));
    g_object_unref(accel_group);

    /* Root Horizontal Box: Sidebar (Left) + Main Content (Right) */
    GtkWidget *root_hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_container_add(GTK_CONTAINER(ctx->window), root_hbox);

    /* ═══════════════════════════════════════════════════════════════════════
     * LEFT SIDEBAR
     * ═══════════════════════════════════════════════════════════════════════ */
    GtkWidget *sidebar = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_style_context_add_class(gtk_widget_get_style_context(sidebar), "sidebar");
    gtk_widget_set_size_request(sidebar, 180, -1);
    gtk_box_pack_start(GTK_BOX(root_hbox), sidebar, FALSE, FALSE, 0);

    /* Sidebar Branding Header */
    GtkWidget *brand_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_style_context_add_class(gtk_widget_get_style_context(brand_box), "sidebar-brand");
    gtk_box_pack_start(GTK_BOX(sidebar), brand_box, FALSE, FALSE, 0);

    GtkWidget *logo_hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_box_pack_start(GTK_BOX(brand_box), logo_hbox, FALSE, FALSE, 0);

    if (app_icon) {
        GdkPixbuf *small_logo = gdk_pixbuf_scale_simple(app_icon, 28, 28, GDK_INTERP_BILINEAR);
        if (small_logo) {
            gtk_box_pack_start(GTK_BOX(logo_hbox), gtk_image_new_from_pixbuf(small_logo), FALSE, FALSE, 0);
            g_object_unref(small_logo);
        }
        g_object_unref(app_icon);
    } else {
        gtk_box_pack_start(GTK_BOX(logo_hbox), gtk_image_new_from_icon_name("preferences-desktop-wallpaper", GTK_ICON_SIZE_LARGE_TOOLBAR), FALSE, FALSE, 0);
    }

    GtkWidget *brand_lbl_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_box_pack_start(GTK_BOX(logo_hbox), brand_lbl_box, TRUE, TRUE, 0);

    GtkWidget *brand_title = gtk_label_new("WallScape");
    gtk_style_context_add_class(gtk_widget_get_style_context(brand_title), "sidebar-title");
    gtk_label_set_xalign(GTK_LABEL(brand_title), 0.0);
    gtk_box_pack_start(GTK_BOX(brand_lbl_box), brand_title, FALSE, FALSE, 0);

    GtkWidget *brand_sub = gtk_label_new("for Zorin OS");
    gtk_style_context_add_class(gtk_widget_get_style_context(brand_sub), "sidebar-subtitle");
    gtk_label_set_xalign(GTK_LABEL(brand_sub), 0.0);
    gtk_box_pack_start(GTK_BOX(brand_lbl_box), brand_sub, FALSE, FALSE, 0);

    /* ── Tab 1 Button: Live Wallpaper ── */
    ctx->nav_live_btn = gtk_button_new();
    GtkWidget *live_btn_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    GtkWidget *live_icon = gtk_image_new_from_icon_name("video-display-symbolic", GTK_ICON_SIZE_BUTTON);
    GtkWidget *live_text = gtk_label_new("Live Wallpapers");
    gtk_box_pack_start(GTK_BOX(live_btn_box), live_icon, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(live_btn_box), live_text, FALSE, FALSE, 0);
    gtk_container_add(GTK_CONTAINER(ctx->nav_live_btn), live_btn_box);
    gtk_style_context_add_class(gtk_widget_get_style_context(ctx->nav_live_btn), "nav-btn");
    gtk_style_context_add_class(gtk_widget_get_style_context(ctx->nav_live_btn), "active");
    g_object_set_data(G_OBJECT(ctx->nav_live_btn), "tab", (gpointer)"live");
    g_signal_connect(ctx->nav_live_btn, "clicked", G_CALLBACK(on_nav_tab_clicked), ctx);
    gtk_box_pack_start(GTK_BOX(sidebar), ctx->nav_live_btn, FALSE, FALSE, 0);

    /* ── Tab 2 Button: Static Wallpaper ── */
    ctx->nav_static_btn = gtk_button_new();
    GtkWidget *static_btn_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    GtkWidget *static_icon = gtk_image_new_from_icon_name("preferences-desktop-wallpaper-symbolic", GTK_ICON_SIZE_BUTTON);
    GtkWidget *static_text = gtk_label_new("Static Wallpapers");
    gtk_box_pack_start(GTK_BOX(static_btn_box), static_icon, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(static_btn_box), static_text, FALSE, FALSE, 0);
    gtk_container_add(GTK_CONTAINER(ctx->nav_static_btn), static_btn_box);
    gtk_style_context_add_class(gtk_widget_get_style_context(ctx->nav_static_btn), "nav-btn");
    g_object_set_data(G_OBJECT(ctx->nav_static_btn), "tab", (gpointer)"static");
    g_signal_connect(ctx->nav_static_btn, "clicked", G_CALLBACK(on_nav_tab_clicked), ctx);
    gtk_box_pack_start(GTK_BOX(sidebar), ctx->nav_static_btn, FALSE, FALSE, 0);

    /* Sidebar vertical expanding spacer */
    GtkWidget *sidebar_spacer = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_box_pack_start(GTK_BOX(sidebar), sidebar_spacer, TRUE, TRUE, 0);

    /* Sidebar Footer: Version and Update Checker */
    GtkWidget *side_footer = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_box_pack_end(GTK_BOX(sidebar), side_footer, FALSE, FALSE, 0);

    GtkWidget *ver_lbl = gtk_label_new("v" WALLSCAPE_CURRENT_VERSION);
    gtk_style_context_add_class(gtk_widget_get_style_context(ver_lbl), "sidebar-version");
    gtk_label_set_xalign(GTK_LABEL(ver_lbl), 0.5);
    gtk_box_pack_start(GTK_BOX(side_footer), ver_lbl, FALSE, FALSE, 0);

    ctx->update_btn = gtk_button_new_with_label("Check for Updates");
    gtk_button_set_image(GTK_BUTTON(ctx->update_btn), gtk_image_new_from_icon_name("software-update-available-symbolic", GTK_ICON_SIZE_BUTTON));
    gtk_button_set_always_show_image(GTK_BUTTON(ctx->update_btn), TRUE);
    gtk_style_context_add_class(gtk_widget_get_style_context(ctx->update_btn), "update-btn");
    g_signal_connect(ctx->update_btn, "clicked", G_CALLBACK(on_update_btn_clicked), ctx);
    gtk_box_pack_start(GTK_BOX(side_footer), ctx->update_btn, FALSE, FALSE, 0);

    /* ═══════════════════════════════════════════════════════════════════════
     * RIGHT MAIN CONTENT (GtkStack)
     * ═══════════════════════════════════════════════════════════════════════ */
    GtkWidget *content_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_box_pack_start(GTK_BOX(root_hbox), content_vbox, TRUE, TRUE, 0);

    ctx->stack = gtk_stack_new();
    gtk_stack_set_transition_type(GTK_STACK(ctx->stack), GTK_STACK_TRANSITION_TYPE_CROSSFADE);
    gtk_stack_set_transition_duration(GTK_STACK(ctx->stack), 150);
    gtk_box_pack_start(GTK_BOX(content_vbox), ctx->stack, TRUE, TRUE, 0);

    /* ───────────────────────────────────────────────────────────────────────
     * Page 1: Live Wallpaper View
     * ─────────────────────────────────────────────────────────────────────── */
    GtkWidget *live_page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_stack_add_named(GTK_STACK(ctx->stack), live_page, "live");

    /* Live Header Bar */
    GtkWidget *live_hdr = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_style_context_add_class(gtk_widget_get_style_context(live_hdr), "header-bar");
    gtk_box_pack_start(GTK_BOX(live_page), live_hdr, FALSE, FALSE, 0);

    /* Back button */
    ctx->live_grid.back_btn = gtk_button_new_with_label("← Folders");
    gtk_style_context_add_class(gtk_widget_get_style_context(ctx->live_grid.back_btn), "back-btn");
    g_object_set_data(G_OBJECT(ctx->live_grid.back_btn), "grid", &ctx->live_grid);
    g_signal_connect(ctx->live_grid.back_btn, "clicked", G_CALLBACK(on_back_btn_clicked), ctx);
    gtk_widget_set_no_show_all(ctx->live_grid.back_btn, TRUE);
    gtk_widget_set_visible(ctx->live_grid.back_btn, FALSE);
    gtk_box_pack_start(GTK_BOX(live_hdr), ctx->live_grid.back_btn, FALSE, FALSE, 0);

    ctx->live_grid.folder_label = gtk_label_new("📁 Live Wallpapers");
    gtk_label_set_use_markup(GTK_LABEL(ctx->live_grid.folder_label), TRUE);
    gtk_label_set_ellipsize(GTK_LABEL(ctx->live_grid.folder_label), PANGO_ELLIPSIZE_MIDDLE);
    gtk_label_set_xalign(GTK_LABEL(ctx->live_grid.folder_label), 0.0);
    gtk_box_pack_start(GTK_BOX(live_hdr), ctx->live_grid.folder_label, TRUE, TRUE, 0);

    ctx->live_grid.add_folder_btn = gtk_button_new_with_label("Add Folder...");
    gtk_button_set_image(GTK_BUTTON(ctx->live_grid.add_folder_btn), gtk_image_new_from_icon_name("folder-open-symbolic", GTK_ICON_SIZE_BUTTON));
    gtk_button_set_always_show_image(GTK_BUTTON(ctx->live_grid.add_folder_btn), TRUE);
    g_object_set_data(G_OBJECT(ctx->live_grid.add_folder_btn), "grid", &ctx->live_grid);
    g_signal_connect(ctx->live_grid.add_folder_btn, "clicked", G_CALLBACK(on_add_folder_btn_clicked), ctx);
    gtk_box_pack_end(GTK_BOX(live_hdr), ctx->live_grid.add_folder_btn, FALSE, FALSE, 0);

    /* Inner page stack: empty-state ↔ folders ↔ gallery */
    ctx->live_grid.page_stack = gtk_stack_new();
    ctx->live_grid.is_video = true;
    gtk_stack_set_transition_type(GTK_STACK(ctx->live_grid.page_stack), GTK_STACK_TRANSITION_TYPE_CROSSFADE);
    gtk_stack_set_transition_duration(GTK_STACK(ctx->live_grid.page_stack), 180);
    gtk_box_pack_start(GTK_BOX(live_page), ctx->live_grid.page_stack, TRUE, TRUE, 0);

    /* 1. Empty state */
    ctx->live_grid.empty_state = create_empty_state(
        "video-x-generic-symbolic",
        "No Video Folders Added",
        "Click 'Add Folder...' above to add folders\nwith your video wallpapers.");
    gtk_stack_add_named(GTK_STACK(ctx->live_grid.page_stack), ctx->live_grid.empty_state, "empty");

    /* 2. Folders root view scroll */
    GtkWidget *live_folders_scr = gtk_scrolled_window_new(NULL, NULL);
    gtk_style_context_add_class(gtk_widget_get_style_context(live_folders_scr), "gallery-scroll");
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(live_folders_scr), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_stack_add_named(GTK_STACK(ctx->live_grid.page_stack), live_folders_scr, "folders");

    ctx->live_grid.folders_flow_box = gtk_flow_box_new();
    gtk_widget_set_valign(ctx->live_grid.folders_flow_box, GTK_ALIGN_START);
    gtk_flow_box_set_max_children_per_line(GTK_FLOW_BOX(ctx->live_grid.folders_flow_box), 6);
    gtk_flow_box_set_min_children_per_line(GTK_FLOW_BOX(ctx->live_grid.folders_flow_box), 2);
    gtk_flow_box_set_selection_mode(GTK_FLOW_BOX(ctx->live_grid.folders_flow_box), GTK_SELECTION_NONE);
    gtk_flow_box_set_column_spacing(GTK_FLOW_BOX(ctx->live_grid.folders_flow_box), 10);
    gtk_flow_box_set_row_spacing(GTK_FLOW_BOX(ctx->live_grid.folders_flow_box), 10);
    gtk_container_set_border_width(GTK_CONTAINER(ctx->live_grid.folders_flow_box), 12);
    gtk_container_add(GTK_CONTAINER(live_folders_scr), ctx->live_grid.folders_flow_box);

    /* 3. Items Gallery scroll */
    GtkWidget *live_gallery_scr = gtk_scrolled_window_new(NULL, NULL);
    gtk_style_context_add_class(gtk_widget_get_style_context(live_gallery_scr), "gallery-scroll");
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(live_gallery_scr), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_stack_add_named(GTK_STACK(ctx->live_grid.page_stack), live_gallery_scr, "gallery");

    ctx->live_grid.gallery_flow_box = gtk_flow_box_new();
    gtk_widget_set_valign(ctx->live_grid.gallery_flow_box, GTK_ALIGN_START);
    gtk_flow_box_set_max_children_per_line(GTK_FLOW_BOX(ctx->live_grid.gallery_flow_box), 8);
    gtk_flow_box_set_min_children_per_line(GTK_FLOW_BOX(ctx->live_grid.gallery_flow_box), 2);
    gtk_flow_box_set_selection_mode(GTK_FLOW_BOX(ctx->live_grid.gallery_flow_box), GTK_SELECTION_NONE);
    gtk_flow_box_set_column_spacing(GTK_FLOW_BOX(ctx->live_grid.gallery_flow_box), 8);
    gtk_flow_box_set_row_spacing(GTK_FLOW_BOX(ctx->live_grid.gallery_flow_box), 8);
    gtk_container_set_border_width(GTK_CONTAINER(ctx->live_grid.gallery_flow_box), 10);
    gtk_container_add(GTK_CONTAINER(live_gallery_scr), ctx->live_grid.gallery_flow_box);

    /* Default: show empty state */
    gtk_stack_set_visible_child_name(GTK_STACK(ctx->live_grid.page_stack), "empty");

    /* ───────────────────────────────────────────────────────────────────────
     * Page 2: Static Wallpaper View
     * ─────────────────────────────────────────────────────────────────────── */
    GtkWidget *static_page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_stack_add_named(GTK_STACK(ctx->stack), static_page, "static");

    /* Static Header Bar */
    GtkWidget *static_hdr = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_style_context_add_class(gtk_widget_get_style_context(static_hdr), "header-bar");
    gtk_box_pack_start(GTK_BOX(static_page), static_hdr, FALSE, FALSE, 0);

    /* Back button */
    ctx->static_grid.back_btn = gtk_button_new_with_label("← Folders");
    gtk_style_context_add_class(gtk_widget_get_style_context(ctx->static_grid.back_btn), "back-btn");
    g_object_set_data(G_OBJECT(ctx->static_grid.back_btn), "grid", &ctx->static_grid);
    g_signal_connect(ctx->static_grid.back_btn, "clicked", G_CALLBACK(on_back_btn_clicked), ctx);
    gtk_widget_set_no_show_all(ctx->static_grid.back_btn, TRUE);
    gtk_widget_set_visible(ctx->static_grid.back_btn, FALSE);
    gtk_box_pack_start(GTK_BOX(static_hdr), ctx->static_grid.back_btn, FALSE, FALSE, 0);

    ctx->static_grid.folder_label = gtk_label_new("📁 Static Wallpapers");
    gtk_label_set_use_markup(GTK_LABEL(ctx->static_grid.folder_label), TRUE);
    gtk_label_set_ellipsize(GTK_LABEL(ctx->static_grid.folder_label), PANGO_ELLIPSIZE_MIDDLE);
    gtk_label_set_xalign(GTK_LABEL(ctx->static_grid.folder_label), 0.0);
    gtk_box_pack_start(GTK_BOX(static_hdr), ctx->static_grid.folder_label, TRUE, TRUE, 0);

    ctx->static_grid.add_folder_btn = gtk_button_new_with_label("Add Folder...");
    gtk_button_set_image(GTK_BUTTON(ctx->static_grid.add_folder_btn), gtk_image_new_from_icon_name("folder-open-symbolic", GTK_ICON_SIZE_BUTTON));
    gtk_button_set_always_show_image(GTK_BUTTON(ctx->static_grid.add_folder_btn), TRUE);
    g_object_set_data(G_OBJECT(ctx->static_grid.add_folder_btn), "grid", &ctx->static_grid);
    g_signal_connect(ctx->static_grid.add_folder_btn, "clicked", G_CALLBACK(on_add_folder_btn_clicked), ctx);
    gtk_box_pack_end(GTK_BOX(static_hdr), ctx->static_grid.add_folder_btn, FALSE, FALSE, 0);

    /* Inner page stack: empty-state ↔ folders ↔ gallery */
    ctx->static_grid.page_stack = gtk_stack_new();
    ctx->static_grid.is_video = false;
    gtk_stack_set_transition_type(GTK_STACK(ctx->static_grid.page_stack), GTK_STACK_TRANSITION_TYPE_CROSSFADE);
    gtk_stack_set_transition_duration(GTK_STACK(ctx->static_grid.page_stack), 180);
    gtk_box_pack_start(GTK_BOX(static_page), ctx->static_grid.page_stack, TRUE, TRUE, 0);

    /* 1. Empty state */
    ctx->static_grid.empty_state = create_empty_state(
        "image-x-generic-symbolic",
        "No Image Folders Added",
        "Click 'Add Folder...' above to add folders\nof JPG, PNG, WebP, SVG or GIF images.");
    gtk_stack_add_named(GTK_STACK(ctx->static_grid.page_stack), ctx->static_grid.empty_state, "empty");

    /* 2. Folders root view scroll */
    GtkWidget *static_folders_scr = gtk_scrolled_window_new(NULL, NULL);
    gtk_style_context_add_class(gtk_widget_get_style_context(static_folders_scr), "gallery-scroll");
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(static_folders_scr), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_stack_add_named(GTK_STACK(ctx->static_grid.page_stack), static_folders_scr, "folders");

    ctx->static_grid.folders_flow_box = gtk_flow_box_new();
    gtk_widget_set_valign(ctx->static_grid.folders_flow_box, GTK_ALIGN_START);
    gtk_flow_box_set_max_children_per_line(GTK_FLOW_BOX(ctx->static_grid.folders_flow_box), 6);
    gtk_flow_box_set_min_children_per_line(GTK_FLOW_BOX(ctx->static_grid.folders_flow_box), 2);
    gtk_flow_box_set_selection_mode(GTK_FLOW_BOX(ctx->static_grid.folders_flow_box), GTK_SELECTION_NONE);
    gtk_flow_box_set_column_spacing(GTK_FLOW_BOX(ctx->static_grid.folders_flow_box), 10);
    gtk_flow_box_set_row_spacing(GTK_FLOW_BOX(ctx->static_grid.folders_flow_box), 10);
    gtk_container_set_border_width(GTK_CONTAINER(ctx->static_grid.folders_flow_box), 12);
    gtk_container_add(GTK_CONTAINER(static_folders_scr), ctx->static_grid.folders_flow_box);

    /* 3. Items Gallery scroll */
    GtkWidget *static_gallery_scr = gtk_scrolled_window_new(NULL, NULL);
    gtk_style_context_add_class(gtk_widget_get_style_context(static_gallery_scr), "gallery-scroll");
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(static_gallery_scr), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_stack_add_named(GTK_STACK(ctx->static_grid.page_stack), static_gallery_scr, "gallery");

    ctx->static_grid.gallery_flow_box = gtk_flow_box_new();
    gtk_widget_set_valign(ctx->static_grid.gallery_flow_box, GTK_ALIGN_START);
    gtk_flow_box_set_max_children_per_line(GTK_FLOW_BOX(ctx->static_grid.gallery_flow_box), 8);
    gtk_flow_box_set_min_children_per_line(GTK_FLOW_BOX(ctx->static_grid.gallery_flow_box), 2);
    gtk_flow_box_set_selection_mode(GTK_FLOW_BOX(ctx->static_grid.gallery_flow_box), GTK_SELECTION_NONE);
    gtk_flow_box_set_column_spacing(GTK_FLOW_BOX(ctx->static_grid.gallery_flow_box), 8);
    gtk_flow_box_set_row_spacing(GTK_FLOW_BOX(ctx->static_grid.gallery_flow_box), 8);
    gtk_container_set_border_width(GTK_CONTAINER(ctx->static_grid.gallery_flow_box), 10);
    gtk_container_add(GTK_CONTAINER(static_gallery_scr), ctx->static_grid.gallery_flow_box);

    /* Default: show empty state */
    gtk_stack_set_visible_child_name(GTK_STACK(ctx->static_grid.page_stack), "empty");

    /* ═══════════════════════════════════════════════════════════════════════
     * SHARED BOTTOM CONTROLS FOOTER
     * ═══════════════════════════════════════════════════════════════════════ */
    GtkWidget *bottom_bar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_style_context_add_class(gtk_widget_get_style_context(bottom_bar), "footer-bar");
    gtk_box_pack_end(GTK_BOX(content_vbox), bottom_bar, FALSE, FALSE, 0);

    ctx->status_label = gtk_label_new("Select a wallpaper to set as desktop background.");
    gtk_label_set_ellipsize(GTK_LABEL(ctx->status_label), PANGO_ELLIPSIZE_END);
    gtk_label_set_xalign(GTK_LABEL(ctx->status_label), 0.0);
    gtk_box_pack_start(GTK_BOX(bottom_bar), ctx->status_label, TRUE, TRUE, 0);

    ctx->pause_btn = gtk_button_new_with_label("Pause");
    gtk_button_set_image(GTK_BUTTON(ctx->pause_btn), gtk_image_new_from_icon_name("media-playback-pause-symbolic", GTK_ICON_SIZE_BUTTON));
    gtk_button_set_always_show_image(GTK_BUTTON(ctx->pause_btn), TRUE);
    g_signal_connect(ctx->pause_btn, "clicked", G_CALLBACK(on_pause_toggled), ctx);
    gtk_box_pack_start(GTK_BOX(bottom_bar), ctx->pause_btn, FALSE, FALSE, 0);

    ctx->stop_btn = gtk_button_new_with_label("Turn Off");
    gtk_button_set_image(GTK_BUTTON(ctx->stop_btn), gtk_image_new_from_icon_name("media-playback-stop-symbolic", GTK_ICON_SIZE_BUTTON));
    gtk_button_set_always_show_image(GTK_BUTTON(ctx->stop_btn), TRUE);
    g_signal_connect(ctx->stop_btn, "clicked", G_CALLBACK(on_stop_clicked), ctx);
    gtk_box_pack_start(GTK_BOX(bottom_bar), ctx->stop_btn, FALSE, FALSE, 0);

    GtkWidget *quit_btn = gtk_button_new_with_label("Quit");
    gtk_button_set_image(GTK_BUTTON(quit_btn), gtk_image_new_from_icon_name("application-exit-symbolic", GTK_ICON_SIZE_BUTTON));
    gtk_button_set_always_show_image(GTK_BUTTON(quit_btn), TRUE);
    g_signal_connect(quit_btn, "clicked", G_CALLBACK(on_quit_clicked), ctx);
    gtk_box_pack_start(GTK_BOX(bottom_bar), quit_btn, FALSE, FALSE, 0);

    gtk_widget_show_all(ctx->window);
    /* After show_all, explicitly set both grids to empty state.
     * populate_*_grid() will switch to "gallery" on the next idle tick.  */
    gtk_stack_set_visible_child_name(GTK_STACK(ctx->live_grid.page_stack),   "empty");
    gtk_stack_set_visible_child_name(GTK_STACK(ctx->static_grid.page_stack), "empty");

    /* GSettings live synchronization for desktop background */
    GSettingsSchemaSource *source = g_settings_schema_source_get_default();
    if (source) {
        GSettingsSchema *schema = g_settings_schema_source_lookup(source, "org.gnome.desktop.background", TRUE);
        if (schema) {
            g_settings_schema_unref(schema);
            ctx->bg_settings = g_settings_new("org.gnome.desktop.background");
            if (ctx->bg_settings) {
                g_signal_connect(ctx->bg_settings, "changed::picture-uri", G_CALLBACK(on_gnome_bg_changed), ctx);
                g_signal_connect(ctx->bg_settings, "changed::picture-uri-dark", G_CALLBACK(on_gnome_bg_changed), ctx);
            }
        }
    }

    /*
     * Restore previously saved folders AFTER the window is fully realized.
     *
     * Why defer with g_idle_add?
     * GtkStack resets its visible child to the first added child ("empty")
     * during the realization pass triggered by gtk_widget_show_all(). Any
     * gtk_stack_set_visible_child_name("gallery") call made BEFORE the stack
     * is realized is silently lost. By deferring to the first idle slot we
     * guarantee the stack is fully mapped before we switch pages.
     */
    g_idle_add(on_restore_folders_idle, ctx);

    return ctx;
}

void gui_destroy(GuiCtx *ctx)
{
    if (!ctx) return;
    gui_stop_render_timer(ctx);
    if (ctx->bg_settings) {
        g_object_unref(ctx->bg_settings);
        ctx->bg_settings = NULL;
    }
    if (ctx->tray_icon) {
        gtk_status_icon_set_visible(ctx->tray_icon, FALSE);
        g_object_unref(ctx->tray_icon);
        ctx->tray_icon = NULL;
    }
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
    /* Default: start at 60fps (16ms) for pre-ready state */
    gui_start_render_timer_at_fps(ctx, 60.0);
}

static void gui_start_render_timer_at_fps(GuiCtx *ctx, double fps)
{
    if (!ctx) return;

    /* Clamp: minimum 8ms (125fps cap), maximum 100ms (10fps floor) */
    if (fps <= 0.0) fps = 30.0;
    guint interval_ms = (guint)(1000.0 / fps);
    if (interval_ms < 8)   interval_ms = 8;
    if (interval_ms > 100) interval_ms = 100;

    /* Stop old timer first */
    if (ctx->render_timer_id != 0) {
        g_source_remove(ctx->render_timer_id);
        ctx->render_timer_id = 0;
    }

    ctx->render_fps       = fps;
    ctx->render_timer_id  = g_timeout_add(interval_ms, on_render_tick, ctx);
}

void gui_stop_render_timer(GuiCtx *ctx)
{
    if (!ctx) return;
    if (ctx->render_timer_id != 0) {
        g_source_remove(ctx->render_timer_id);
        ctx->render_timer_id = 0;
    }
    ctx->render_fps = 0.0;
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

    int fidx = -1;
    for (int i = 0; i < ctx->live_grid.folder_count; i++) {
        if (strcmp(ctx->live_grid.folders[i], dirname) == 0) {
            fidx = i;
            break;
        }
    }
    if (fidx < 0 && ctx->live_grid.folder_count < MAX_FOLDERS) {
        snprintf(ctx->live_grid.folders[ctx->live_grid.folder_count], LW_MAX_PATH, "%s", dirname);
        fidx = ctx->live_grid.folder_count;
        ctx->live_grid.folder_count++;
        config_save_live_folders(ctx->live_grid.folders, ctx->live_grid.folder_count);
    }

    start_live_wallpaper(ctx, filepath);
    if (fidx >= 0) {
        open_folder_view(ctx, &ctx->live_grid, fidx);
    }
    g_free(dirname);
}
