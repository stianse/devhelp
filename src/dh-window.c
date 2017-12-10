/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*- */
/*
 * Copyright (C) 2001-2008 Imendio AB
 * Copyright (C) 2012 Aleksander Morgado <aleksander@gnu.org>
 * Copyright (C) 2012 Thomas Bechtold <toabctl@gnome.org>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include "config.h"
#include "dh-window.h"

#include <string.h>
#include <math.h>
#include <glib/gi18n-lib.h>
#include <gdk/gdkkeysyms.h>
#include <webkit2/webkit2.h>

#include "dh-book-manager.h"
#include "dh-book.h"
#include "dh-sidebar.h"
#include "dh-util.h"
#include "dh-settings.h"
#include "dh-enum-types-app.h"

typedef struct {
        GMenuModel     *gear_app_menu;
        GtkPaned       *hpaned;
        DhSidebar      *sidebar;
        GtkNotebook    *notebook;
        GtkHeaderBar   *header_bar;
        GtkMenuButton  *gear_menu_button;
        GtkWidget      *grid_sidebar;
        GtkSearchBar   *search_bar;
        GtkSearchEntry *search_entry;
        GtkButton      *go_up_button;
        GtkButton      *go_down_button;

        DhLink         *selected_search_link;
        guint           configure_id;
} DhWindowPrivate;

enum {
        OPEN_LINK,
        N_SIGNALS
};

static guint signals[N_SIGNALS] = { 0 };

#define WINDOW_SETTINGS_SAVE_TIMEOUT_MSECS 100

static guint tab_accel_keys[] = {
        GDK_KEY_1, GDK_KEY_2, GDK_KEY_3, GDK_KEY_4, GDK_KEY_5,
        GDK_KEY_6, GDK_KEY_7, GDK_KEY_8, GDK_KEY_9, GDK_KEY_0
};

static const
struct
{
        const gchar *name;
        double level;
}
zoom_levels[] =
{
        { N_("50%"), 0.5 },
        { N_("75%"), 0.8408964152 },
        { N_("100%"), 1.0 },
        { N_("125%"), 1.1892071149 },
        { N_("150%"), 1.4142135623 },
        { N_("175%"), 1.6817928304 },
        { N_("200%"), 2.0 },
        { N_("300%"), 2.8284271247 },
        { N_("400%"), 4.0 }
};

static const guint n_zoom_levels = G_N_ELEMENTS (zoom_levels);

#define ZOOM_MINIMAL    (zoom_levels[0].level)
#define ZOOM_MAXIMAL    (zoom_levels[n_zoom_levels - 1].level)
#define ZOOM_DEFAULT    (zoom_levels[2].level)

static void           window_search_link_selected_cb (GObject         *ignored,
                                                      DhLink          *link,
                                                      DhWindow        *window);
static void           window_web_view_tab_accel_cb   (GtkAccelGroup   *accel_group,
                                                      GObject         *object,
                                                      guint            key,
                                                      GdkModifierType  mod,
                                                      DhWindow        *window);
static void           window_find_search_changed_cb  (GtkEntry        *entry,
                                                      DhWindow        *window);
static void           window_find_next_cb            (GtkWidget       *widget,
                                                      DhWindow        *window);
static void           findbar_find_next              (DhWindow        *window);
static void           window_find_previous_cb        (GtkWidget       *widget,
                                                      DhWindow        *window);
static void           findbar_find_previous          (DhWindow        *window);
static void           on_search_mode_enabled_changed (GtkSearchBar    *search_bar,
                                                      GParamSpec      *pspec,
                                                      DhWindow        *window);
static void           on_search_entry_activated      (GtkEntry        *entry,
                                                      DhWindow        *window);
static gboolean       on_search_entry_key_press      (GtkEntry    *entry,
                                                      GdkEventKey *event,
                                                      DhWindow    *window);
static GtkWidget *    window_new_tab_label           (DhWindow        *window,
                                                      const gchar     *label,
                                                      const GtkWidget *parent);
static void           window_open_new_tab            (DhWindow        *window,
                                                      const gchar     *location,
                                                      gboolean         switch_focus);
static WebKitWebView *window_get_active_web_view     (DhWindow        *window);
static GtkWidget *    window_get_active_info_bar     (DhWindow *window);
static void           window_update_title            (DhWindow        *window,
                                                      WebKitWebView   *web_view,
                                                      const gchar     *title);
static void           window_tab_set_title           (DhWindow        *window,
                                                      WebKitWebView   *web_view,
                                                      const gchar     *title);
static void           do_search                      (DhWindow *window);

G_DEFINE_TYPE_WITH_PRIVATE (DhWindow, dh_window, GTK_TYPE_APPLICATION_WINDOW);

static void
new_tab_cb (GSimpleAction *action,
            GVariant      *parameter,
            gpointer       user_data)
{
        DhWindow *window = user_data;

        window_open_new_tab (window, NULL, TRUE);
}

static void
next_tab_cb (GSimpleAction *action,
             GVariant      *parameter,
             gpointer       user_data)
{
        gint current_page, n_pages;
        DhWindowPrivate *priv;
        DhWindow *window = user_data;

        priv = dh_window_get_instance_private (window);

        current_page = gtk_notebook_get_current_page (priv->notebook);
        n_pages = gtk_notebook_get_n_pages (priv->notebook);

        if (current_page < n_pages - 1)
                gtk_notebook_next_page (priv->notebook);
        else
                /* Wrap around to the first tab */
                gtk_notebook_set_current_page (priv->notebook, 0);
}

static void
prev_tab_cb (GSimpleAction *action,
             GVariant      *parameter,
             gpointer       user_data)
{
        gint current_page;
        DhWindowPrivate *priv;
        DhWindow *window = user_data;

        priv = dh_window_get_instance_private (window);

        current_page = gtk_notebook_get_current_page (priv->notebook);

        if (current_page > 0)
                gtk_notebook_prev_page (priv->notebook);
        else
                /* Wrap around to the last tab */
                gtk_notebook_set_current_page (priv->notebook, -1);
}

static void
print_cb (GSimpleAction *action,
          GVariant      *parameter,
          gpointer       user_data)
{
        DhWindow *window = user_data;
        WebKitWebView *web_view = window_get_active_web_view (window);
        WebKitPrintOperation *print_operation;

        print_operation = webkit_print_operation_new (web_view);
        webkit_print_operation_run_dialog (print_operation, GTK_WINDOW (window));
        g_object_unref (print_operation);
}

