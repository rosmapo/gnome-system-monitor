/* Procman - main window
Copyright (C) 2001 Kevin Vandersloot
This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.
*/
#include <config.h>
#include <glib/gi18n.h>
#include <gtk/gtk.h>
#include <adwaita.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>
#include <gdk/gdkkeysyms.h>
#include <math.h>

#include "interface.h"
#include "application.h"
#include "procinfo.h"
#include "proctable.h"
#include "procactions.h"
#include "procdialogs.h"
#include "setaffinity.h"
#include "memmaps.h"
#include "openfiles.h"
#include "procproperties.h"
#include "util.h"
#include "settings-keys.h"

static void
search_text_changed (GtkEditable*, gpointer data)
{
  GsmApplication * const app = static_cast<GsmApplication *>(data);
  gtk_tree_model_filter_refilter (GTK_TREE_MODEL_FILTER (gtk_tree_model_sort_get_model (
    GTK_TREE_MODEL_SORT (gtk_tree_view_get_model (
      GTK_TREE_VIEW (app->tree))))));
  proctable_refresh_summary_headers(app);
}

static void
create_proc_view (GsmApplication *app, GtkBuilder *builder)
{
  GsmTreeView *proctree;
  GtkWidget *scrolled;

  proctree = proctable_new (app);
  scrolled = GTK_WIDGET (gtk_builder_get_object (builder, "processes_scrolled"));
  gtk_scrolled_window_set_child (GTK_SCROLLED_WINDOW (scrolled), GTK_WIDGET (proctree));

  app->proc_actionbar_revealer = GTK_REVEALER (gtk_builder_get_object (builder, "proc_actionbar_revealer"));

  GMenuModel *menu_model = G_MENU_MODEL (gtk_builder_get_object (builder, "process-popup-menu"));
  app->proc_popover_menu = GTK_POPOVER (gtk_popover_menu_new_from_model (menu_model));
  gtk_popover_set_has_arrow (app->proc_popover_menu, FALSE);
  gtk_popover_set_default_widget (app->proc_popover_menu, GTK_WIDGET (scrolled));
  gtk_widget_set_parent (GTK_WIDGET (app->proc_popover_menu), GTK_WIDGET (scrolled));

  app->end_process_button = GTK_BUTTON (gtk_builder_get_object (builder, "end_process_button"));
  app->search_button = GTK_TOGGLE_BUTTON (gtk_builder_get_object (builder, "search_button"));
  app->search_bar = GTK_SEARCH_BAR (gtk_builder_get_object (builder, "proc_searchbar"));
  app->search_entry = GTK_SEARCH_ENTRY (gtk_builder_get_object (builder, "proc_searchentry"));

  gtk_search_bar_set_key_capture_widget (app->search_bar, GTK_WIDGET (app->main_window));
  g_signal_connect (app->search_entry, "changed", G_CALLBACK (search_text_changed), app);
}

static void
on_activate_about (GSimpleAction *, GVariant *, gpointer data)
{
  GsmApplication *app = (GsmApplication *) data;
  AdwDialog *about_dialog;
  const gchar *authors[] = { "Kevin Vandersloot", "Erik Johnsson", "Jorgen Scheibengruber", "Benoît Dejean", "Paolo Borelli", "Karl Lattimer", "Chris Kühl", "Robert Roth", "Stefano Facchini", "Jacob Barkdull", "Ondřej Míchal", NULL };
  const gchar *documenters[] = { "Bill Day", "Sun Microsystems", NULL };
  const gchar *artists[] = { "Baptiste Mille-Mathias", NULL };

  about_dialog = adw_about_dialog_new_from_appdata ("/org/gnome/gnome-system-monitor/metainfo.xml", VERSION);
  adw_about_dialog_set_copyright (ADW_ABOUT_DIALOG (about_dialog), "Copyright \xc2\xa9 2001-2004 Kevin Vandersloot\nCopyright \xc2\xa9 2005-2007 Benoît Dejean\nCopyright \xc2\xa9 2011 Chris Kühl");
  adw_about_dialog_set_developers (ADW_ABOUT_DIALOG (about_dialog), authors);
  adw_about_dialog_set_artists (ADW_ABOUT_DIALOG (about_dialog), artists);
  adw_about_dialog_set_documenters (ADW_ABOUT_DIALOG (about_dialog), documenters);
  adw_about_dialog_set_translator_credits (ADW_ABOUT_DIALOG (about_dialog), _("translator-credits"));
  adw_dialog_present (about_dialog, GTK_WIDGET (app->main_window));
}

