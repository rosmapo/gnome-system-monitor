#include <config.h>
#include <glib/gi18n.h>
#include <glibtop.h>
#include <glibtop/close.h>
#include <glibtop/cpu.h>
#include <glibtop/sysinfo.h>
#include <adwaita.h>
#include <signal.h>
#include <stdlib.h>
#include <glib.h>
#include <sigc++/functors/functors.h>
#include "application.h"
#include "procdialogs.h"
#include "prefsdialog.h"
#include "interface.h"
#include "proctable.h"
#include "settings-keys.h"
#include "util.h"
#include "lsof.h"
extern "C" {
#include "gsm-resources.h"
}

static void
cb_solaris_mode_changed (Gio::Settings& settings,
                         Glib::ustring  key,
                         GsmApplication*app)
{
  app->config.solaris_mode = settings.get_boolean (key);
  if (app->timeout)
    proctable_update (app);
}

static void
cb_process_memory_in_iec_changed (Gio::Settings& settings,
                                  Glib::ustring  key,
                                  GsmApplication*app)
{
  app->config.process_memory_in_iec = settings.get_boolean (key);
  if (app->timeout)
    proctable_update (app);
}

static void
cb_timeouts_changed (Gio::Settings& settings,
                     Glib::ustring  key,
                     GsmApplication*app)
{
  if (key == GSM_SETTING_PROCESS_UPDATE_INTERVAL)
  {
    app->config.update_interval = settings.get_int (key);
    app->smooth_refresh->reset ();
    if (app->timeout)
      proctable_reset_timeout (app);
  }
}

void
GsmApplication::load_settings ()
{
  glibtop_cpu cpu;
  this->settings = Gio::Settings::create (GSM_GSETTINGS_SCHEMA);
  config.current_tab = this->settings->get_string (GSM_SETTING_CURRENT_TAB);
  config.solaris_mode = this->settings->get_boolean (GSM_SETTING_SOLARIS_MODE);
  this->settings->signal_changed (GSM_SETTING_SOLARIS_MODE).connect ([this](const Glib::ustring&key) {
    cb_solaris_mode_changed (*this->settings.operator-> (), key, this);
  });
  config.process_memory_in_iec = this->settings->get_boolean (GSM_SETTING_PROCESS_MEMORY_IN_IEC);
  this->settings->signal_changed (GSM_SETTING_PROCESS_MEMORY_IN_IEC).connect ([this](const Glib::ustring&key) {
    cb_process_memory_in_iec_changed (*this->settings.operator-> (), key, this);
  });
  auto cbtc = [this](const Glib::ustring&key) {
    cb_timeouts_changed (*this->settings.operator-> (), key, this);
  };
  config.update_interval = this->settings->get_int (GSM_SETTING_PROCESS_UPDATE_INTERVAL);
  this->settings->signal_changed (GSM_SETTING_PROCESS_UPDATE_INTERVAL).connect (cbtc);
  glibtop_get_cpu (&cpu);
  frequency = cpu.frequency;
  config.num_cpus = glibtop_get_sysinfo ()->ncpu;
}

GsmApplication::GsmApplication()
: Gtk::Application (APP_ID, Gio::Application::Flags::HANDLES_COMMAND_LINE),
  settings (NULL),
  main_window (NULL),
  stack (NULL),
  app_menu_button (NULL),
  config (),
  pretty_table (NULL),
  tree (NULL),
  proc_actionbar_revealer (NULL),
  proc_popover_menu (NULL),
  refresh_button (NULL),
  end_process_button (NULL),
  search_button (NULL),
  search_entry (NULL),
  search_bar (NULL),
  top_of_tree (NULL),
  last_vscroll_max (0.0),
  last_vscroll_value (0.0),
  timeout (0U),
  cpu_total_time (0ULL),
  cpu_total_time_last (0ULL),
  frequency (0U),
  selection (NULL),
  smooth_refresh (NULL)
{
  Glib::set_application_name (_("System Monitor"));
  this->set_version (VERSION);
  this->set_option_context_summary (_("A simple process and system monitor."));
  this->add_main_option_entry (OptionType::BOOL,
                               "show-processes-tab",
                               'p',
                               _("Show the Processes tab"));
}

