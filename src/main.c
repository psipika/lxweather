/**
 * Copyright (c) 2012-2015 Piotr Sipika; see the AUTHORS file for more.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 * 
 * See the COPYRIGHT file for more information.
 */

#include "logutil.h"
#include "yahooutil.h"
#include "fileutil.h"
#include "location.h"
#include "forecast.h"
#include "weatherwidget.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <getopt.h>
#include <signal.h>

#include <gtk/gtk.h>

#define APP_NAME "lxweather"

/* signal handler to insure cleanup */
static void
sighandler(int signal)
{
  char * signame = NULL;

  switch (signal) {
  case SIGINT:
    signame = "SIGINT";
    break;

  case SIGKILL:
    signame = "SIGKILL";
    break;

  case SIGTERM:
    signame = "SIGTERM";
    break;

  case SIGHUP:
    signame = "SIGHUP";
    break;

  default:
    signame = "UNHANDLED";
    break;
  }

  fprintf(stderr, "LXWeather: signal caught: %s [%d]\n",
          signame, signal);

  gtk_main_quit();
}

/* long options */
static struct option longopts[] =
{
  {"help",     0, NULL, 1},
  {"config",   1, NULL, 2},
  {"logfile",  1, NULL, 3},
  {"loglevel", 1, NULL, 4},
  {NULL,       0, NULL, 0}
};

/* Wrapper around the weather widget/status icon pair */
typedef struct
{
  GtkWeather    * widget_;
  GtkStatusIcon * icon_;
} WeatherWidgetEntry;

/* List with WeatherWidgetEntry entries */
static GList * g_widgetentrylist = NULL;

/**
 * Prints out the usage help text.
 *
 * @param progname Pointer to the character constant with the name
 *                 of the executable being ran.
 */
static void
usage(const char *progname)
{
  fprintf(stderr, "Usage: %s [OPTIONs]\n", progname);
  fprintf(stderr, "Where OPTIONs is one or more of the following:\n");
  fprintf(stderr, "  -c|--config   Specify configuration file [Default: $HOME/.config/" APP_NAME "/config].\n");
  fprintf(stderr, "  -f|--logfile  Specify file location to write to. Acceptable values: \n");
  fprintf(stderr, "                'syslog', 'std', or PATH_TO_A_FILE. [Default: 'std']\n");
  fprintf(stderr, "  -l|--loglevel Specify the level to log at. Acceptable values: \n");
  fprintf(stderr, "                0 (no logging), 1 (log only errors), 2 (log errors and debug messages),\n");
  fprintf(stderr, "                3 (show verbose output) [Default: 0]\n");
  fprintf(stderr, "  -h|--help     Print this message and exit.\n");
}

/* WeatherWidget EVENT handling functions */

/**
 * Handles the location-changed event.
 *
 * @param weather  Pointer to the weather instance generating the event.
 * @param location Pointer to the LocationInfo object (can be NULL).
 * @param data     Pointer to user data.
 */
static void
location_changed(GtkWeather * weather  G_GNUC_UNUSED,
                 gpointer     location G_GNUC_UNUSED,
                 gpointer     data)
{
  WeatherWidgetEntry * entry = (WeatherWidgetEntry *) data;

  LXW_LOG(LXW_DEBUG, "main::location_changed: %p, %p, %p, %p", 
          location, entry, (entry)?entry->widget_:NULL, data);

  if (entry) {
    gtk_status_icon_set_from_stock(entry->icon_, GTK_STOCK_DIALOG_WARNING);
  }
}

/**
 * Handles the forecast-changed event.
 *
 * @param weather Pointer to the weather instance generating the event.
 * @param forecast Pointer to the ForecastInfo object (can be NULL).
 * @param data     Pointer to user data.
 */