static void
on_activate_keyboard_shortcuts (GSimpleAction *, GVariant *, gpointer data)
{
  GsmApplication *app = (GsmApplication *) data;
  GtkBuilder *builder = gtk_builder_new ();
  GError *err = NULL;
  AdwDialog *shortcuts_dialog;

  gtk_builder_add_from_resource (builder, "/org/gnome/gnome-system-monitor/shortcuts-dialog.ui", &err);
  if (err != NULL)
    g_error ("%s", err->message);

  shortcuts_dialog = ADW_DIALOG (gtk_builder_get_object (builder, "shortcuts_dialog"));
  adw_dialog_present (shortcuts_dialog, GTK_WIDGET (app->main_window));
}

static void
on_activate_refresh (GSimpleAction *, GVariant *, gpointer data)
{
  GsmApplication *app = (GsmApplication *) data;
  proctable_update (app);
}

static void
on_activate_set_affinity (GSimpleAction *, GVariant *, gpointer data)
{
  GsmApplication *app = (GsmApplication *) data;
  create_set_affinity_dialog (app);
}

static void
create_single_memmaps_dialog (GtkTreeModel *model,
                              GtkTreePath *,
                              GtkTreeIter  *iter,
                              gpointer)
{
  ProcInfo *info;
  GsmMemMapsView *memmaps_view;
  gtk_tree_model_get (model, iter, COL_POINTER, &info, -1);
  memmaps_view = gsm_memmaps_view_new (info);
  gtk_window_present (GTK_WINDOW (memmaps_view));
}

static void
on_activate_memory_maps (GSimpleAction *, GVariant *, gpointer data)
{
  GsmApplication *app = (GsmApplication *) data;
  gtk_tree_selection_selected_foreach (app->selection, create_single_memmaps_dialog, NULL);
}

static void
create_single_openfiles_dialog (GtkTreeModel *model, GtkTreePath *, GtkTreeIter *iter, gpointer)
{
  ProcInfo *info;
  GsmOpenFiles *open_files;
  gtk_tree_model_get (model, iter, COL_POINTER, &info, -1);
  open_files = gsm_open_files_new (GsmApplication::get().gobj(), info);
  gtk_window_present (GTK_WINDOW (open_files));
}

static void
on_activate_open_files (GSimpleAction *, GVariant *, gpointer data)
{
  GsmApplication *app = (GsmApplication *) data;
  gtk_tree_selection_selected_foreach (app->selection, create_single_openfiles_dialog, app);
}

static void
on_activate_process_properties (GSimpleAction *, GVariant *, gpointer data)
{
  GsmApplication *app = (GsmApplication *) data;
  create_procproperties_dialog (app);
}

static void
on_activate_radio (GSimpleAction *action, GVariant *parameter, gpointer)
{
  g_action_change_state (G_ACTION (action), parameter);
}

static void
on_activate_toggle (GSimpleAction *action, GVariant *, gpointer)
{
  GVariant *state = g_action_get_state (G_ACTION (action));
  g_action_change_state (G_ACTION (action), g_variant_new_boolean (!g_variant_get_boolean (state)));
  g_variant_unref (state);
}

static void
on_activate_search (GSimpleAction*, GVariant*, gpointer data)
{
  GsmApplication *app = (GsmApplication *) data;
  gtk_search_bar_set_search_mode (app->search_bar, !gtk_search_bar_get_search_mode (app->search_bar));
}

static void
change_show_page_state (GSimpleAction *action, GVariant *state, gpointer data)
{
  GsmApplication *app = (GsmApplication *) data;
  g_simple_action_set_state (action, state);
  auto tab = g_variant_get_string (state, NULL);
  app->settings->set_string (GSM_SETTING_CURRENT_TAB, tab);
  app->config.current_tab = tab;
  adw_view_stack_set_visible_child_name (app->stack, tab);
}