static void
window_close_tab (DhWindow *window,
                  gint      page_num)
{
        DhWindowPrivate *priv;
        gint          pages;

        priv = dh_window_get_instance_private (window);

        gtk_notebook_remove_page (priv->notebook, page_num);

        pages = gtk_notebook_get_n_pages (priv->notebook);

        if (pages == 0)
                gtk_widget_destroy (GTK_WIDGET (window));
        else if (pages == 1)
                gtk_notebook_set_show_tabs (priv->notebook, FALSE);
}

static void
close_cb (GSimpleAction *action,
          GVariant      *parameter,
          gpointer       user_data)
{
        DhWindow *window = user_data;
        DhWindowPrivate *priv;
        gint page_num;

        priv = dh_window_get_instance_private (window);

        page_num = gtk_notebook_get_current_page (priv->notebook);
        window_close_tab (window, page_num);
}

static void
copy_cb (GSimpleAction *action,
         GVariant      *parameter,
         gpointer       user_data)
{
        DhWindow *window = user_data;
        GtkWidget *widget;
        DhWindowPrivate *priv;

        priv = dh_window_get_instance_private (window);

        widget = gtk_window_get_focus (GTK_WINDOW (window));

        if (GTK_IS_EDITABLE (widget)) {
                gtk_editable_copy_clipboard (GTK_EDITABLE (widget));
        } else if (GTK_IS_TREE_VIEW (widget) &&
                   gtk_widget_is_ancestor (widget, GTK_WIDGET (priv->sidebar)) &&
                   priv->selected_search_link != NULL) {
                GtkClipboard *clipboard;
                clipboard = gtk_widget_get_clipboard (widget, GDK_SELECTION_CLIPBOARD);
                gtk_clipboard_set_text (clipboard,
                                        dh_link_get_name (priv->selected_search_link),
                                        -1);
        } else {
                WebKitWebView *web_view;

                web_view = window_get_active_web_view (window);
                webkit_web_view_execute_editing_command (web_view, WEBKIT_EDITING_COMMAND_COPY);
        }
}

static void
find_cb (GSimpleAction *action,
         GVariant      *parameter,
         gpointer       user_data)
{
        DhWindow *window = user_data;
        DhWindowPrivate *priv;

        priv = dh_window_get_instance_private (window);

        gtk_search_bar_set_search_mode (priv->search_bar, TRUE);
        gtk_widget_grab_focus (GTK_WIDGET (priv->search_entry));

        /* The behaviour for WebKit1 is to re-enable highlighting without
         * starting a new search. WebKit2 API does not allow that
         * without invoking a new search.
         */
        do_search (window);
}

static void
find_previous_cb (GSimpleAction *action,
                  GVariant      *parameter,
                  gpointer       user_data)
{
        findbar_find_previous (DH_WINDOW (user_data));
}

static void
find_next_cb (GSimpleAction *action,
              GVariant      *parameter,
              gpointer       user_data)
{
        findbar_find_next (DH_WINDOW (user_data));
}

static int
window_get_current_zoom_level_index (DhWindow *window)
{
        WebKitWebView *web_view;
        double previous;
        double zoom_level = ZOOM_DEFAULT;
        guint i;

        web_view = window_get_active_web_view (window);
        if (web_view != NULL)
                zoom_level = webkit_web_view_get_zoom_level (web_view);

        previous = zoom_levels[0].level;
        for (i = 1; i < n_zoom_levels; i++) {
                double current = zoom_levels[i].level;
                double mean = sqrt (previous * current);

                if (zoom_level <= mean)
                        return i - 1;

                previous = current;
        }

        return n_zoom_levels - 1;
}

static void
window_update_zoom_actions_state (DhWindow *window)
{
        GAction *action;
        int zoom_level_index;
        gboolean enabled;

        zoom_level_index = window_get_current_zoom_level_index (window);

        enabled = zoom_levels[zoom_level_index].level < ZOOM_MAXIMAL;
        action = g_action_map_lookup_action (G_ACTION_MAP (window), "zoom-in");
        g_simple_action_set_enabled (G_SIMPLE_ACTION (action), enabled);

        enabled = zoom_levels[zoom_level_index].level > ZOOM_MINIMAL;
        action = g_action_map_lookup_action (G_ACTION_MAP (window), "zoom-out");
        g_simple_action_set_enabled (G_SIMPLE_ACTION (action), enabled);

        enabled = zoom_levels[zoom_level_index].level != ZOOM_DEFAULT;
        action = g_action_map_lookup_action (G_ACTION_MAP (window), "zoom-default");
        g_simple_action_set_enabled (G_SIMPLE_ACTION (action), enabled);
}

static void
window_update_back_forward_actions_state (DhWindow *window)
{
        WebKitWebView *web_view;
        GAction *action;
        gboolean enabled;

        web_view = window_get_active_web_view (window);

        enabled = web_view != NULL ? webkit_web_view_can_go_back (web_view) : FALSE;
        action = g_action_map_lookup_action (G_ACTION_MAP (window), "go-back");
        g_simple_action_set_enabled (G_SIMPLE_ACTION (action), enabled);

        enabled = web_view != NULL ? webkit_web_view_can_go_forward (web_view) : FALSE;
        action = g_action_map_lookup_action (G_ACTION_MAP (window), "go-forward");
        g_simple_action_set_enabled (G_SIMPLE_ACTION (action), enabled);
}

static void
show_sidebar_cb (GSimpleAction *action,
                 GVariant      *parameter,
                 gpointer       user_data)
{
        DhWindow *window = user_data;
        DhWindowPrivate *priv = dh_window_get_instance_private (window);
        gboolean visible;

        visible = gtk_widget_get_visible (priv->grid_sidebar);

        g_simple_action_set_state (action, g_variant_new_boolean (!visible));
        gtk_widget_set_visible (priv->grid_sidebar, !visible);
}

static void
bump_zoom_level (DhWindow *window,
                 int bump_amount)
{
        int zoom_level_index;
        double new_zoom_level;

        if (bump_amount == 0)
                return;

        zoom_level_index = window_get_current_zoom_level_index (window);
        new_zoom_level = zoom_levels[zoom_level_index + bump_amount].level;

        if (new_zoom_level >= ZOOM_MINIMAL &&
                new_zoom_level <= ZOOM_MAXIMAL) {
                WebKitWebView *web_view;

                web_view = window_get_active_web_view (window);
                webkit_web_view_set_zoom_level (web_view, new_zoom_level);
                window_update_zoom_actions_state (window);
        }
}

