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

#include "gui.h"
#include "decoder.h"
#include "config.h"
#include "thumbnail.h"
#include "static_wallpaper.h"

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
"}";

/* Structure to manage a grid view (Live or Static) */
typedef struct {
    GtkWidget *flow_box;
    GtkWidget *folder_label;
    GtkWidget *page_stack;      /* Switches between empty_state and gallery */
    GtkWidget *empty_state;     /* Shown when no folder is loaded */
    char       folder_path[LW_MAX_PATH];
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
    GtkWidget    *status_label;
    GtkWidget    *pause_btn;
    GtkWidget    *stop_btn;

    /* Live video grid */
    GridView      live_grid;

    /* Static image grid */
    GridView      static_grid;
    char          active_static_path[LW_MAX_PATH];

    guint         render_timer_id;
};

/* Forward declarations */
static gboolean on_render_tick(gpointer user_data);
static void on_select_video_folder_clicked(GtkButton *button, gpointer user_data);
static void on_select_image_folder_clicked(GtkButton *button, gpointer user_data);
static void on_live_card_clicked(GtkButton *button, gpointer user_data);
static void on_static_card_clicked(GtkButton *button, gpointer user_data);
static void on_nav_tab_clicked(GtkButton *button, gpointer user_data);
static void on_pause_toggled(GtkButton *button, gpointer user_data);
static void on_stop_clicked(GtkButton *button, gpointer user_data);
static void on_quit_clicked(GtkButton *button, gpointer user_data);
static gboolean on_window_delete_event(GtkWidget *widget, GdkEvent *event, gpointer user_data);

static void populate_live_grid(GuiCtx *ctx, const char *folder, const char *active_file);
static void populate_static_grid(GuiCtx *ctx, const char *folder, const char *active_file);
static void update_grid_visuals(GridView *grid, int active_idx);
static void start_live_wallpaper(GuiCtx *ctx, const char *filepath);
static void stop_live_wallpaper(GuiCtx *ctx);
static void apply_static_wallpaper(GuiCtx *ctx, const char *filepath);

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
    wallpaper_hide(ctx->wallpaper);
    update_grid_visuals(&ctx->live_grid, -1);

    if (static_wallpaper_apply(filepath)) {
        snprintf(ctx->active_static_path, sizeof(ctx->active_static_path), "%s", filepath);

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
 * Populate Live Video Grid
 * ──────────────────────────────────────────────────────────────────────────── */
static void populate_live_grid(GuiCtx *ctx, const char *folder, const char *active_file)
{
    if (!folder || !ctx->live_grid.flow_box) return;

    GList *children = gtk_container_get_children(GTK_CONTAINER(ctx->live_grid.flow_box));
    for (GList *l = children; l != NULL; l = l->next) {
        gtk_widget_destroy(GTK_WIDGET(l->data));
    }
    g_list_free(children);

    ctx->live_grid.count = 0;
    ctx->live_grid.active_idx = -1;
    /* MEM-3: zero stale widget pointers so update_grid_visuals never dereferences freed widgets */
    memset(ctx->live_grid.card_widgets,  0, sizeof(ctx->live_grid.card_widgets));
    memset(ctx->live_grid.badge_widgets, 0, sizeof(ctx->live_grid.badge_widgets));
    memset(ctx->live_grid.title_widgets, 0, sizeof(ctx->live_grid.title_widgets));
    snprintf(ctx->live_grid.folder_path, sizeof(ctx->live_grid.folder_path), "%s", folder);

    /* Persist this folder so we can restore it next launch */
    config_save_live_folder(folder);

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
            gtk_stack_set_visible_child_name(GTK_STACK(ctx->live_grid.page_stack), "empty");
        return;
    }

    /* Switch to grid view */
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

        GdkPixbuf *thumb = thumbnail_generate(fpath, THUMB_WIDTH, THUMB_HEIGHT);
        GtkWidget *img = thumb ? gtk_image_new_from_pixbuf(thumb)
                               : gtk_image_new_from_icon_name("video-x-generic", GTK_ICON_SIZE_DIALOG);
        if (thumb) g_object_unref(thumb);
        gtk_container_add(GTK_CONTAINER(overlay), img);

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

        gtk_flow_box_insert(GTK_FLOW_BOX(ctx->live_grid.flow_box), card, -1);
        ctx->live_grid.count++;
        g_free(fpath);
    }

    g_list_free_full(filenames, g_free);
    gtk_widget_show_all(ctx->live_grid.flow_box);
    update_grid_visuals(&ctx->live_grid, target_active);
}

/* ─────────────────────────────────────────────────────────────────────────────
 * Populate Static Image Grid
 * ──────────────────────────────────────────────────────────────────────────── */