static void
change_show_processes_state (GSimpleAction *action, GVariant *state, gpointer data)
{
  GsmApplication *app = (GsmApplication *) data;
  auto state_var = Glib::wrap (state, true);
  g_simple_action_set_state (action, state);
  app->settings->set_value (GSM_SETTING_SHOW_WHOSE_PROCESSES, state_var);
}

static void
change_show_dependencies_state (GSimpleAction *action, GVariant *state, gpointer data)
{
  GsmApplication *app = (GsmApplication *) data;
  auto state_var = Glib::wrap (state, true);
  g_simple_action_set_state (action, state);
  app->settings->set_value (GSM_SETTING_SHOW_DEPENDENCIES, state_var);
}

static void
on_activate_priority (GSimpleAction *action, GVariant *parameter, gpointer data)
{
  GsmApplication *app = (GsmApplication *) data;
  g_action_change_state (G_ACTION (action), parameter);
  const gint32 priority = g_variant_get_int32 (parameter);
  switch (priority) {
    case 32:
      procdialog_create_renice_dialog (app);
      break;
    default:
      renice (app, priority);
      break;
  }
}

static void
change_priority_state (GSimpleAction *action, GVariant *state, gpointer)
{
  g_simple_action_set_state (action, state);
}

static void
update_page_activities (GsmApplication *app)
{
  const gchar *current_page = adw_view_stack_get_visible_child_name (app->stack);
  if (app->config.current_tab != current_page) {
    app->settings->set_string (GSM_SETTING_CURRENT_TAB, current_page);
    app->config.current_tab = current_page;
  }

  if (strcmp (current_page, "processes") == 0) {
    proctable_update (app);
    proctable_thaw (app);
    gtk_widget_set_visible (GTK_WIDGET (app->end_process_button), TRUE);
    gtk_widget_set_visible (GTK_WIDGET (app->search_button), TRUE);
    gtk_menu_button_set_menu_model (app->app_menu_button, app->process_window_menu_model);
    gtk_search_bar_set_key_capture_widget (app->search_bar, GTK_WIDGET (app->main_window));
    update_sensitivity (app);
    if (gtk_search_bar_get_search_mode (app->search_bar))
      gtk_widget_grab_focus (GTK_WIDGET (app->search_entry));
    else
      gtk_widget_grab_focus (GTK_WIDGET (app->tree));
  } else {
    proctable_freeze (app);
    gtk_widget_set_visible (GTK_WIDGET (app->end_process_button), FALSE);
    gtk_widget_set_visible (GTK_WIDGET (app->search_button), FALSE);
    gtk_menu_button_set_menu_model (app->app_menu_button, app->generic_window_menu_model);
    gtk_search_bar_set_key_capture_widget (app->search_bar, NULL);
    update_sensitivity (app);
  }
}

static void
cb_change_current_page (AdwViewStack *view_stack, GParamSpec *, gpointer data)
{
  GsmApplication *app = (GsmApplication *) data;
  if (g_strcmp0 (adw_view_stack_get_visible_child_name (view_stack), "processes") != 0)
    gtk_toggle_button_set_active (app->search_button, FALSE);
  update_page_activities ((GsmApplication *)data);
}

static gboolean
cb_main_window_delete (GtkWindow*, gpointer data)
{
  GsmApplication *app = (GsmApplication *) data;
  app->shutdown ();
  return TRUE;
}

static gboolean
cb_main_window_close (GtkWindow*, gpointer data)
{
  GsmApplication *app = (GsmApplication *) data;
  gint default_width, default_height;
  gboolean maximized;

  g_object_get (G_OBJECT (app->main_window), "default-width", &default_width, NULL);
  g_object_get (G_OBJECT (app->main_window), "default-height", &default_height, NULL);
  g_object_get (G_OBJECT (app->main_window), "maximized", &maximized, NULL);

  g_settings_set_int (app->settings->gobj (), GSM_SETTING_WINDOW_WIDTH, default_width);
  g_settings_set_int (app->settings->gobj (), GSM_SETTING_WINDOW_HEIGHT, default_height);
  g_settings_set_boolean (app->settings->gobj (), GSM_SETTING_MAXIMIZED, maximized);
  return FALSE;
}