static void
zoom_in_cb (GSimpleAction *action,
            GVariant      *parameter,
            gpointer       user_data)
{
        DhWindow *window = user_data;

        bump_zoom_level (window, 1);
}

static void
zoom_out_cb (GSimpleAction *action,
             GVariant      *parameter,
             gpointer       user_data)
{
        DhWindow *window = user_data;

        bump_zoom_level (window, -1);
}

static void
zoom_default_cb (GSimpleAction *action,
                 GVariant      *parameter,
                 gpointer       user_data)
{
        DhWindow *window = user_data;
        WebKitWebView *web_view;

        web_view = window_get_active_web_view (window);
        webkit_web_view_set_zoom_level (web_view, ZOOM_DEFAULT);
        window_update_zoom_actions_state (window);
}

static void
focus_search_cb (GSimpleAction *action,
                 GVariant      *parameter,
                 gpointer       user_data)
{
        DhWindow *window = user_data;
        DhWindowPrivate *priv;

        priv = dh_window_get_instance_private (window);

        dh_sidebar_set_search_focus (priv->sidebar);
}

static void
go_back_cb (GSimpleAction *action,
            GVariant      *parameter,
            gpointer       user_data)
{
        DhWindow      *window = user_data;
        DhWindowPrivate *priv;
        gint current_page_num;
        WebKitWebView *web_view;
        GtkWidget     *frame;

        priv = dh_window_get_instance_private (window);

        current_page_num = gtk_notebook_get_current_page (priv->notebook);
        frame = gtk_notebook_get_nth_page (priv->notebook, current_page_num);

        web_view = g_object_get_data (G_OBJECT (frame), "web_view");

        webkit_web_view_go_back (web_view);
}

static void
go_forward_cb (GSimpleAction *action,
               GVariant      *parameter,
               gpointer       user_data)
{
        DhWindow      *window = user_data;
        DhWindowPrivate *priv;
        gint current_page_num;
        WebKitWebView *web_view;
        GtkWidget     *frame;

        priv = dh_window_get_instance_private (window);

        current_page_num = gtk_notebook_get_current_page (priv->notebook);
        frame = gtk_notebook_get_nth_page (priv->notebook, current_page_num);

        web_view = g_object_get_data (G_OBJECT (frame), "web_view");

        webkit_web_view_go_forward (web_view);
}

static void
gear_menu_cb (GSimpleAction *action,
              GVariant      *parameter,
              gpointer       user_data)
{
        GVariant *state;

        state = g_action_get_state (G_ACTION (action));
        g_action_change_state (G_ACTION (action),
                               g_variant_new_boolean (!g_variant_get_boolean (state)));
        g_variant_unref (state);
}

static void
dh_window_open_link (DhWindow        *window,
                     const char      *location,
                     DhOpenLinkFlags  flags)
{
        switch (flags) {
                case DH_OPEN_LINK_NEW_WINDOW:
                        dh_app_new_window (DH_APP (gtk_window_get_application (GTK_WINDOW (window))));
                        break;

                case DH_OPEN_LINK_NEW_TAB:
                        window_open_new_tab (window, location, FALSE);
                        break;

                default:
                        g_assert_not_reached ();
        }
}

static GActionEntry win_entries[] = {
        /* tabs */
        { "new-tab",          new_tab_cb },
        { "next-tab",         next_tab_cb,         NULL, NULL, NULL },
        { "prev-tab",         prev_tab_cb,         NULL, NULL, NULL },
        { "print",            print_cb },
        { "close",            close_cb },

        /* edit */
        { "copy",             copy_cb },
        { "find",             find_cb },
        { "find-previous",    find_previous_cb },
        { "find-next",        find_next_cb },

        /* view */
        { "show-sidebar",     NULL, NULL, "true", show_sidebar_cb },
        { "zoom-in",          zoom_in_cb },
        { "zoom-out",         zoom_out_cb },
        { "zoom-default",     zoom_default_cb },
        { "focus-search",     focus_search_cb },

        /* go */
        { "go-back",          go_back_cb, NULL, "false" },
        { "go-forward",       go_forward_cb, NULL, "false" },

        /* gear menu */
        { "gear-menu",        gear_menu_cb, NULL, "false" },
};

static void
settings_fonts_changed_cb (DhSettings  *settings,
                           const gchar *font_name_fixed,
                           const gchar *font_name_variable,
                           gpointer     user_data)
{
        DhWindow *window = DH_WINDOW (user_data);
        DhWindowPrivate *priv;
        gint i;
        WebKitWebView *view;

        priv = dh_window_get_instance_private (window);

        /* change font for all pages */
        for (i = 0; i < gtk_notebook_get_n_pages (priv->notebook); i++) {
                GtkWidget *page = gtk_notebook_get_nth_page (priv->notebook, i);
                view = WEBKIT_WEB_VIEW (g_object_get_data (G_OBJECT (page), "web_view"));
                dh_util_view_set_font (view, font_name_fixed, font_name_variable);
        }
}

/* FIXME: this is a bit complicated just for saving the window state.
 * Implement something (hopefully) simpler, by saving the GSettings only when
 * needed:
 * - when closing a DhWindow.
 * - just before creating a second/third/... DhWindow, save the GSettings of the
 *   active DhWindow.
 *
 * There is also a HowDoI:
 * https://wiki.gnome.org/HowDoI/SaveWindowState
 */
static gboolean
window_settings_save_cb (gpointer user_data)
{
        DhWindow *window = DH_WINDOW (user_data);
        DhWindowPrivate *priv;
        DhSettings *settings;

        priv = dh_window_get_instance_private (window);

        priv->configure_id = 0;

        settings = dh_settings_get_singleton ();
        dh_util_window_settings_save (GTK_WINDOW (window),
                                      dh_settings_peek_window_settings (settings),
                                      TRUE);

        return G_SOURCE_REMOVE;
}