GsmApplication&
GsmApplication::get ()
{
  static GsmApplication singleton;
  return singleton;
}

void
GsmApplication::on_activate ()
{
  if (main_window == NULL)
    create_main_window (this);
  gtk_window_present (GTK_WINDOW (main_window));
}

int
GsmApplication::on_command_line (const Glib::RefPtr<Gio::ApplicationCommandLine>&command_line)
{
  auto args = command_line->get_options_dict ();
  const char*tab = NULL;
  if (args->contains ("show-processes-tab")) {
    tab = "processes";
  }
  on_activate ();
  if (tab)
    adw_view_stack_set_visible_child_name (this->stack, tab);
  return 0;
}

void
GsmApplication::on_help_activate (const Glib::VariantBase&)
{
  GError*error = 0;
  if (!g_app_info_launch_default_for_uri ("help:gnome-system-monitor", NULL, &error))
  {
    g_warning ("Could not display help : %s", error->message);
    g_error_free (error);
  }
}

void
GsmApplication::on_lsof_activate (const Glib::VariantBase&)
{
  GsmLsof *window = gsm_lsof_new (GTK_WINDOW (this->main_window));
  gtk_window_present (GTK_WINDOW (window));
}

void
GsmApplication::on_preferences_activate (const Glib::VariantBase&)
{
  create_preferences_dialog (this);
}

void
GsmApplication::on_quit_activate (const Glib::VariantBase&)
{
  shutdown ();
}

void
GsmApplication::shutdown ()
{
  if (timeout)
    g_source_remove (timeout);
  proctable_free_table (this);
  delete smooth_refresh;
  delete pretty_table;
  glibtop_close ();
  quit ();
}

void
GsmApplication::on_startup ()
{
  AdwStyleManager *style_manager;
  g_resources_register (gsm_get_resource ());
  Gtk::Application::on_startup ();
  glibtop_init ();
  style_manager = adw_style_manager_get_default ();
  adw_style_manager_set_color_scheme (style_manager, ADW_COLOR_SCHEME_PREFER_LIGHT);
  Gtk::Window::set_default_icon_name (APP_ID);
  using SignalFunction = void (GsmApplication::*)(const Glib::VariantBase&);
  auto make_action = [&](const Glib::ustring& name, const SignalFunction function) {
    auto action = Gio::SimpleAction::create (name);
    action->signal_activate ().connect (sigc::mem_fun (*this, function));
    return action;
  };
  add_action (make_action("quit", &GsmApplication::on_quit_activate));
  add_action (make_action("help", &GsmApplication::on_help_activate));
  add_action (make_action("lsof", &GsmApplication::on_lsof_activate));
  add_action (make_action("preferences", &GsmApplication::on_preferences_activate));
  Glib::RefPtr<Gtk::Builder> builder = Gtk::Builder::create_from_resource ("/org/gnome/gnome-system-monitor/data/menus.ui");
  set_accel_for_action ("win.show-dependencies", "<Primary>d");
  set_accel_for_action ("app.quit", "<Primary>q");
  set_accel_for_action ("window.close", "<Primary>w");
  set_accel_for_action ("win.send-signal-stop", "<Primary>s");
  set_accel_for_action ("win.send-signal-cont", "<Primary>c");
  set_accel_for_action ("win.send-signal-term", "<Primary>t");
  set_accel_for_action ("win.send-signal-kill", "<Primary>k");
  set_accel_for_action ("win.set-affinity", "<Alt>s");
  set_accel_for_action ("win.memory-maps", "<Control>m");
  set_accel_for_action ("win.open-files", "<Control>o");
  set_accel_for_action ("win.process-properties", "<Alt>Return");
  set_accel_for_action ("win.search", "<Control>f");
  set_accel_for_action ("app.help", "F1");
  set_accel_for_action ("win.shortcuts", "<Control>question");
  set_accel_for_action ("app.preferences", "<Primary>comma");
  set_accel_for_action ("win.show-page('processes')", "<Alt>2");
  set_accel_for_action ("win.refresh", "<Control>r");
  load_settings ();
  pretty_table = new PrettyTable ();
  smooth_refresh = new SmoothRefresh (settings);
}