static void
forecast_changed(GtkWeather * weather, gpointer forecast, gpointer data)
{
  WeatherWidgetEntry * entry = (WeatherWidgetEntry *) data;

  LXW_LOG(LXW_DEBUG, "main::forecast_changed: %p, %p, %p", 
          forecast, entry, (entry)?entry->widget_:NULL);
  
  if (entry) {
    if (forecast) {
      LXW_LOG(LXW_DEBUG, "Setting status icon.");
      gtk_status_icon_set_from_pixbuf(entry->icon_,
                                      ((ForecastInfo *)forecast)->image_);
    } else {
      LXW_LOG(LXW_DEBUG, "Setting status icon STOCK.");
      gtk_status_icon_set_from_stock(entry->icon_,
                                     GTK_STOCK_DIALOG_WARNING);
    }

    LXW_LOG(LXW_DEBUG, "Getting status tooltip.");
    gchar * tooltip_text = gtk_weather_get_tooltip_text(GTK_WIDGET(weather));

    LXW_LOG(LXW_DEBUG, "Setting status tooltip.");
    gtk_status_icon_set_tooltip_text(entry->icon_, tooltip_text);

    LXW_LOG(LXW_DEBUG, "Setting icon visible %d, %d.",
            gtk_status_icon_get_visible(entry->icon_),
            gtk_status_icon_is_embedded(entry->icon_));

    //gtk_status_icon_set_visible(entry->icon_, TRUE);

    LXW_LOG(LXW_DEBUG, "free tooltip.");
    g_free(tooltip_text);
  }

  LXW_LOG(LXW_DEBUG, "...Done.");
}

/**
 * Handles the 'activate' signal for the status icon (think left-click).
 *
 * @param icon  Pointer to the status icon from which the signal was emitted.
 * @param data  Pointer to the user data.
 */
static void
icon_activate(GtkStatusIcon * icon, gpointer data)
{
  /* To avoid compiler warning */
  (void) icon;

  LXW_LOG(LXW_DEBUG, "-- ICON::ACTIVATE\n");

  gtk_weather_run_conditions_dialog(GTK_WIDGET(data));
}

/**
 * Handles the 'popup-menu' signal for the status icon (think right-click).
 *
 * @param icon   Pointer to the status icon from which the signal is emitted.
 * @param button Id of the button which caused the signal.
 * @param time   Time of signal emission.
 * @param data   Pointer to user data.
 */
static void
icon_popup_menu(GtkStatusIcon * icon, guint button, guint time, gpointer data)
{
  /* To avoid compiler warning */
  (void)icon;
  (void)button;
  (void)time;

  LXW_LOG(LXW_DEBUG, "-- ICON::POPUP_MENU (\n");

  gtk_weather_run_popup_menu(GTK_WIDGET(data));
}

/* WeatherWidget UTILITY functions */

/**
 * Allocates the WeatherWidgetEntry
 *
 * @return pointer to the newly allocated WeatherWidgetEntry, or NULL
 */
static gpointer
entry_new()
{
  WeatherWidgetEntry * entry = (WeatherWidgetEntry *) g_try_new0(WeatherWidgetEntry, 1);

  if (entry) {
    GtkWidget * weather = gtk_weather_new();

    g_object_ref_sink(weather);

    g_signal_connect(G_OBJECT(weather),
                     "location-changed",
                     G_CALLBACK(location_changed),
                     (gpointer)entry);
      
    g_signal_connect(G_OBJECT(weather),
                     "forecast-changed",
                     G_CALLBACK(forecast_changed),
                     (gpointer)entry);
      
    GtkStatusIcon * icon = gtk_status_icon_new_from_stock(GTK_STOCK_DIALOG_ERROR);

    g_object_ref_sink(icon);

    g_signal_connect(G_OBJECT(icon),
                     "activate",
                     G_CALLBACK(icon_activate),
                     (gpointer)weather);
      
    g_signal_connect(G_OBJECT(icon),
                     "popup-menu",
                     G_CALLBACK(icon_popup_menu),
                     (gpointer)weather);

    entry->widget_ = GTK_WEATHER(weather);
    entry->icon_   = icon;
  }

  return entry;
}

/**
 * Frees the passed-in entry.
 *
 * @param entry Pointer to the entry to free
 */
static void
entry_free(gpointer entry)
{
  if (entry) {
    WeatherWidgetEntry * pObj = (WeatherWidgetEntry *)entry;

    LXW_LOG(LXW_DEBUG, "Destroying weather widget.");

    g_object_unref(pObj->widget_);
    g_object_unref(pObj->icon_);
      
    g_free(entry);
  }
}