static void
cb_main_window_suspended (GtkWindow *window, GParamSpec *, GsmApplication *app)
{
  auto current_page = app->config.current_tab;
  if (gtk_window_is_suspended (window)) {
    if (current_page == "processes") {
      proctable_freeze (app);
    }
  } else {
    if (current_page == "processes") {
      proctable_update (app);
      proctable_thaw (app);
    }
  }
}

void
create_main_window (GsmApplication *app)
{
  GtkBuilder *builder = gtk_builder_new ();
  GError *err = NULL;

  gtk_builder_add_from_resource (builder, "/org/gnome/gnome-system-monitor/data/interface.ui", &err);
  if (err != NULL) g_error ("%s", err->message);

  gtk_builder_add_from_resource (builder, "/org/gnome/gnome-system-monitor/data/menus.ui", &err);
  if (err != NULL) g_error ("%s", err->message);

  app->main_window = ADW_APPLICATION_WINDOW (gtk_builder_get_object (builder, "main_window"));
  gtk_window_set_application (GTK_WINDOW (app->main_window), app->gobj ());
  gtk_widget_set_name (GTK_WIDGET (app->main_window), "gnome-system-monitor");

  app->stack = ADW_VIEW_STACK (gtk_builder_get_object (builder, "stack"));
  
  create_proc_view (app, builder);

  app->app_menu_button = GTK_MENU_BUTTON (gtk_builder_get_object (builder, "app_menu_button"));
  app->generic_window_menu_model = G_MENU_MODEL (gtk_builder_get_object (builder, "generic-window-menu"));
  app->process_window_menu_model = G_MENU_MODEL (gtk_builder_get_object (builder, "process-window-menu"));

  g_object_ref (G_OBJECT (app->generic_window_menu_model));
  g_object_ref (G_OBJECT (app->process_window_menu_model));
  gtk_menu_button_set_menu_model (app->app_menu_button, app->generic_window_menu_model);

  GActionEntry win_action_entries[] = {
    { "about", on_activate_about, NULL, NULL, NULL, { 0, 0, 0 } },
    { "shortcuts", on_activate_keyboard_shortcuts, NULL, NULL, NULL, { 0, 0, 0 } },
    { "search", on_activate_search, NULL, NULL, NULL, { 0, 0, 0 } },
    { "send-signal-stop", on_activate_send_signal, NULL, NULL, NULL, { 0, 0, 0 } },
    { "send-signal-cont", on_activate_send_signal, NULL, NULL, NULL, { 0, 0, 0 } },
    { "send-signal-term", on_activate_send_signal, NULL, NULL, NULL, { 0, 0, 0 } },
    { "send-signal-kill", on_activate_send_signal, NULL, NULL, NULL, { 0, 0, 0 } },
    { "priority", on_activate_priority, "i", "@i 0", change_priority_state, { 0, 0, 0 } },
    { "set-affinity", on_activate_set_affinity, NULL, NULL, NULL, { 0, 0, 0 } },
    { "memory-maps", on_activate_memory_maps, NULL, NULL, NULL, { 0, 0, 0 } },
    { "open-files", on_activate_open_files, NULL, NULL, NULL, { 0, 0, 0 } },
    { "process-properties", on_activate_process_properties, NULL, NULL, NULL, { 0, 0, 0 } },
    { "refresh", on_activate_refresh, NULL, NULL, NULL, { 0, 0, 0 } },
    // ZMENENÉ: Predvolená záložka je teraz 'processes' namiesto 'resources'
    { "show-page", on_activate_radio, "s", "'processes'", change_show_page_state, { 0, 0, 0 } },
    { "show-whose-processes", on_activate_radio, "s", "'all'", change_show_processes_state, { 0, 0, 0 } },
    { "show-dependencies", on_activate_toggle, NULL, "false", change_show_dependencies_state, { 0, 0, 0 } }
  };

  g_action_map_add_action_entries (G_ACTION_MAP (app->main_window), win_action_entries, G_N_ELEMENTS (win_action_entries), app);

  adw_view_stack_set_visible_child_name (ADW_VIEW_STACK (app->stack), app->config.current_tab.c_str ());

  g_signal_connect (G_OBJECT (app->stack), "notify::visible-child", G_CALLBACK (cb_change_current_page), app);
  g_signal_connect (G_OBJECT (app->main_window), "notify::suspended", G_CALLBACK (cb_main_window_suspended), app);
  g_signal_connect (G_OBJECT (app->main_window), "destroy", G_CALLBACK (cb_main_window_delete), app);
  g_signal_connect (G_OBJECT (app->main_window), "close-request", G_CALLBACK (cb_main_window_close), app);

  GAction *action;
  action = g_action_map_lookup_action (G_ACTION_MAP (app->main_window), "show-dependencies");
  g_action_change_state (action, g_settings_get_value (app->settings->gobj (), GSM_SETTING_SHOW_DEPENDENCIES));

  action = g_action_map_lookup_action (G_ACTION_MAP (app->main_window), "show-whose-processes");
  g_action_change_state (action, g_settings_get_value (app->settings->gobj (), GSM_SETTING_SHOW_WHOSE_PROCESSES));

  g_object_set (GTK_WINDOW (app->main_window), "default-width", g_settings_get_int (app->settings->gobj (), GSM_SETTING_WINDOW_WIDTH), NULL);
  g_object_set (GTK_WINDOW (app->main_window), "default-height", g_settings_get_int (app->settings->gobj (), GSM_SETTING_WINDOW_HEIGHT), NULL);
  g_object_set (GTK_WINDOW (app->main_window), "maximized", g_settings_get_boolean (app->settings->gobj (), GSM_SETTING_MAXIMIZED), NULL);

  gtk_window_present (GTK_WINDOW (app->main_window));
  update_page_activities (app);
  g_object_unref (G_OBJECT (builder));
}