static gboolean
dh_window_configure_event (GtkWidget         *widget,
                           GdkEventConfigure *event)
{
        DhWindow *window = DH_WINDOW (widget);
        DhWindowPrivate *priv;

        priv = dh_window_get_instance_private (window);

        if (priv->configure_id != 0) {
                g_source_remove (priv->configure_id);
                priv->configure_id = 0;
        }

        priv->configure_id = g_timeout_add (WINDOW_SETTINGS_SAVE_TIMEOUT_MSECS,
                                            window_settings_save_cb,
                                            window);

        if (GTK_WIDGET_CLASS (dh_window_parent_class)->configure_event == NULL)
                return GDK_EVENT_PROPAGATE;

        return GTK_WIDGET_CLASS (dh_window_parent_class)->configure_event (widget, event);
}

static gboolean
dh_window_delete_event (GtkWidget   *widget,
                        GdkEventAny *event)
{
        DhWindow *window = DH_WINDOW (widget);
        DhWindowPrivate *priv;
        DhSettings *settings;

        priv = dh_window_get_instance_private (window);

        if (priv->configure_id != 0) {
                g_source_remove (priv->configure_id);
                priv->configure_id = 0;
        }

        settings = dh_settings_get_singleton ();
        dh_util_window_settings_save (GTK_WINDOW (window),
                                      dh_settings_peek_window_settings (settings),
                                      TRUE);

        if (GTK_WIDGET_CLASS (dh_window_parent_class)->delete_event == NULL)
                return GDK_EVENT_PROPAGATE;

        return GTK_WIDGET_CLASS (dh_window_parent_class)->delete_event (widget, event);
}

static void
dh_window_init (DhWindow *window)
{
        DhWindowPrivate *priv;
        DhSettings    *settings;
        GtkAccelGroup *accel_group;
        GClosure      *closure;
        guint          i;

        gtk_widget_init_template (GTK_WIDGET (window));

        priv = dh_window_get_instance_private (window);
        priv->selected_search_link = NULL;

        if (!_dh_app_has_app_menu (DH_APP (g_application_get_default ()))) {
                gtk_menu_button_set_menu_model (priv->gear_menu_button, priv->gear_app_menu);
        }

        /* handle settings */
        settings = dh_settings_get_singleton ();
        g_signal_connect_object (settings,
                                 "fonts-changed",
                                 G_CALLBACK (settings_fonts_changed_cb),
                                 window,
                                 0);

        g_action_map_add_action_entries (G_ACTION_MAP (window),
                                         win_entries, G_N_ELEMENTS (win_entries),
                                         window);

        accel_group = gtk_accel_group_new ();
        gtk_window_add_accel_group (GTK_WINDOW (window), accel_group);
        for (i = 0; i < G_N_ELEMENTS (tab_accel_keys); i++) {
                closure = g_cclosure_new (G_CALLBACK (window_web_view_tab_accel_cb),
                                          window,
                                          NULL);
                gtk_accel_group_connect (accel_group,
                                         tab_accel_keys[i],
                                         GDK_MOD1_MASK,
                                         0,
                                         closure);
        }
}

static void
dh_window_dispose (GObject *object)
{
        DhWindow *window = DH_WINDOW (object);
        DhWindowPrivate *priv;

        priv = dh_window_get_instance_private (window);

        if (priv->configure_id != 0) {
                g_source_remove (priv->configure_id);
                priv->configure_id = 0;
        }

        G_OBJECT_CLASS (dh_window_parent_class)->dispose (object);
}

static void
dh_window_class_init (DhWindowClass *klass)
{
        GObjectClass *object_class = G_OBJECT_CLASS (klass);
        GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

        object_class->dispose = dh_window_dispose;

        widget_class->configure_event = dh_window_configure_event;
        widget_class->delete_event = dh_window_delete_event;

        klass->open_link = dh_window_open_link;

        signals[OPEN_LINK] =
                g_signal_new ("open-link",
                              G_TYPE_FROM_CLASS (klass),
                              G_SIGNAL_RUN_LAST,
                              G_STRUCT_OFFSET (DhWindowClass, open_link),
                              NULL, NULL, NULL,
                              G_TYPE_NONE,
                              2,
                              G_TYPE_STRING,
                              DH_TYPE_OPEN_LINK_FLAGS);

        /* Bind class to template */
        gtk_widget_class_set_template_from_resource (widget_class,
                                                     "/org/gnome/devhelp/dh-window.ui");
        gtk_widget_class_bind_template_child_private (widget_class, DhWindow, gear_app_menu);
        gtk_widget_class_bind_template_child_private (widget_class, DhWindow, header_bar);
        gtk_widget_class_bind_template_child_private (widget_class, DhWindow, gear_menu_button);
        gtk_widget_class_bind_template_child_private (widget_class, DhWindow, hpaned);
        gtk_widget_class_bind_template_child_private (widget_class, DhWindow, grid_sidebar);
        gtk_widget_class_bind_template_child_private (widget_class, DhWindow, notebook);
        gtk_widget_class_bind_template_child_private (widget_class, DhWindow, search_bar);
        gtk_widget_class_bind_template_child_private (widget_class, DhWindow, search_entry);
        gtk_widget_class_bind_template_child_private (widget_class, DhWindow, go_up_button);
        gtk_widget_class_bind_template_child_private (widget_class, DhWindow, go_down_button);
}

static void
window_web_view_switch_page_cb (GtkNotebook *notebook,
                                gpointer     page,
                                guint        new_page_num,
                                DhWindow    *window)
{
        DhWindowPrivate *priv;
        GtkWidget *new_page;

        priv = dh_window_get_instance_private (window);

        new_page = gtk_notebook_get_nth_page (notebook, new_page_num);
        if (new_page != NULL) {
                WebKitWebView  *new_web_view;
                const gchar    *location;

                new_web_view = g_object_get_data (G_OBJECT (new_page), "web_view");

                /* Sync the book tree */
                location = webkit_web_view_get_uri (new_web_view);

                if (location != NULL)
                        dh_sidebar_select_uri (priv->sidebar, location);

                window_update_title (window, new_web_view, NULL);
        } else {
                /* i18n: Please don't translate "Devhelp" (it's marked as translatable
                 * for transliteration only) */
                gtk_window_set_title (GTK_WINDOW (window), _("Devhelp"));
        }
}

static void
window_web_view_switch_page_after_cb (GtkNotebook *notebook,
                                      gpointer     page,
                                      guint        new_page_num,
                                      DhWindow    *window)
{
        window_update_zoom_actions_state (window);
        window_update_back_forward_actions_state (window);
}

