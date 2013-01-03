/**
 * Copyright (c) 2012-2013 Piotr Sipika; see the AUTHORS file for more.
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

#include <gtk/gtk.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <getopt.h>
#include <signal.h>

#define APP_NAME "lxweather"

/* signal handler to insure cleanup */
static void
sighandler(int signal)
{
  char * pcSigName = NULL;

  switch (signal)
    {
    case SIGINT:
      pcSigName = "SIGINT";
      break;

    case SIGKILL:
      pcSigName = "SIGKILL";
      break;

    case SIGTERM:
      pcSigName = "SIGTERM";
      break;

    case SIGHUP:
      pcSigName = "SIGHUP";
      break;

    default:
      pcSigName = "UNHANDLED";
      break;
    }

  fprintf(stderr, "LXWeather: signal caught: %s [%d]\n",
          pcSigName, signal);

  gtk_main_quit();
}

/* long options */
static struct option longOptions[] =
{
  {"help", 0, NULL, 1},
  {"config", 1, NULL, 2},
  {"logfile", 1, NULL, 3},
  {"loglevel", 1, NULL, 4},
  {NULL, 0, NULL, 0}
};

/* Wrapper around the weather widget/status icon pair */
typedef struct
{
  GtkWeather    * pWeatherWidget_;
  GtkStatusIcon * pStatusIcon_;
} WeatherWidgetEntry;

/* List with WeatherWidgetEntry entries */
static GList * pWidgetEntryList = NULL;

static gpointer entry_find(const GtkWeather * pWeather);

/**
 * Prints out the usage help text.
 *
 * @param pczProgName Pointer to the character constant with the name
 *                    of the executable being ran.
 */