static gboolean
scroll_to_selection (gpointer data)
{
  GsmApplication *app = (GsmApplication *) data;
  GList *paths = gtk_tree_selection_get_selected_rows (app->selection, NULL);
  guint length = g_list_length (paths);
  if (length > 0) {
    GtkTreePath *last_path = (GtkTreePath *) g_list_nth_data (paths, length - 1);
    gtk_tree_view_scroll_to_cell (GTK_TREE_VIEW (app->tree), last_path, NULL, FALSE, 0.0, 0.0);
  }
  g_list_free_full (paths, (GDestroyNotify) gtk_tree_path_free);
  return FALSE;
}

void
update_sensitivity (GsmApplication *app)
{
  const char * const selected_actions[] = { "send-signal-stop", "send-signal-cont", "send-signal-term", "send-signal-kill", "priority", "set-affinity", "memory-maps", "open-files", "process-properties" };
  const char * const processes_actions[] = { "refresh", "search", "show-whose-processes", "show-dependencies" };
  size_t i;
  gboolean processes_sensitivity, selected_sensitivity;
  GAction *action;

  processes_sensitivity = (strcmp (adw_view_stack_get_visible_child_name (app->stack), "processes") == 0);
  selected_sensitivity = gtk_tree_selection_count_selected_rows (app->selection) > 0;

  for (i = 0; i != G_N_ELEMENTS (processes_actions); ++i) {
    action = g_action_map_lookup_action (G_ACTION_MAP (app->main_window), processes_actions[i]);
    g_simple_action_set_enabled (G_SIMPLE_ACTION (action), processes_sensitivity);
  }

  for (i = 0; i != G_N_ELEMENTS (selected_actions); ++i) {
    action = g_action_map_lookup_action (G_ACTION_MAP (app->main_window), selected_actions[i]);
    g_simple_action_set_enabled (G_SIMPLE_ACTION (action), processes_sensitivity & selected_sensitivity);
  }

  gtk_revealer_set_reveal_child (GTK_REVEALER (app->proc_actionbar_revealer), selected_sensitivity);
  guint duration_ms = gtk_revealer_get_transition_duration (GTK_REVEALER (app->proc_actionbar_revealer));
  g_timeout_add (duration_ms, scroll_to_selection, app);
}