static void
window_populate (DhWindow *window)
{
        DhWindowPrivate *priv = dh_window_get_instance_private (window);

        /* Sidebar */
        priv->sidebar = DH_SIDEBAR (dh_sidebar_new (NULL));
        gtk_widget_set_vexpand (GTK_WIDGET (priv->sidebar), TRUE);
        gtk_widget_set_hexpand (GTK_WIDGET (priv->sidebar), TRUE);
        gtk_widget_show (GTK_WIDGET (priv->sidebar));
        gtk_container_add (GTK_CONTAINER (priv->grid_sidebar),
                           GTK_WIDGET (priv->sidebar));
        g_signal_connect (priv->sidebar,
                          "link-selected",
                          G_CALLBACK (window_search_link_selected_cb),
                          window);

        /* HTML tabs notebook. */
        g_signal_connect (priv->notebook,
                          "switch-page",
                          G_CALLBACK (window_web_view_switch_page_cb),
                          window);
        g_signal_connect_after (priv->notebook,
                                "switch-page",
                                G_CALLBACK (window_web_view_switch_page_after_cb),
                                window);

        /* Create findbar */
        gtk_search_bar_connect_entry (priv->search_bar, GTK_ENTRY (priv->search_entry));

        g_signal_connect (priv->search_bar,
                          "notify::search-mode-enabled",
                          G_CALLBACK (on_search_mode_enabled_changed),
                          window);
        g_signal_connect (priv->search_entry,
                          "search-changed",
                          G_CALLBACK (window_find_search_changed_cb),
                          window);
        g_signal_connect (priv->search_entry,
                          "activate",
                          G_CALLBACK (on_search_entry_activated),
                          window);
        g_signal_connect (priv->search_entry,
                          "key-press-event",
                          G_CALLBACK (on_search_entry_key_press),
                          window);
        g_signal_connect (priv->go_up_button,
                          "clicked",
                          G_CALLBACK (window_find_previous_cb),
                          window);
        g_signal_connect (priv->go_down_button,
                          "clicked",
                          G_CALLBACK (window_find_next_cb),
                          window);

        /* Focus search in sidebar by default */
        dh_sidebar_set_search_focus (priv->sidebar);

        window_update_zoom_actions_state (window);
        window_open_new_tab (window, NULL, TRUE);
}

static gchar *
find_equivalent_local_uri (const gchar *uri)
{
        gchar **components;
        guint n_components;
        const gchar *book_id;
        const gchar *relative_url;
        DhBookManager *book_manager;
        GList *books;
        GList *book_node;
        gchar *local_uri = NULL;

        g_return_val_if_fail (uri != NULL, NULL);

        components = g_strsplit (uri, "/", 0);
        n_components = g_strv_length (components);

        if ((g_str_has_prefix (uri, "http://library.gnome.org/devel/") ||
             g_str_has_prefix (uri, "https://library.gnome.org/devel/")) &&
            n_components >= 7) {
                book_id = components[4];
                relative_url = components[6];
        } else if ((g_str_has_prefix (uri, "http://developer.gnome.org/") ||
                    g_str_has_prefix (uri, "https://developer.gnome.org/")) &&
                   n_components >= 6) {
                /* E.g. http://developer.gnome.org/gio/stable/ch02.html */
                book_id = components[3];
                relative_url = components[5];
        } else {
                goto out;
        }

        book_manager = dh_book_manager_get_singleton ();
        books = dh_book_manager_get_books (book_manager);

        for (book_node = books; book_node != NULL; book_node = book_node->next) {
                DhBook *cur_book = DH_BOOK (book_node->data);
                GList *links;
                GList *link_node;

                if (g_strcmp0 (dh_book_get_id (cur_book), book_id) != 0)
                        continue;

                links = dh_book_get_links (cur_book);

                for (link_node = links; link_node != NULL; link_node = link_node->next) {
                        DhLink *cur_link = link_node->data;

                        if (dh_link_match_relative_url (cur_link, relative_url)) {
                                local_uri = dh_link_get_uri (cur_link);
                                goto out;
                        }
                }
        }

out:
        g_strfreev (components);
        return local_uri;
}

static gboolean
window_web_view_decide_policy_cb (WebKitWebView            *web_view,
                                  WebKitPolicyDecision     *policy_decision,
                                  WebKitPolicyDecisionType  type,
                                  DhWindow                 *window)
{
        const char   *uri;
        WebKitNavigationPolicyDecision *navigation_decision;
        WebKitNavigationAction *navigation_action;
        gchar *local_uri;
        gint button;
        gint state;

        if (type != WEBKIT_POLICY_DECISION_TYPE_NAVIGATION_ACTION)
                return GDK_EVENT_PROPAGATE;

        navigation_decision = WEBKIT_NAVIGATION_POLICY_DECISION (policy_decision);
        navigation_action = webkit_navigation_policy_decision_get_navigation_action (navigation_decision);
        uri = webkit_uri_request_get_uri (webkit_navigation_action_get_request (navigation_action));

        /* make sure to hide the info bar on page change */
        gtk_widget_hide (window_get_active_info_bar (window));

        /* middle click or ctrl-click -> new tab */
        button = webkit_navigation_action_get_mouse_button (navigation_action);
        state = webkit_navigation_action_get_modifiers (navigation_action);
        if (button == 2 || (button == 1 && state == GDK_CONTROL_MASK)) {
                webkit_policy_decision_ignore (policy_decision);
                g_signal_emit (window, signals[OPEN_LINK], 0, uri, DH_OPEN_LINK_NEW_TAB);
                return GDK_EVENT_STOP;
        }

        if (g_str_equal (uri, "about:blank")) {
                return GDK_EVENT_PROPAGATE;
        }

        local_uri = find_equivalent_local_uri (uri);
        if (local_uri != NULL) {
                webkit_policy_decision_ignore (policy_decision);
                _dh_window_display_uri (window, local_uri);
                g_free (local_uri);
                return GDK_EVENT_STOP;
        }

        if (!g_str_has_prefix (uri, "file://")) {
                webkit_policy_decision_ignore (policy_decision);
                gtk_show_uri_on_window (GTK_WINDOW (window), uri, GDK_CURRENT_TIME, NULL);
                return GDK_EVENT_STOP;
        }

        return GDK_EVENT_PROPAGATE;
}