/* -- main -- */
int
main(int argc, char **argv)
{
  int optindx = 0;
  int rc      = 0;

  gchar * config   = NULL;
  gchar * logfile  = NULL;
  gint    loglevel = LXW_NONE;
  
  while ((rc = getopt_long(argc, argv, "c:hf:l:", longopts, &optindx)) != -1) {
    switch (rc) {
    case 1:
    case 'h':
      usage(argv[0]);

      exit(0);

    case 2:
    case 'c':
      config = g_strndup(optarg, strlen(optarg));
      break;


    case 3:
    case 'f':
      logfile = g_strndup(optarg, strlen(optarg));
      break;
          
    case 4:
    case 'l':
      loglevel = (int)strtol(optarg, NULL, 10);

      if (loglevel < LXW_NONE || loglevel > LXW_ALL) {
        loglevel = LXW_NONE;
      }

      break;

    default:
      /* Unhandled */
      usage(argv[0]);

      exit(1);

      break;
    }
  }

  logutil_init(logfile);

  logutil_max_loglevel_set(loglevel);

  g_free(logfile);

  LXW_LOG(LXW_DEBUG, "Configuration option: %s", config);

  if (!config) {
    config = g_strdup_printf("%s%s%s%sconfig", 
                             g_get_user_config_dir(),
                             G_DIR_SEPARATOR_S,
                             APP_NAME,
                             G_DIR_SEPARATOR_S);
  }

  LXW_LOG(LXW_DEBUG, "Effective configuration: %s", config);

  /* setup our own signal handling for terminating signals */
  signal(SIGINT, sighandler);
  signal(SIGTERM, sighandler);
  signal(SIGHUP, sighandler);

  gtk_init(&argc, &argv);

  /* do some magic here */
  yahooutil_init();

  GList * list = fileutil_config_locations_load(config);

  LXW_LOG(LXW_DEBUG, "Size of configured list: %u", g_list_length(list));

  /* Initialize entry one per configured location*/
  if (g_list_length(list)) {
    GList * iter = g_list_first(list);

    while (iter) {
      /* Create entry */
      WeatherWidgetEntry * entry = entry_new();

      if (entry) {
        g_widgetentrylist = g_list_append(g_widgetentrylist, entry);

        /* set location based on config file entry */
        GValue location = G_VALUE_INIT;
        g_value_init(&location, G_TYPE_POINTER);
              
        LocationInfo * info = iter->data;

        g_value_set_pointer(&location, info);
              
        g_object_set_property(G_OBJECT(entry->widget_),
                              "location",
                              &location);
      }

      iter = g_list_next(iter);
    }
  } else {
    /* No entries means no location and no forecast */
    WeatherWidgetEntry * entry = entry_new();

    if (entry) {
      g_widgetentrylist = g_list_append(g_widgetentrylist, entry);

      /* this call will update the status icon accordingly */
      forecast_changed(GTK_WEATHER(entry->widget_), NULL, entry);
    }
  }

  /* Free location list from configuration */
  g_list_free_full(list, location_free);

  list = NULL;

  /* Rearrange our entry list */
  g_widgetentrylist = g_list_reverse(g_widgetentrylist);

  LXW_LOG(LXW_DEBUG,
          "Size of widget entrylist: %d",
          g_list_length(g_widgetentrylist));

  /* GTK main loop */
  gtk_main();

  /* one entry to save per widget's location */
  GList * iter = g_list_first(g_widgetentrylist);

  while (iter) {
    WeatherWidgetEntry * entry = iter->data;

    GValue location = G_VALUE_INIT;
    g_value_init(&location, G_TYPE_POINTER);

    g_object_get_property(G_OBJECT(entry->widget_),
                          "location",
                          &location);

    list = g_list_prepend(list, g_value_get_pointer(&location));

    iter = g_list_next(iter);
  }

  LXW_LOG(LXW_DEBUG, "Length of save list: %d", g_list_length(list));

  if (g_list_length(list)) {
    fileutil_config_locations_save(list, config);
  }

  /* The local list does NOT own the data in each entry, do not free_full */
  g_list_free(list);

  list = NULL;

  g_list_free_full(g_widgetentrylist, entry_free);

  g_widgetentrylist = NULL;

  g_free(config);

  yahooutil_cleanup();

  LXW_LOG(LXW_DEBUG, "Done.");

  logutil_cleanup();

  return EXIT_SUCCESS;
}