static void
usage(const char *pczProgName)
{
  fprintf(stderr, "Usage: %s [OPTIONs]\n", pczProgName);
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
location_changed(GtkWeather * weather, gpointer location, gpointer data)
{
  /* To avoid compilation warning */
  (void)weather;

  WeatherWidgetEntry * pEntry = (WeatherWidgetEntry *)data; //entry_find(weather);

  LXW_LOG(LXW_DEBUG, "main::location_changed: %p, %p, %p, %p", 
          location, pEntry, (pEntry)?pEntry->pWeatherWidget_:NULL, data);

  if (pEntry)
    {
      gtk_status_icon_set_from_stock(pEntry->pStatusIcon_, GTK_STOCK_DIALOG_WARNING);
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
  WeatherWidgetEntry * pEntry = (WeatherWidgetEntry *)data; //entry_find(weather);

  LXW_LOG(LXW_DEBUG, "main::forecast_changed: %p, %p, %p", 
          forecast, pEntry, (pEntry)?pEntry->pWeatherWidget_:NULL);
  
  if (pEntry)
    {
      if (forecast)
        {
          LXW_LOG(LXW_ERROR, "Setting status icon.");
          gtk_status_icon_set_from_pixbuf(pEntry->pStatusIcon_,
                                          ((ForecastInfo *)forecast)->pImage_);
        }
      else
        {
          LXW_LOG(LXW_ERROR, "Setting status icon STOCK.");
          gtk_status_icon_set_from_stock(pEntry->pStatusIcon_,
                                         GTK_STOCK_DIALOG_WARNING);
        }

      LXW_LOG(LXW_ERROR, "Getting status tooltip.");
      gchar * tooltip_text = gtk_weather_get_tooltip_text(GTK_WIDGET(weather));

      LXW_LOG(LXW_ERROR, "Setting status tooltip.");
      gtk_status_icon_set_tooltip_text(pEntry->pStatusIcon_, tooltip_text);

      LXW_LOG(LXW_ERROR, "Setting icon visible %d, %d.", 
              gtk_status_icon_get_visible(pEntry->pStatusIcon_),
              gtk_status_icon_is_embedded(pEntry->pStatusIcon_));

      //gtk_status_icon_set_visible(pEntry->pStatusIcon_, TRUE);

      LXW_LOG(LXW_ERROR, "free tooltip.");
      g_free(tooltip_text);
    }

  LXW_LOG(LXW_ERROR, "...Done.");  
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
  (void)icon;

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
  WeatherWidgetEntry * pEntry = (WeatherWidgetEntry *) g_try_new0(WeatherWidgetEntry, 1);

  if (pEntry)
    {
      GtkWidget * pWeather = gtk_weather_new(TRUE);

      g_object_ref_sink(pWeather);

      g_signal_connect(G_OBJECT(pWeather),
                       "location-changed",
                       G_CALLBACK(location_changed),
                       (gpointer)pEntry);
      
      g_signal_connect(G_OBJECT(pWeather),
                       "forecast-changed",
                       G_CALLBACK(forecast_changed),
                       (gpointer)pEntry);
      
      GtkStatusIcon * pStatusIcon = gtk_status_icon_new_from_stock(GTK_STOCK_DIALOG_ERROR);

      g_object_ref_sink(pStatusIcon);

      g_signal_connect(G_OBJECT(pStatusIcon),
                       "activate",
                       G_CALLBACK(icon_activate),
                       (gpointer)pWeather);
      
      g_signal_connect(G_OBJECT(pStatusIcon),
                       "popup-menu",
                       G_CALLBACK(icon_popup_menu),
                       (gpointer)pWeather);

      pEntry->pWeatherWidget_ = GTK_WEATHER(pWeather);
      pEntry->pStatusIcon_ = pStatusIcon;
    }

  return pEntry;
}

/**
 * Frees the passed-in entry.
 *
 * @param pEntry Pointer to the entry to free
 */
static void
entry_free(gpointer pEntry)
{
  if (pEntry)
    {
      WeatherWidgetEntry * pData = (WeatherWidgetEntry *)pEntry;

      LXW_LOG(LXW_DEBUG, "Destroying weather widget.");

      g_object_unref(pData->pWeatherWidget_);
      g_object_unref(pData->pStatusIcon_);
      
      g_free(pEntry);
    }
}

/**
 * Finds the entry with the specified widget
 *
 * @param pWeather Pointer to the Weather widget to find in the list.
 *
 * @return the entry containing the specified widget pointer, or NULL
 *         if not found
 */
static gpointer
entry_find(const GtkWeather * pWeather)
{
  gpointer pRetVal = NULL;

  GList * iter = g_list_first(pWidgetEntryList);

  while (iter)
    {
      WeatherWidgetEntry * pEntry = (WeatherWidgetEntry *) iter->data;

      if (pEntry && pEntry->pWeatherWidget_ == pWeather)
        {
          pRetVal = pEntry;

          break;
        }

      iter = g_list_next(iter);
    }

  return pRetVal;
}

/* -- main -- */
int
main(int argc, char **argv)
{
  int iIndex = 0;
  int iRetVal = 0;

  gchar * pcConfiguration = NULL;
  gchar * pcLogFile = NULL;
  gint  iLogLevel = LXW_NONE;
  
  while ((iRetVal = getopt_long(argc, argv, "c:hf:l:", longOptions, &iIndex)) != -1)
    {
      switch (iRetVal)
        {
        case 1:
        case 'h':
          usage(argv[0]);

          exit(0);

        case 2:
        case 'c':
          pcConfiguration = g_strndup(optarg, strlen(optarg));
          break;


        case 3:
        case 'f':
          pcLogFile = g_strndup(optarg, strlen(optarg));
          break;
          
        case 4:
        case 'l':
          iLogLevel = (int)strtol(optarg, NULL, 10);

          if (iLogLevel < LXW_NONE || iLogLevel > LXW_ALL)
            {
              iLogLevel = LXW_NONE;
            }

          break;

        default:
          /* Unhandled */
          usage(argv[0]);

          exit(1);

          break;
        }
    }

  initializeLogUtil(pcLogFile);

  setMaxLogLevel(iLogLevel);

  g_free(pcLogFile);

  LXW_LOG(LXW_DEBUG, "Configuration option: %s", pcConfiguration);

  if (!pcConfiguration)
    {
      pcConfiguration = g_strdup_printf("%s%s%s%sconfig", 
                                        g_get_user_config_dir(),
                                        G_DIR_SEPARATOR_S,
                                        APP_NAME,
                                        G_DIR_SEPARATOR_S);
    }

  LXW_LOG(LXW_DEBUG, "Effective configuration: %s", pcConfiguration);

  /* setup our own signal handling for terminating signals */
  signal(SIGINT, sighandler);
  signal(SIGKILL, sighandler);
  signal(SIGTERM, sighandler);
  signal(SIGHUP, sighandler);

  gtk_init(&argc, &argv);

  /* do some magic here */
  initializeYahooUtil();

  GList * pList = getLocationsFromConfiguration(pcConfiguration);

  LXW_LOG(LXW_DEBUG, "Size of configured list: %u", g_list_length(pList));

  /* Initialize entry one per configured location*/
  if (g_list_length(pList))
    {
      GList * iter = g_list_first(pList);

      while (iter)
        {
          /* Create entry */
          WeatherWidgetEntry * pEntry = entry_new();

          if (pEntry)
            {
              pWidgetEntryList = g_list_append(pWidgetEntryList, pEntry);

              /* set location based on config file entry */
              GValue location = G_VALUE_INIT;
              g_value_init(&location, G_TYPE_POINTER);
              
              LocationInfo * pInfo = iter->data;

              g_value_set_pointer(&location, pInfo);
              
              g_object_set_property(G_OBJECT(pEntry->pWeatherWidget_),
                                    "location",
                                    &location);
            }

          iter = g_list_next(iter);
        }
    }
  else
    {
      /* No entries means no location and no forecast */
      WeatherWidgetEntry * pEntry = entry_new();

      if (pEntry)
        {
          pWidgetEntryList = g_list_append(pWidgetEntryList, pEntry);

          forecast_changed(GTK_WEATHER(pEntry->pWeatherWidget_), NULL, pEntry);
        }
    }

  /* Free location list from configuration */
  g_list_free_full(pList, freeLocation);

  pList = NULL;

  /* Rearrange our entry list */
  pWidgetEntryList = g_list_reverse(pWidgetEntryList);

  LXW_LOG(LXW_DEBUG, "Size of widget entrylist: %d", g_list_length(pWidgetEntryList));

  /* GTK main loop */
  gtk_main();

  /* one entry to save per widget's location */
  GList * iter = g_list_first(pWidgetEntryList);

  while (iter)
    {
      WeatherWidgetEntry * pEntry = iter->data;

      GValue location = G_VALUE_INIT;
      g_value_init(&location, G_TYPE_POINTER);

      g_object_get_property(G_OBJECT(pEntry->pWeatherWidget_),
                        "location",
                        &location);

      pList = g_list_prepend(pList, g_value_get_pointer(&location));

      iter = g_list_next(iter);
    }

  LXW_LOG(LXW_DEBUG, "Length of save list: %d", g_list_length(pList));

  if (g_list_length(pList))
    {
      saveLocationsToConfiguration(pList, pcConfiguration);
    }

  /* The list does NOT own the data in each entry,
   * no reason to free_full */
  g_list_free(pList);

  pList = NULL;

  g_list_free_full(pWidgetEntryList, entry_free);

  pWidgetEntryList = NULL;

  g_free(pcConfiguration);

  cleanupYahooUtil();

  LXW_LOG(LXW_DEBUG, "Done.");

  cleanupLogUtil();

  return EXIT_SUCCESS;
}