static void
window_web_view_load_changed_cb (WebKitWebView   *web_view,
                                 WebKitLoadEvent  load_event,
                                 DhWindow        *window)
{
        const gchar *uri;
        DhWindowPrivate *priv;

        priv = dh_window_get_instance_private (window);

        if (load_event != WEBKIT_LOAD_COMMITTED)
                return;

        uri = webkit_web_view_get_uri (web_view);
        dh_sidebar_select_uri (priv->sidebar, uri);
}

static gboolean
window_web_view_load_failed_cb (WebKitWebView   *web_view,
                                WebKitLoadEvent  load_event,
                                const gchar     *uri,
                                GError          *web_error,
                                DhWindow        *window)
{
        GtkWidget *info_bar;
        GtkWidget *content_area;
        GtkWidget *message_label;
        GList     *children;
        gchar     *markup;

        /* Ignore cancellation errors; which happen when typing fast in the search entry */
        if (g_error_matches (web_error, WEBKIT_NETWORK_ERROR, WEBKIT_NETWORK_ERROR_CANCELLED))
                return GDK_EVENT_STOP;

        info_bar = window_get_active_info_bar (window);
        markup = g_strdup_printf ("<b>%s</b>", _("Error opening the requested link."));
        message_label = gtk_label_new (markup);
        gtk_label_set_xalign (GTK_LABEL (message_label), 0.0);
        gtk_label_set_use_markup (GTK_LABEL (message_label), TRUE);
        content_area = gtk_info_bar_get_content_area (GTK_INFO_BAR (info_bar));
        children = gtk_container_get_children (GTK_CONTAINER (content_area));
        if (children != NULL) {
                gtk_container_remove (GTK_CONTAINER (content_area), children->data);
                g_list_free (children);
        }
        gtk_container_add (GTK_CONTAINER (content_area), message_label);
        gtk_widget_show (message_label);

        gtk_widget_show (info_bar);
        g_free (markup);

        return GDK_EVENT_STOP;
}

static gboolean
window_web_view_scroll_event_cb (GtkWidget *widget,
                                 GdkEvent  *event,
                                 gpointer   user_data) {
        static gdouble total_delta_y = 0.f;

        GdkEventScroll *scroll_event;
        DhWindow *window;
        gdouble delta_y;

        scroll_event = (GdkEventScroll *)event;

        if (scroll_event->state & GDK_CONTROL_MASK) {
                window = user_data;

                switch (scroll_event->direction) {
                        case GDK_SCROLL_UP:
                                bump_zoom_level (window, 1);

                                return TRUE;

                        case GDK_SCROLL_DOWN:
                                bump_zoom_level (window, -1);

                                return TRUE;

                        case GDK_SCROLL_LEFT:
                        case GDK_SCROLL_RIGHT:
                                break;

                        case GDK_SCROLL_SMOOTH:
                                gdk_event_get_scroll_deltas (event, NULL, &delta_y);

                                total_delta_y += delta_y;

                                /*
                                 * Avoiding direct float comparison.
                                 * -1 and 1 are the thresholds for bumping the zoom level,
                                 *  which can be adjusted for taste.
                                 */
                                if ((int)total_delta_y <= -1) {
                                        total_delta_y = 0.f;

                                        bump_zoom_level (window, 1);
                                } else if ((int)total_delta_y >= 1) {
                                        total_delta_y = 0.f;

                                        bump_zoom_level (window, -1);
                                }
                                return TRUE;

                        default:
                                g_assert_not_reached ();
            }
        }

        return FALSE;
}

static void
window_search_link_selected_cb (GObject  *ignored,
                                DhLink   *link,
                                DhWindow *window)
{
        DhWindowPrivate  *priv;
        WebKitWebView *view;
        gchar         *uri;

        priv = dh_window_get_instance_private (window);
        priv->selected_search_link = link;

        view = window_get_active_web_view (window);

        uri = dh_link_get_uri (link);
        if (uri != NULL)
                webkit_web_view_load_uri (view, uri);
        g_free (uri);
}

static void
window_web_view_title_changed_cb (WebKitWebView *web_view,
                                  GParamSpec    *param_spec,
                                  DhWindow      *window)
{
        const gchar *title = webkit_web_view_get_title (web_view);

        if (web_view == window_get_active_web_view (window)) {
                window_update_title (window, web_view, title);
        }

        window_tab_set_title (window, web_view, title);
}

static gboolean
window_web_view_button_press_event_cb (WebKitWebView  *web_view,
                                       GdkEventButton *event,
                                       DhWindow       *window)
{
        switch (event->button) {
                case 3:
                        return GDK_EVENT_STOP;

                /* Some mice emit button presses when the scroll wheel is tilted
                 * to the side. Web browsers use them to navigate in history.
                 */
                case 8:
                        webkit_web_view_go_back (web_view);
                        return GDK_EVENT_STOP;
                case 9:
                        webkit_web_view_go_forward (web_view);
                        return GDK_EVENT_STOP;

                default:
                        break;
        }

        return GDK_EVENT_PROPAGATE;
}

static void
do_search (DhWindow *window)
{
        DhWindowPrivate *priv;
        WebKitFindController *find_controller;
        guint                 find_options = WEBKIT_FIND_OPTIONS_WRAP_AROUND;
        const gchar          *search_text;

        priv = dh_window_get_instance_private (window);

        find_controller = webkit_web_view_get_find_controller (window_get_active_web_view (window));
        /* FIXME: do we want an option to set this? */
        find_options |= WEBKIT_FIND_OPTIONS_CASE_INSENSITIVE;

        search_text = gtk_entry_get_text (GTK_ENTRY (priv->search_entry));
        webkit_find_controller_search (find_controller, search_text, find_options, G_MAXUINT);
}

static void
window_find_search_changed_cb (GtkEntry *entry,
                               DhWindow *window)
{
        do_search (window);
}

static void
findbar_find_next (DhWindow *window)
{
        DhWindowPrivate *priv;
        WebKitWebView        *view;
        WebKitFindController *find_controller;

        priv = dh_window_get_instance_private (window);

        view = window_get_active_web_view (window);

        gtk_search_bar_set_search_mode (priv->search_bar, TRUE);

        find_controller = webkit_web_view_get_find_controller (view);
        webkit_find_controller_search_next (find_controller);
}

static void
window_find_next_cb (GtkWidget *widget,
                     DhWindow  *window)
{
        findbar_find_next (window);
}