static void populate_static_grid(GuiCtx *ctx, const char *folder, const char *active_file)
{
    if (!folder || !ctx->static_grid.flow_box) return;

    GList *children = gtk_container_get_children(GTK_CONTAINER(ctx->static_grid.flow_box));
    for (GList *l = children; l != NULL; l = l->next) {
        gtk_widget_destroy(GTK_WIDGET(l->data));
    }
    g_list_free(children);

    ctx->static_grid.count = 0;
    ctx->static_grid.active_idx = -1;
    /* MEM-3: zero stale widget pointers so update_grid_visuals never dereferences freed widgets */
    memset(ctx->static_grid.card_widgets,  0, sizeof(ctx->static_grid.card_widgets));
    memset(ctx->static_grid.badge_widgets, 0, sizeof(ctx->static_grid.badge_widgets));
    memset(ctx->static_grid.title_widgets, 0, sizeof(ctx->static_grid.title_widgets));
    snprintf(ctx->static_grid.folder_path, sizeof(ctx->static_grid.folder_path), "%s", folder);

    /* Persist this folder so we can restore it next launch */
    config_save_static_folder(folder);

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
            gtk_stack_set_visible_child_name(GTK_STACK(ctx->static_grid.page_stack), "empty");
        return;
    }

    /* Switch to grid view */
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

        GdkPixbuf *thumb = thumbnail_generate(fpath, THUMB_WIDTH, THUMB_HEIGHT);
        GtkWidget *img = thumb ? gtk_image_new_from_pixbuf(thumb)
                               : gtk_image_new_from_icon_name("image-x-generic", GTK_ICON_SIZE_DIALOG);
        if (thumb) g_object_unref(thumb);
        gtk_container_add(GTK_CONTAINER(overlay), img);

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

        gtk_flow_box_insert(GTK_FLOW_BOX(ctx->static_grid.flow_box), card, -1);
        ctx->static_grid.count++;
        g_free(fpath);
    }

    g_list_free_full(filenames, g_free);
    gtk_widget_show_all(ctx->static_grid.flow_box);
    update_grid_visuals(&ctx->static_grid, target_active);
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
 * Folder Choosers
 * ──────────────────────────────────────────────────────────────────────────── */
static void on_select_video_folder_clicked(GtkButton *button, gpointer user_data)
{
    (void)button;
    GuiCtx *ctx = (GuiCtx *)user_data;

    GtkWidget *dialog = gtk_file_chooser_dialog_new(
        "Select Live Video Wallpapers Folder",
        GTK_WINDOW(ctx->window),
        GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER,
        "_Cancel", GTK_RESPONSE_CANCEL,
        "_Select Folder", GTK_RESPONSE_ACCEPT,
        NULL);

    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
        char *folder = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));
        if (folder) {
            populate_live_grid(ctx, folder, NULL);
            g_free(folder);
        }
    }
    gtk_widget_destroy(dialog);
}

