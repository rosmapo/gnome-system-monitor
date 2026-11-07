#ifndef _GSM_APPLICATION_H_
#define _GSM_APPLICATION_H_

#include <gtkmm.h>
#include <glibtop/cpu.h>
#include <adwaita.h>

#include <algorithm>
#include <mutex>

#include "legacy/treeview.h"
#include "prettytable.h"
#include "procinfo.h"
#include "proclist.h"
#include "smooth_refresh.h"
#include "util.h"

struct ProcConfig
  : private procman::NonCopyable
{
  ProcConfig ()
    : current_tab (),
    update_interval (0),
    num_cpus (0),
    solaris_mode (false),
    process_memory_in_iec (true)
  {
  }

  Glib::ustring current_tab;
  int update_interval;
  gint num_cpus;
  bool solaris_mode;
  bool process_memory_in_iec;
};

class GsmApplication final: public Gtk::Application, private procman::NonCopyable
{
private:
  void load_settings ();
  void load_command_line_options();

  void on_preferences_activate (const Glib::VariantBase&);
  void on_lsof_activate (const Glib::VariantBase&);
  void on_help_activate (const Glib::VariantBase&);
  void on_quit_activate (const Glib::VariantBase&);
  int handle_local_options (const Glib::RefPtr<Glib::VariantDict> &);

  GsmApplication();

  void on_activate () override;
  int on_command_line (const Glib::RefPtr<Gio::ApplicationCommandLine>&command_line) override;
  void on_startup () override;
public:
  static GsmApplication& get ();

  void save_config ();
  void shutdown ();

  Glib::RefPtr<Gio::Settings> settings;
  AdwApplicationWindow *main_window;
  AdwViewStack *stack;
  GtkMenuButton *app_menu_button;

  GMenuModel *generic_window_menu_model;
  GMenuModel *process_window_menu_model;

  ProcList processes;
  ProcConfig config;
  PrettyTable *pretty_table;
  GsmTreeView *tree;
  GtkRevealer *proc_actionbar_revealer;
  GtkPopover *proc_popover_menu;
  GtkButton *refresh_button;
  GtkButton *end_process_button;
  GtkToggleButton *search_button;
  GtkSearchEntry *search_entry;
  GtkSearchBar *search_bar;
  GtkTreePath *top_of_tree;

  gdouble last_vscroll_max;
  gdouble last_vscroll_value;
  guint timeout;
  guint64 cpu_total_time;
  guint64 cpu_total_time_last;
  unsigned frequency;

  GtkTreeSelection *selection;

  SmoothRefresh *smooth_refresh;
};

#endif /* _GSM_APPLICATION_H_ */