static void
findbar_find_previous (DhWindow *window)
{
        DhWindowPrivate *priv;
        WebKitWebView        *view;
        WebKitFindController *find_controller;

        priv = dh_window_get_instance_private (window);

        view = window_get_active_web_view (window);

        gtk_search_bar_set_search_mode (priv->search_bar, TRUE);

        find_controller = webkit_web_view_get_find_controller (view);
        webkit_find_controller_search_previous (find_controller);
}

static void
window_find_previous_cb (GtkWidget *widget,
                         DhWindow  *window)
{
        findbar_find_previous (window);
}

static void
on_search_mode_enabled_changed (GtkSearchBar *search_bar,
                                GParamSpec   *pspec,
                                DhWindow     *window)
{
        if (!gtk_search_bar_get_search_mode (search_bar)) {
                WebKitWebView        *view;
                WebKitFindController *find_controller;

                view = window_get_active_web_view (window);

                find_controller = webkit_web_view_get_find_controller (view);
                webkit_find_controller_search_finish (find_controller);
        }
}

static void
on_search_entry_activated (GtkEntry *entry,
                           DhWindow *window)
{
        findbar_find_next (window);
}

static gboolean
on_search_entry_key_press (GtkEntry    *entry,
                           GdkEventKey *event,
                           DhWindow    *window)
{
        if (event->keyval == GDK_KEY_Return &&
            event->state & GDK_SHIFT_MASK) {
                findbar_find_previous (window);
                return GDK_EVENT_STOP;
        }

        return GDK_EVENT_PROPAGATE;
}

static void
window_web_view_tab_accel_cb (GtkAccelGroup   *accel_group,
                              GObject         *object,
                              guint            key,
                              GdkModifierType  mod,
                              DhWindow        *window)
{
        DhWindowPrivate *priv;
        gint page_num;
        guint i;

        priv = dh_window_get_instance_private (window);

        page_num = -1;
        for (i = 0; i < G_N_ELEMENTS (tab_accel_keys); i++) {
                if (tab_accel_keys[i] == key) {
                        page_num = i;
                        break;
                }
        }

        if (page_num != -1)
                gtk_notebook_set_current_page (priv->notebook, page_num);
}

static void
apply_webview_settings (WebKitWebView *view)
{
        /* Disable some things we have no need for */
        g_object_set (webkit_web_view_get_settings (view),
                      "enable-html5-database", FALSE,
                      "enable-html5-local-storage", FALSE,
                      "enable-plugins", FALSE,
                      NULL);
}

static void
window_open_new_tab (DhWindow    *window,
                     const gchar *location,
                     gboolean     switch_focus)
{
        DhWindowPrivate *priv;
        DhSettings   *settings;
        GtkWidget    *view;
        GtkWidget    *vbox;
        GtkWidget    *label;
        gint          num;
        GtkWidget    *info_bar;
        gchar *font_fixed = NULL;
        gchar *font_variable = NULL;
        WebKitBackForwardList *back_forward_list;

        priv = dh_window_get_instance_private (window);

        /* Prepare the web view */
        view = webkit_web_view_new ();
        apply_webview_settings (WEBKIT_WEB_VIEW (view));
        gtk_widget_show (view);

        /* get the current fonts and set them on the new view */
        settings = dh_settings_get_singleton ();
        dh_settings_get_selected_fonts (settings, &font_fixed, &font_variable);
        dh_util_view_set_font (WEBKIT_WEB_VIEW (view), font_fixed, font_variable);
        g_free (font_fixed);
        g_free (font_variable);

        /* Prepare the info bar */
        info_bar = gtk_info_bar_new ();
        gtk_widget_set_no_show_all (info_bar, TRUE);
        gtk_info_bar_add_button (GTK_INFO_BAR (info_bar),
                                 _("_Close"), GTK_RESPONSE_OK);
        gtk_info_bar_set_message_type (GTK_INFO_BAR (info_bar),
                                       GTK_MESSAGE_ERROR);
        g_signal_connect (info_bar,
                          "response",
                          G_CALLBACK (gtk_widget_hide),
                          NULL);

        vbox = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);
        gtk_widget_show (vbox);

        /* XXX: Really it would be much better to use real structures */
        g_object_set_data (G_OBJECT (vbox), "web_view", view);
        g_object_set_data (G_OBJECT (vbox), "info_bar", info_bar);

        gtk_box_pack_start (GTK_BOX (vbox), info_bar, FALSE, TRUE, 0);
        gtk_box_pack_start (GTK_BOX (vbox), view, TRUE, TRUE, 0);

        label = window_new_tab_label (window, _("Empty Page"), vbox);
        gtk_widget_show_all (label);

        g_signal_connect (view, "notify::title",
                          G_CALLBACK (window_web_view_title_changed_cb),
                          window);
        g_signal_connect (view, "button-press-event",
                          G_CALLBACK (window_web_view_button_press_event_cb),
                          window);
        g_signal_connect (view, "decide-policy",
                          G_CALLBACK (window_web_view_decide_policy_cb),
                          window);
        g_signal_connect (view, "load-changed",
                          G_CALLBACK (window_web_view_load_changed_cb),
                          window);
        g_signal_connect (view, "load-failed",
                          G_CALLBACK (window_web_view_load_failed_cb),
                          window);
        g_signal_connect (view, "scroll-event",
                          G_CALLBACK (window_web_view_scroll_event_cb),
                          window);

        back_forward_list = webkit_web_view_get_back_forward_list (WEBKIT_WEB_VIEW (view));
        g_signal_connect_object (back_forward_list,
                                 "changed",
                                 G_CALLBACK (window_update_back_forward_actions_state),
                                 window,
                                 G_CONNECT_AFTER | G_CONNECT_SWAPPED);

        num = gtk_notebook_append_page (priv->notebook, vbox, NULL);
        gtk_container_child_set (GTK_CONTAINER (priv->notebook), vbox,
                                 "tab-expand", TRUE,
                                 "reorderable", TRUE,
                                 NULL);
        gtk_notebook_set_tab_label (priv->notebook, vbox, label);

        if (gtk_notebook_get_n_pages (priv->notebook) > 1) {
                gtk_notebook_set_show_tabs (priv->notebook, TRUE);
        } else {
                gtk_notebook_set_show_tabs (priv->notebook, FALSE);
        }

        if (location != NULL) {
                webkit_web_view_load_uri (WEBKIT_WEB_VIEW (view), location);
        } else {
                webkit_web_view_load_uri (WEBKIT_WEB_VIEW (view), "about:blank");
        }

        if (switch_focus) {
                gtk_notebook_set_current_page (priv->notebook, num);
        }
}