static void on_select_image_folder_clicked(GtkButton *button, gpointer user_data)
{
    (void)button;
    GuiCtx *ctx = (GuiCtx *)user_data;

    GtkWidget *dialog = gtk_file_chooser_dialog_new(
        "Select Static Image Wallpapers Folder",
        GTK_WINDOW(ctx->window),
        GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER,
        "_Cancel", GTK_RESPONSE_CANCEL,
        "_Select Folder", GTK_RESPONSE_ACCEPT,
        NULL);

    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
        char *folder = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));
        if (folder) {
            populate_static_grid(ctx, folder, NULL);
            g_free(folder);
        }
    }
    gtk_widget_destroy(dialog);
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
    gtk_main_quit();
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
        gtk_main_quit();
        return G_SOURCE_REMOVE;
    }

    if (atomic_load(&ctx->state->quit)) {
        return G_SOURCE_REMOVE;
    }

    /* UX-4: Once the decoder signals ready, update the status bar with accurate
     * video metadata.  We do this here (on the GTK thread) to avoid any race. */
    if (atomic_load(&ctx->state->decoder_ready)) {
        /* Check if status still shows the loading placeholder */
        const char *cur = gtk_label_get_text(GTK_LABEL(ctx->status_label));
        if (cur && strstr(cur, "Loading live wallpaper")) {
            int w = atomic_load(&ctx->state->video_width);
            int h = atomic_load(&ctx->state->video_height);
            double fps = ctx->state->video_fps;
            char *base = g_path_get_basename(ctx->state->video_path);
            char buf[512];
            snprintf(buf, sizeof(buf), "🎬 Live Wallpaper: %s (%dx%d @ %.0ffps)",
                     base, w, h, fps > 0 ? fps : 30.0);
            gui_set_status(ctx, buf);
            g_free(base);
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

/* ═══════════════════════════════════════════════════════════════════════════
 * Deferred folder restore (runs after window is fully realized)
 * ═══════════════════════════════════════════════════════════════════════════ */
static gboolean on_restore_folders_idle(gpointer user_data)
{
    GuiCtx *ctx = (GuiCtx *)user_data;

    char saved_live[LW_MAX_PATH] = {0};
    if (config_load_live_folder(saved_live, sizeof(saved_live)) &&
        saved_live[0] != '\0' &&
        g_file_test(saved_live, G_FILE_TEST_IS_DIR)) {
        populate_live_grid(ctx, saved_live, NULL);
    }

    char saved_static[LW_MAX_PATH] = {0};
    if (config_load_static_folder(saved_static, sizeof(saved_static)) &&
        saved_static[0] != '\0' &&
        g_file_test(saved_static, G_FILE_TEST_IS_DIR)) {
        populate_static_grid(ctx, saved_static, NULL);
    }

    return G_SOURCE_REMOVE; /* one-shot */
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
    gtk_window_set_title(GTK_WINDOW(ctx->window), "Live Wallpaper Studio");
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

    gtk_style_context_add_class(gtk_widget_get_style_context(ctx->window), "studio-window");
    g_signal_connect(ctx->window, "delete-event", G_CALLBACK(on_window_delete_event), ctx);

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

    GtkWidget *brand_title = gtk_label_new("Wallpaper");
    gtk_style_context_add_class(gtk_widget_get_style_context(brand_title), "sidebar-title");
    gtk_label_set_xalign(GTK_LABEL(brand_title), 0.0);
    gtk_box_pack_start(GTK_BOX(brand_lbl_box), brand_title, FALSE, FALSE, 0);

    GtkWidget *brand_sub = gtk_label_new("Studio for Zorin OS");
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
    GtkWidget *live_hdr = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_style_context_add_class(gtk_widget_get_style_context(live_hdr), "header-bar");
    gtk_box_pack_start(GTK_BOX(live_page), live_hdr, FALSE, FALSE, 0);

    ctx->live_grid.folder_label = gtk_label_new("📁 No video folder loaded");
    gtk_label_set_use_markup(GTK_LABEL(ctx->live_grid.folder_label), TRUE);
    gtk_label_set_ellipsize(GTK_LABEL(ctx->live_grid.folder_label), PANGO_ELLIPSIZE_MIDDLE);
    gtk_label_set_xalign(GTK_LABEL(ctx->live_grid.folder_label), 0.0);
    gtk_box_pack_start(GTK_BOX(live_hdr), ctx->live_grid.folder_label, TRUE, TRUE, 0);

    GtkWidget *live_fldr_btn = gtk_button_new_with_label("Select Folder...");
    gtk_button_set_image(GTK_BUTTON(live_fldr_btn), gtk_image_new_from_icon_name("folder-open-symbolic", GTK_ICON_SIZE_BUTTON));
    gtk_button_set_always_show_image(GTK_BUTTON(live_fldr_btn), TRUE);
    g_signal_connect(live_fldr_btn, "clicked", G_CALLBACK(on_select_video_folder_clicked), ctx);
    gtk_box_pack_end(GTK_BOX(live_hdr), live_fldr_btn, FALSE, FALSE, 0);

    /* Inner page stack: empty-state ↔ gallery */
    ctx->live_grid.page_stack = gtk_stack_new();
    gtk_stack_set_transition_type(GTK_STACK(ctx->live_grid.page_stack), GTK_STACK_TRANSITION_TYPE_CROSSFADE);
    gtk_stack_set_transition_duration(GTK_STACK(ctx->live_grid.page_stack), 180);
    gtk_box_pack_start(GTK_BOX(live_page), ctx->live_grid.page_stack, TRUE, TRUE, 0);

    /* Empty state */
    ctx->live_grid.empty_state = create_empty_state(
        "video-x-generic-symbolic",
        "No Videos Loaded",
        "Click 'Select Folder...' above to import a folder\nfull of video wallpapers.");
    gtk_stack_add_named(GTK_STACK(ctx->live_grid.page_stack), ctx->live_grid.empty_state, "empty");

    /* Gallery scroll */
    GtkWidget *live_scr = gtk_scrolled_window_new(NULL, NULL);
    gtk_style_context_add_class(gtk_widget_get_style_context(live_scr), "gallery-scroll");
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(live_scr), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_stack_add_named(GTK_STACK(ctx->live_grid.page_stack), live_scr, "gallery");

    ctx->live_grid.flow_box = gtk_flow_box_new();
    gtk_widget_set_valign(ctx->live_grid.flow_box, GTK_ALIGN_START);
    gtk_flow_box_set_max_children_per_line(GTK_FLOW_BOX(ctx->live_grid.flow_box), 8);
    gtk_flow_box_set_min_children_per_line(GTK_FLOW_BOX(ctx->live_grid.flow_box), 2);
    gtk_flow_box_set_selection_mode(GTK_FLOW_BOX(ctx->live_grid.flow_box), GTK_SELECTION_NONE);
    gtk_flow_box_set_column_spacing(GTK_FLOW_BOX(ctx->live_grid.flow_box), 8);
    gtk_flow_box_set_row_spacing(GTK_FLOW_BOX(ctx->live_grid.flow_box), 8);
    gtk_container_set_border_width(GTK_CONTAINER(ctx->live_grid.flow_box), 10);
    gtk_container_add(GTK_CONTAINER(live_scr), ctx->live_grid.flow_box);

    /* Default: show empty state */
    gtk_stack_set_visible_child_name(GTK_STACK(ctx->live_grid.page_stack), "empty");

    /* ───────────────────────────────────────────────────────────────────────
     * Page 2: Static Wallpaper View
     * ─────────────────────────────────────────────────────────────────────── */
    GtkWidget *static_page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_stack_add_named(GTK_STACK(ctx->stack), static_page, "static");

    /* Static Header Bar */
    GtkWidget *static_hdr = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_style_context_add_class(gtk_widget_get_style_context(static_hdr), "header-bar");
    gtk_box_pack_start(GTK_BOX(static_page), static_hdr, FALSE, FALSE, 0);

    ctx->static_grid.folder_label = gtk_label_new("📁 No image folder loaded");
    gtk_label_set_use_markup(GTK_LABEL(ctx->static_grid.folder_label), TRUE);
    gtk_label_set_ellipsize(GTK_LABEL(ctx->static_grid.folder_label), PANGO_ELLIPSIZE_MIDDLE);
    gtk_label_set_xalign(GTK_LABEL(ctx->static_grid.folder_label), 0.0);
    gtk_box_pack_start(GTK_BOX(static_hdr), ctx->static_grid.folder_label, TRUE, TRUE, 0);

    GtkWidget *static_fldr_btn = gtk_button_new_with_label("Select Folder...");
    gtk_button_set_image(GTK_BUTTON(static_fldr_btn), gtk_image_new_from_icon_name("folder-open-symbolic", GTK_ICON_SIZE_BUTTON));
    gtk_button_set_always_show_image(GTK_BUTTON(static_fldr_btn), TRUE);
    g_signal_connect(static_fldr_btn, "clicked", G_CALLBACK(on_select_image_folder_clicked), ctx);
    gtk_box_pack_end(GTK_BOX(static_hdr), static_fldr_btn, FALSE, FALSE, 0);

    /* Inner page stack: empty-state ↔ gallery */
    ctx->static_grid.page_stack = gtk_stack_new();
    gtk_stack_set_transition_type(GTK_STACK(ctx->static_grid.page_stack), GTK_STACK_TRANSITION_TYPE_CROSSFADE);
    gtk_stack_set_transition_duration(GTK_STACK(ctx->static_grid.page_stack), 180);
    gtk_box_pack_start(GTK_BOX(static_page), ctx->static_grid.page_stack, TRUE, TRUE, 0);

    /* Empty state */
    ctx->static_grid.empty_state = create_empty_state(
        "image-x-generic-symbolic",
        "No Images Loaded",
        "Click 'Select Folder...' above to import a folder\nof JPG, PNG, WebP, SVG or GIF images.");
    gtk_stack_add_named(GTK_STACK(ctx->static_grid.page_stack), ctx->static_grid.empty_state, "empty");

    /* Gallery scroll */
    GtkWidget *static_scr = gtk_scrolled_window_new(NULL, NULL);
    gtk_style_context_add_class(gtk_widget_get_style_context(static_scr), "gallery-scroll");
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(static_scr), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_stack_add_named(GTK_STACK(ctx->static_grid.page_stack), static_scr, "gallery");

    ctx->static_grid.flow_box = gtk_flow_box_new();
    gtk_widget_set_valign(ctx->static_grid.flow_box, GTK_ALIGN_START);
    gtk_flow_box_set_max_children_per_line(GTK_FLOW_BOX(ctx->static_grid.flow_box), 8);
    gtk_flow_box_set_min_children_per_line(GTK_FLOW_BOX(ctx->static_grid.flow_box), 2);
    gtk_flow_box_set_selection_mode(GTK_FLOW_BOX(ctx->static_grid.flow_box), GTK_SELECTION_NONE);
    gtk_flow_box_set_column_spacing(GTK_FLOW_BOX(ctx->static_grid.flow_box), 8);
    gtk_flow_box_set_row_spacing(GTK_FLOW_BOX(ctx->static_grid.flow_box), 8);
    gtk_container_set_border_width(GTK_CONTAINER(ctx->static_grid.flow_box), 10);
    gtk_container_add(GTK_CONTAINER(static_scr), ctx->static_grid.flow_box);

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
    start_live_wallpaper(ctx, filepath);
    populate_live_grid(ctx, dirname, filepath);
    g_free(dirname);
}