static void
close_button_clicked_cb (GtkButton *button,
                         DhWindow  *window)
{
        DhWindowPrivate *priv;
        GtkWidget *parent_tab;
        gint page_num;

        priv = dh_window_get_instance_private (window);

        parent_tab = g_object_get_data (G_OBJECT (button), "parent_tab");
        page_num = gtk_notebook_page_num (priv->notebook, parent_tab);
        window_close_tab (window, page_num);
}

static GtkWidget *
window_new_tab_label (DhWindow        *window,
                      const gchar     *str,
                      const GtkWidget *parent)
{
        GtkWidget *label;
        GtkWidget *hbox;
        GtkWidget *close_button;
        GtkWidget *image;
        GIcon *icon;
        GtkStyleContext *context;

        hbox = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 4);

        label = gtk_label_new (str);
        gtk_label_set_ellipsize (GTK_LABEL (label), PANGO_ELLIPSIZE_END);
        gtk_widget_set_halign (label, GTK_ALIGN_CENTER);
        gtk_box_pack_start (GTK_BOX (hbox), label, TRUE, TRUE, 0);

        close_button = GTK_WIDGET (g_object_new (GTK_TYPE_BUTTON,
                                "focus-on-click", FALSE,
                                NULL));
        context = gtk_widget_get_style_context (close_button);
        gtk_style_context_add_class (context, "small-button");
        gtk_style_context_add_class (context, "flat");

        icon = g_themed_icon_new_with_default_fallbacks ("window-close-symbolic");
        image = gtk_image_new_from_gicon (icon, GTK_ICON_SIZE_MENU);
        gtk_widget_show (image);
        g_object_unref (icon);
        gtk_container_add (GTK_CONTAINER (close_button), image);

        g_object_set_data (G_OBJECT (close_button), "parent_tab", (gpointer) parent);

        g_signal_connect (close_button, "clicked",
                          G_CALLBACK (close_button_clicked_cb),
                          window);

        gtk_box_pack_start (GTK_BOX (hbox), close_button, FALSE, FALSE, 0);

        g_object_set_data (G_OBJECT (hbox), "label", label);

        return hbox;
}

static WebKitWebView *
window_get_active_web_view (DhWindow *window)
{
        DhWindowPrivate *priv;
        gint          page_num;
        GtkWidget    *page;

        priv = dh_window_get_instance_private (window);

        page_num = gtk_notebook_get_current_page (priv->notebook);
        if (page_num == -1) {
                return NULL;
        }

        page = gtk_notebook_get_nth_page (priv->notebook, page_num);

        return g_object_get_data (G_OBJECT (page), "web_view");
}

static GtkWidget *
window_get_active_info_bar (DhWindow *window)
{
        DhWindowPrivate *priv;
        gint          page_num;
        GtkWidget    *page;

        priv = dh_window_get_instance_private (window);

        page_num = gtk_notebook_get_current_page (priv->notebook);
        if (page_num == -1) {
                return NULL;
        }

        page = gtk_notebook_get_nth_page (priv->notebook, page_num);

        return g_object_get_data (G_OBJECT (page), "info_bar");
}

static void
window_update_title (DhWindow      *window,
                     WebKitWebView *web_view,
                     const gchar   *web_view_title)
{
        DhWindowPrivate *priv;

        priv = dh_window_get_instance_private (window);

        if (web_view_title == NULL)
                web_view_title = webkit_web_view_get_title (web_view);

        if (web_view_title != NULL && web_view_title[0] == '\0')
                web_view_title = NULL;

        gtk_header_bar_set_title (priv->header_bar, web_view_title);
}

static void
window_tab_set_title (DhWindow      *window,
                      WebKitWebView *web_view,
                      const gchar   *title)
{
        DhWindowPrivate *priv;
        gint num_pages;
        gint i;

        priv = dh_window_get_instance_private (window);

        if (title == NULL || title[0] == '\0')
                title = _("Empty Page");

        num_pages = gtk_notebook_get_n_pages (priv->notebook);
        for (i = 0; i < num_pages; i++) {
                GtkWidget *page;
                GtkWidget *page_web_view;

                page = gtk_notebook_get_nth_page (priv->notebook, i);
                page_web_view = g_object_get_data (G_OBJECT (page), "web_view");

                /* The web_view widget is inside a frame. */
                if (page_web_view == GTK_WIDGET (web_view)) {
                        GtkWidget *hbox;

                        hbox = gtk_notebook_get_tab_label (priv->notebook, page);

                        if (hbox != NULL) {
                                GtkLabel *label = g_object_get_data (G_OBJECT (hbox), "label");
                                gtk_label_set_text (label, title);
                        }
                        break;
                }
        }
}

GtkWidget *
dh_window_new (DhApp *application)
{
        DhWindow *window;
        DhWindowPrivate *priv;
        DhSettings *settings;

        window = g_object_new (DH_TYPE_WINDOW,
                               "application", application,
                               NULL);

        priv = dh_window_get_instance_private (window);

        window_populate (window);

        settings = dh_settings_get_singleton ();
        dh_util_window_settings_restore (GTK_WINDOW (window),
                                         dh_settings_peek_window_settings (settings),
                                         TRUE);

        g_settings_bind (dh_settings_peek_paned_settings (settings), "position",
                         priv->hpaned, "position",
                         G_SETTINGS_BIND_DEFAULT);

        return GTK_WIDGET (window);
}

void
dh_window_search (DhWindow    *window,
                  const gchar *str)
{
        DhWindowPrivate *priv;

        g_return_if_fail (DH_IS_WINDOW (window));

        priv = dh_window_get_instance_private (window);

        dh_sidebar_set_search_string (priv->sidebar, str);
}

/* Only call this with a URI that is known to be in the docs. */
void
_dh_window_display_uri (DhWindow    *window,
                        const gchar *uri)
{
        DhWindowPrivate  *priv;
        WebKitWebView *web_view;

        g_return_if_fail (DH_IS_WINDOW (window));
        g_return_if_fail (uri != NULL);

        priv = dh_window_get_instance_private (window);

        web_view = window_get_active_web_view (window);
        webkit_web_view_load_uri (web_view, uri);
        dh_sidebar_select_uri (priv->sidebar, uri);
}
