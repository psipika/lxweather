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

#include "location.h"
#include "forecast.h"
#include "yahooutil.h"
#include "weatherwidget.h"
#include "logutil.h"

#include <glib/gi18n.h>
#include <gdk/gdkkeysyms.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>

/* Using pthreads instead of glib's due to cancellability and API stability */
#include <pthread.h>

/* Private structure, property and signal definitions. */
#define GTK_WEATHER_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE ((obj), \
                                      GTK_WEATHER_TYPE, GtkWeatherPrivate))

/* This will exit the app gracefully with debug and without */
#ifdef DEBUG
#define LOG_ERRNO(e, msg)                                   \
  do {                                                      \
    errno = e;                                              \
    logutil_log(LXW_ERROR, "%s: %s", msg, strerror(errno)); \
    gtk_main_quit();                                        \
  } while (0)
#else
#define LOG_ERRNO(e, msg) gtk_main_quit()
#endif

#define GTK_WEATHER_NAME "GtkWeather"
#define GTK_WEATHER_NOT_AVAILABLE_LABEL _("[N/A]")

typedef struct _GtkWeatherPrivate     GtkWeatherPrivate;
typedef struct _LocationThreadData    LocationThreadData;
typedef struct _ForecastThreadData    ForecastThreadData;
typedef struct _PopupMenuData         PopupMenuData;
typedef struct _PreferencesDialogData PreferencesDialogData;
typedef struct _ConditionsDialogData  ConditionsDialogData;

enum
{
  CITY_COLUMN = 0,
  STATE_COLUMN,
  COUNTRY_COLUMN,
  MAX_COLUMNS
};

struct _PopupMenuData
{
  GtkWidget * menu;
  GtkWidget * refresh_item;
  GtkWidget * preferences_item;
  GtkWidget * quit_item;
};

struct _PreferencesDialogData
{
  gboolean    shown;
  GtkWidget * dialog;
  GtkWidget * location_label;
  GtkWidget * location_button;
  GtkWidget * alias_entry;
  GtkWidget * c_button;
  GtkWidget * f_button;
  GtkWidget * manual_button;
  GtkWidget * auto_button;
  GtkWidget * auto_spin_button;
};

struct _ConditionsDialogData
{
  gboolean    shown;
  GtkWidget * dialog;
  GtkWidget * location_text_label;
  GtkWidget * update_text_label;
  GtkWidget * windchill_text_label;
  GtkWidget * humidity_text_label;
  GtkWidget * pressure_text_label;
  GtkWidget * visibility_text_label;
  GtkWidget * wind_text_label;
  GtkWidget * sunrise_text_label;
  GtkWidget * sunset_text_label;
  GtkWidget * conditions_text_label;
  GtkWidget * conditions_image;
};

// @TODO: consider the use cases:
// 1. updating location when forecast is being retrieved
// 2. updating forecast when location is being changed (does this matter?)
// 3.
//   It looks like just a single mutex/cond pair would be sufficient.
//   Only lock the mutex once the user selects the location to set --
//   this way, once set, unlock the mutex and signal the condition variable.

// @TODO: make sure the rwlock is locked on
//     1) location set/copy
//     2) forecast retrieval/removal
//     3) save (?)

struct _LocationThreadData
{
  pthread_t      * tid; // @TODO: remember to not make this a pointer
  gchar          * location;
  GtkProgressBar * progress_bar;
  GtkWidget      * progress_dialog;
  volatile gint    active;
};

struct _ForecastThreadData
{
  pthread_t       thread;
  gint            timerid;
  volatile gint   active;    // 1 = should run, 0 = should stop
};

struct _GtkWeatherPrivate
{
  /* Main Widget Box layout */
  GtkWidget * hbox;
  GtkWidget * image;
  GtkWidget * label;

  /* Menus and dialogs */
  PopupMenuData         menu_data;
  PreferencesDialogData preferences_data;
  ConditionsDialogData  conditions_data;
  
  /* Internal data */
  gpointer    previous_location;
  gpointer    location;
  gpointer    forecast;

  /* Mutex goes with the condition variable */
  pthread_mutex_t  mutex;
  pthread_cond_t   cond;

  /* RWLock is for both the location and forecast, they're a pair */
  pthread_rwlock_t rwlock;
  
  /* Data for location and forecast retrieval threads */
  LocationThreadData location_data;
  ForecastThreadData forecast_data;
};

enum
{
  LOCATION_CHANGED_SIGNAL,
  FORECAST_CHANGED_SIGNAL,
  LAST_SIGNAL
};

enum
{
  PROP_0,
  PROP_LOCATION,
  PROP_FORECAST
};

static guint gtk_weather_signals[LAST_SIGNAL] = {0};

/* Function declarations. */
static void gtk_weather_class_init    (GtkWeatherClass * klass);
static void gtk_weather_init          (GtkWeather * weather);
static void gtk_weather_render        (GtkWeather * weather);
static void gtk_weather_size_allocate (GtkWidget * widget, GtkAllocation * allocation);

static void gtk_weather_destroy       (GObject * object);

static void gtk_weather_set_property (GObject * object, guint prop_id,
                                      const GValue * value, GParamSpec * param_spec);
static void gtk_weather_get_property (GObject * object, guint prop_id,
                                      GValue * value, GParamSpec * param_spec);

static void gtk_weather_set_location (GtkWeather * weather, gpointer location, gboolean backup);
static void gtk_weather_set_forecast (GtkWeather * weather, gpointer forecast);

static gboolean gtk_weather_button_pressed  (GtkWidget * widget, GdkEventButton * event);
static gboolean gtk_weather_key_pressed     (GtkWidget * widget, GdkEventKey * event, gpointer data);
static gboolean gtk_weather_change_location (GtkWidget * widget, GdkEventButton * event);

static void gtk_weather_auto_update_toggled (GtkWidget * widget);

static void gtk_weather_create_popup_menu            (GtkWeather * weather);
static void gtk_weather_set_window_icon              (GtkWindow * window, gchar * icon_id);
static void gtk_weather_show_location_progress_bar   (GtkWeather * weather);
static void gtk_weather_show_location_list           (GtkWeather * weather, GList * list);
static void gtk_weather_update_preferences_dialog    (GtkWeather * weather);
static GtkWidget * gtk_weather_create_preferences_dialog(GtkWidget * widget);

static void gtk_weather_create_conditions_dialog (GtkWeather * weather);
static void gtk_weather_update_conditions_dialog (GtkWeather * weather);

static gint gtk_weather_forecast_thread_start (GtkWeather * weather);
static gint gtk_weather_forecast_thread_stop  (GtkWeather * weather);
static void * gtk_weather_get_forecast_threadfunc (void * arg);

static void gtk_weather_get_forecast (GtkWidget * widget);

static void gtk_weather_run_error_dialog (GtkWindow * parent, gchar * error_msg);

static gboolean gtk_weather_update_location_progress_bar (gpointer data);
static gboolean gtk_weather_update_ui                    (gpointer data);

static void * gtk_weather_get_location_threadfunc  (void * arg);
static gboolean gtk_weather_get_forecast_timerfunc (gpointer data);


/* Function definitions. */

/**
 * Provides the type definition for this widget.
 *
 * @return The type identifier for this widget.
 */
GType
gtk_weather_get_type(void)
{
  /*
   * Normally, the variable below is declared static and initialized to 0.
   * However, when dealing with lxpanel, the type remains registered,
   * while this widget class is removed from scope.
   * This means that the variable below goes out of scope, BUT the type
   * remains registered with GTK.
   * Hence, g_type_from_name...
   */
  GType gtk_weather_type = g_type_from_name(GTK_WEATHER_NAME);

  LXW_LOG(LXW_DEBUG, "GtkWeather::get_type(): %lu", (gulong)gtk_weather_type);

  if (!gtk_weather_type) {
    static const GTypeInfo gtk_weather_info =
      {
        sizeof(GtkWeatherClass),
        NULL,
        NULL,
        (GClassInitFunc)gtk_weather_class_init,
        NULL,
        NULL,
        sizeof(GtkWeather),
        0,
        (GInstanceInitFunc)gtk_weather_init,
        NULL
      };

    gtk_weather_type = g_type_register_static(GTK_TYPE_EVENT_BOX,
                                              GTK_WEATHER_NAME,
                                              &gtk_weather_info,
                                              0);

  }

  return gtk_weather_type;
}

/**
 * Returns a new instance of this widget.
 *
 * @return A new instance of this widget type.
 */
GtkWidget *
gtk_weather_new()
{
  GObject * object = g_object_new(gtk_weather_get_type(), NULL);
  
  return GTK_WIDGET(object);
}

/**
 * Initializes this widget's class internals.
 *
 * @param klass Pointer to this widget's class.
 */
static void
gtk_weather_class_init(GtkWeatherClass * klass)
{
  GObjectClass   * gobject_class = (GObjectClass *)klass;
  GtkWidgetClass * widget_class  = (GtkWidgetClass *)klass;

  gobject_class->set_property = gtk_weather_set_property;
  gobject_class->get_property = gtk_weather_get_property;
  gobject_class->finalize     = gtk_weather_destroy;

  //widget_class->expose_event = gtk_weather_expose;
  //widget_class->size_request = gtk_weather_size_request;
  widget_class->size_allocate      = gtk_weather_size_allocate;
  widget_class->button_press_event = gtk_weather_button_pressed;
  
  g_type_class_add_private(klass, sizeof(GtkWeatherPrivate));

  g_object_class_install_property(gobject_class, PROP_LOCATION,
                                  g_param_spec_pointer("location",
                                                       "Current Location",
                                                       "Current Location",
                                                       G_PARAM_READWRITE));

  g_object_class_install_property(gobject_class, PROP_FORECAST,
                                  g_param_spec_pointer("forecast",
                                                       "Current Conditions",
                                                       "Current conditions and forecast",
                                                       G_PARAM_READWRITE));

  gtk_weather_signals[LOCATION_CHANGED_SIGNAL] =
    g_signal_new("location-changed",
                 G_TYPE_FROM_CLASS(klass),
                 G_SIGNAL_RUN_FIRST | G_SIGNAL_ACTION,
                 G_STRUCT_OFFSET(GtkWeatherClass, location_changed),
                 NULL,
                 NULL,
                 g_cclosure_marshal_VOID__POINTER,
                 G_TYPE_NONE,
                 1,
                 G_TYPE_POINTER);

  gtk_weather_signals[FORECAST_CHANGED_SIGNAL] =
    g_signal_new("forecast-changed",
                 G_TYPE_FROM_CLASS(klass),
                 G_SIGNAL_RUN_FIRST | G_SIGNAL_ACTION,
                 G_STRUCT_OFFSET(GtkWeatherClass, forecast_changed),
                 NULL,
                 NULL,
                 g_cclosure_marshal_VOID__POINTER,
                 G_TYPE_NONE,
                 1,
                 G_TYPE_POINTER);
}

/**
 * Initializes this widget's instance.
 *
 * @param weather Pointer to this widget's instance.
 */
static void
gtk_weather_init(GtkWeather * weather)
{
  LXW_LOG(LXW_DEBUG, "GtkWeather::init()");

  GtkWeatherPrivate * priv = GTK_WEATHER_GET_PRIVATE(weather);

  /* Initialize thread/sync primitives */
   LocationThreadData * ltdata = &(priv->location_data);
   ForecastThreadData * ftdata = &(priv->forecast_data);

  ltdata->active = 0;
  
  ftdata->timerid = 0;
  ftdata->active  = 0;
      
  if (pthread_mutex_init(&(priv->mutex), NULL) ||
      pthread_cond_init(&(priv->cond), NULL) ||
      pthread_rwlock_init(&(priv->rwlock), NULL)) {
    LOG_ERRNO(errno,
              "gtk_weather_new(): could not initialize threading primitives.");
  }

  /* Box layout internals */
  priv->hbox = gtk_hbox_new(FALSE, 1);

  priv->image = gtk_image_new_from_stock(GTK_STOCK_DIALOG_ERROR,
                                         GTK_ICON_SIZE_BUTTON);

  priv->label = gtk_label_new(GTK_WEATHER_NOT_AVAILABLE_LABEL);

  gtk_box_pack_start(GTK_BOX(priv->hbox),
                     priv->image,
                     FALSE,
                     FALSE,
                     2);

  gtk_box_pack_start(GTK_BOX(priv->hbox),
                     priv->label,
                     FALSE,
                     FALSE,
                     0);

  gtk_container_add(GTK_CONTAINER(weather), priv->hbox);

  gtk_container_set_border_width(GTK_CONTAINER(weather), 2);

  /* Popup menu */
  gtk_weather_create_popup_menu(weather);

  /* Adjust size of label and icon inside */
  gtk_weather_render(weather);
}

/**
 * Destroys the weather widget object
 *
 * @param object Pointer to this widget's instance cast as a GObject
 */
static void
gtk_weather_destroy(GObject * object)
{
  LXW_LOG(LXW_DEBUG, "GtkWeather::destroy()");

  g_return_if_fail(object != NULL);
  g_return_if_fail(IS_GTK_WEATHER(object));

  GtkWeather * weather = GTK_WEATHER(object);

  GtkWeatherPrivate * priv = GTK_WEATHER_GET_PRIVATE(weather);

  if (gtk_weather_forecast_thread_stop(weather)) {
    LXW_LOG(LXW_DEBUG, "Could not stop forecast thread.");
  }
  
  /* Need to free location and forecast. */
  location_free(priv->previous_location);
  location_free(priv->location);
  forecast_free(priv->forecast);

  if (priv->menu_data.menu && GTK_IS_WIDGET(priv->menu_data.menu)) {
    gtk_widget_destroy(priv->menu_data.menu);
  }

  if (priv->hbox && GTK_IS_WIDGET(priv->hbox)) {
    gtk_widget_destroy(priv->hbox);
  }

  if (priv->image && GTK_IS_WIDGET(priv->image)) {
    gtk_widget_destroy(priv->image);
  }

  if (priv->label && GTK_IS_WIDGET(priv->label))  {
    gtk_widget_destroy(priv->label);
  }

}

/**
 * Makes the requested allocation happen for this widget.
 *
 * @param widget     Pointer to the instance of this widget.
 * @param allocation Pointer to the allocation being done.
 */
static void
gtk_weather_size_allocate(GtkWidget * widget, GtkAllocation * allocation)
{
  /*  g_return_if_fail(widget != NULL || allocation != NULL);
      g_return_if_fail(IS_GTK_WEATHER(widget));*/
  if (!widget || !allocation || !IS_GTK_WEATHER(widget)) {
    return;
  }

  GtkWeatherPrivate * priv = GTK_WEATHER_GET_PRIVATE(GTK_WEATHER(widget));

  LXW_LOG(LXW_DEBUG, "GtkWeather::size_allocate(%d): x: %d, y: %d, %dx%d (x: %d, y: %d, %dx%d)", 
          gtk_widget_get_has_window(widget),
          allocation->x, allocation->y, allocation->width, allocation->height,
          widget->allocation.x, widget->allocation.y, 
          widget->allocation.width, widget->allocation.height);

  /* check new allocation against previous one (height), 
     if they don't match, make a new icon...
     this is done inside gtk_weather_render() function
   */

  widget->allocation = *allocation;

  gboolean weather_has_window = gtk_widget_get_has_window(widget);

  if (GTK_WIDGET_REALIZED(widget) && weather_has_window) {
    gdk_window_move_resize(widget->window, 
                           allocation->x, 
                           allocation->y, 
                           allocation->width, 
                           allocation->height);
  }
   
  GtkAllocation box_allocation;

  /* we know the private hbox doesn't have a window */
  box_allocation.x = 0;
  box_allocation.y = 0;

  /* but in case we don't, either, let's make sure 
   * the box appears correctly...
   */
  if (!weather_has_window) {
    box_allocation.x = allocation->x;
    box_allocation.y = allocation->y;
  }

  box_allocation.height = allocation->height;
  box_allocation.width = allocation->width;

  gtk_widget_size_allocate(GTK_WIDGET(priv->hbox), &box_allocation);
}

/**
 * Helper function to update the widget based on internal change.
 *
 * @param weather Pointer to the instance of this widget.
 */
static void
gtk_weather_render(GtkWeather * weather)
{
  GtkWeatherPrivate * priv = GTK_WEATHER_GET_PRIVATE(weather);
 
  LXW_LOG(LXW_DEBUG, "GtkWeather::render(): location: %p, forecast: %p",
          priv->location, priv->forecast);

  if (pthread_rwlock_rdlock(&(priv->rwlock)) == 0) {
    if (priv->location && priv->forecast) {
      ForecastInfo * forecast = (ForecastInfo *)priv->forecast;

      GtkRequisition req;

      gtk_widget_size_request(GTK_WIDGET(priv->hbox), &req);

      /* req will hold valid data for painted widget, so disregard if we're
       * running in a single app 
       */
      if (req.height) {          
        /* set this image to the one in the forecast at correct scale */
        GdkPixbuf * forecast_pixbuf = gdk_pixbuf_scale_simple(forecast->image_,
                                                              req.height,
                                                              req.height,
                                                              GDK_INTERP_BILINEAR);
        
        gtk_image_set_from_pixbuf(GTK_IMAGE(priv->image), forecast_pixbuf);
        
        if (G_IS_OBJECT(forecast_pixbuf)) {
          g_object_unref(forecast_pixbuf);
        }
      }

      /* update the label with proper temperature */
      gchar * temperature = g_strdup_printf("%d \302\260%s", 
                                            forecast->temperature_,
                                            forecast->units_.temperature_);

      gtk_label_set_text(GTK_LABEL(priv->label), temperature);
      
      //gtk_widget_show_all(priv->hbox);

      g_free(temperature);
    } else {
      /* N/A */
      if (priv->location) {
        gtk_image_set_from_stock(GTK_IMAGE(priv->image), 
                                 GTK_STOCK_DIALOG_WARNING, 
                                 GTK_ICON_SIZE_BUTTON);
      } else {
        gtk_image_set_from_stock(GTK_IMAGE(priv->image), 
                                 GTK_STOCK_DIALOG_ERROR, 
                                 GTK_ICON_SIZE_BUTTON);
      }
      
      gtk_label_set_text(GTK_LABEL(priv->label), 
                         GTK_WEATHER_NOT_AVAILABLE_LABEL);
    }
    
    /* update tooltip with proper data... */
    gchar * tooltip_text = gtk_weather_get_tooltip_text(GTK_WIDGET(weather));

    pthread_rwlock_unlock(&(priv->rwlock));

    gtk_widget_set_tooltip_text(GTK_WIDGET(weather), tooltip_text);

    g_free(tooltip_text);
  }
}

/* Property access functions */
/**
 * Sets the specified property.
 *
 * @param object     Pointer to the GObject instance of this widget.
 * @param prop_id    Property Id of the property to set.
 * @param value      Pointer to the GValue containing actual value to use.
 * @param param_spec Pointer to GParamSpec structure for this property.
 */
static void
gtk_weather_set_property(GObject * object,
                         guint     prop_id,
                         const GValue * value,
                         GParamSpec   * param_spec)
{
  GtkWeather * weather = GTK_WEATHER(object);

  LXW_LOG(LXW_DEBUG, "GtkWeather::set_property(%u - %s)", prop_id,
          ((prop_id == PROP_LOCATION)?"location":
           (prop_id == PROP_FORECAST)?"forecast":"???"));

  switch (prop_id) {
  case PROP_LOCATION:
    gtk_weather_set_location(weather, g_value_get_pointer(value), TRUE);

    /* The function starts the forecast thread if the location is 'enabled' */
    gtk_weather_forecast_thread_start(weather);
    break;

  case PROP_FORECAST:
    gtk_weather_set_forecast(weather, g_value_get_pointer(value));
    break;

  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, param_spec);
    break;
  }

}

/**
 * Gets the specified property.
 *
 * @param object     Pointer to the GObject instance of this widget.
 * @param prop_id    Property Id of the property to get.
 * @param value      Pointer to the GValue to set with actual value.
 * @param param_spec Pointer to GParamSpec structure for this property.
 */
static void
gtk_weather_get_property(GObject * object, 
                         guint prop_id, 
                         GValue * value, 
                         GParamSpec * param_spec)
{
  GtkWeather * weather = GTK_WEATHER(object);
  GtkWeatherPrivate * priv = GTK_WEATHER_GET_PRIVATE(weather);

  switch (prop_id) {
  case PROP_LOCATION:
    g_value_set_pointer(value, priv->location);
    break;

  case PROP_FORECAST:
    g_value_set_pointer(value, priv->forecast);
    break;

  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, param_spec);
    break;
  }

}

/**
 * Sets the location property pointer for this widget.
 *
 * @param weather  Pointer to the instance of this widget.
 * @param location Location to use.
 * @param backup   Whether to make a backup copy of the current location.
 *
 */
static void
gtk_weather_set_location(GtkWeather * weather, gpointer location, gboolean backup)
{
  GtkWeatherPrivate * priv = GTK_WEATHER_GET_PRIVATE(weather);

  LXW_LOG(LXW_DEBUG, "GtkWeather::set_location(): current: %p, new: %p",
          priv->location, location);

#ifdef DEBUG
  location_print(priv->location);
  location_print(location);
#endif

  if (pthread_rwlock_wrlock(&(priv->rwlock)) == 0) {
    if (backup) {
      /* Set previous location, to save it. */
      location_copy(&priv->previous_location, priv->location);
    }

    if (location) {
      location_copy(&priv->location, location);

      pthread_rwlock_unlock(&(priv->rwlock));

      /* reset forecast */
      gtk_weather_set_forecast(weather, NULL);

      /* weather is rendered inside */
    } else {
      location_free(priv->location);

      priv->location = NULL;

      pthread_rwlock_unlock(&(priv->rwlock));
      
      gtk_weather_render(weather);
    }
  }

  /* This will signal a conditional variable, not get the forecast.
   * It will also start the timer, if the location is configured for it
   */
  gtk_weather_get_forecast(GTK_WIDGET(weather));

  /* Emit location-changed event */
  g_signal_emit_by_name(weather, "location-changed", location);
}

/**
 * Sets the forecast property pointer for this widget.
 *
 * @param weather  Pointer to the instance of this widget.
 * @param forecast Forecast to use.
 *
 * @note I realize there is a slight inconsistency here:
 *       The set_location() function properly sets the location, while this
 *       function (set_forecast()) does not.  This is because the forecast
 *       is being modified in-place inside its thread function.
 *       It is not the most nimble approach and should be changed in the future.
 */
static void
gtk_weather_set_forecast(GtkWeather * weather, gpointer forecast)
{
  GtkWeatherPrivate * priv = GTK_WEATHER_GET_PRIVATE(weather);

  LXW_LOG(LXW_DEBUG, "GtkWeather::set_forecast(): current: %p, new: %p",
          priv->forecast, forecast);

#ifdef DEBUG
  forecast_print(priv->forecast);
  forecast_print(forecast);
#endif

  if (pthread_rwlock_wrlock(&(priv->rwlock)) == 0) {
    if (!forecast) {
      forecast_free(priv->forecast);

      priv->forecast = NULL;
    }
    
    pthread_rwlock_unlock(&(priv->rwlock));
  }

  gtk_weather_render(weather);

  /* Emit forecast-changed event */
  g_signal_emit_by_name(weather, "forecast-changed", forecast);

  /* Update the conditions dialog, if shown */
  if (priv->conditions_data.shown) {
    gtk_weather_update_conditions_dialog(weather);
  }
}


/* Action callbacks (button/cursor/key) */
/**
 * Handles the button-pressed event.
 *
 * @param widget Pointer to the instance on which the event occurred.
 * @param event  Pointer to the event structure with details.
 *
 * @return TRUE if the event should not be propagated further, FALSE otherwise.
 */
static gboolean
gtk_weather_button_pressed(GtkWidget * widget, GdkEventButton * event)
{
  LXW_LOG(LXW_DEBUG, "GtkWeather::button_pressed(): Button: %d, type: %d", 
          event->button, event->type);

  /* If right-clicked, show popup */
  if (event->button == 3 && (event->type == GDK_BUTTON_PRESS)) {
    gtk_weather_run_popup_menu(widget);

    return TRUE;
  } else if (event->button == 1 && (event->type == GDK_BUTTON_PRESS)) {
    gtk_weather_run_conditions_dialog(widget);
  }

  return TRUE;
}

/**
 * Handles the toggled event for auto/manual radio buttons
 * 
 * @param widget Poitner to the instance of this widget
 */
static void 
gtk_weather_auto_update_toggled(GtkWidget * widget)
{
  LXW_LOG(LXW_DEBUG, "GtkWeather::auto_update_toggled()");

  GtkWeatherPrivate * priv = GTK_WEATHER_GET_PRIVATE(GTK_WEATHER(widget));

  LocationInfo * location = (LocationInfo *) priv->location;

  if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(priv->preferences_data.auto_button)) &&
      priv->location) {
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(priv->preferences_data.manual_button),
                                 FALSE);

    gtk_widget_set_sensitive(GTK_WIDGET(priv->preferences_data.auto_spin_button), TRUE);

    gtk_spin_button_set_value(GTK_SPIN_BUTTON(priv->preferences_data.auto_spin_button), 
                              (gdouble)location->interval_);
  } else {
    gtk_widget_set_sensitive(GTK_WIDGET(priv->preferences_data.auto_spin_button), FALSE);
  }
  
}

/**
 * Handles the button-pressed event for the location set/change button.
 *
 * @param widget Pointer to the instance of this widget.
 * @param event  Pointer to the event structure with details.
 *
 * @return TRUE if the event should not be propagated further, FALSE otherwise.
 */
static gboolean
gtk_weather_change_location(GtkWidget * widget, GdkEventButton * event)
{
  LXW_LOG(LXW_DEBUG, "GtkWeather::change_location");

  /* disable compilation warning */
  (void)event;

  GtkWeatherPrivate * priv = GTK_WEATHER_GET_PRIVATE(GTK_WEATHER(widget));

  GtkWidget * dialog = gtk_dialog_new_with_buttons(_("Enter New Location"),
                                                   GTK_WINDOW(priv->preferences_data.dialog),
                                                   GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                                                   GTK_STOCK_OK, GTK_RESPONSE_ACCEPT,
                                                   GTK_STOCK_CANCEL, GTK_RESPONSE_REJECT,
                                                   NULL);

  /* Set dialog window icon */
  gtk_weather_set_window_icon(GTK_WINDOW(dialog), "gtk-properties");
  
  gtk_window_set_resizable(GTK_WINDOW(dialog), FALSE);

  gtk_dialog_set_default_response(GTK_DIALOG(dialog), GTK_RESPONSE_ACCEPT);

  GtkWidget * location_label = gtk_label_new_with_mnemonic(_("_New Location:"));

  GtkWidget * location_entry = gtk_entry_new();

  g_signal_connect(G_OBJECT(location_entry),
                   "key-press-event",
                   G_CALLBACK(gtk_weather_key_pressed),
                   (gpointer)dialog);

  GtkWidget * image = gtk_image_new_from_stock(GTK_STOCK_DIALOG_INFO, GTK_ICON_SIZE_DIALOG);

  GtkWidget * description_label = gtk_label_new(_("Enter the:\n- city, or\n- city and state/country, or\n- postal code\nfor which to retrieve the weather forecast."));

  gtk_label_set_justify(GTK_LABEL(description_label), GTK_JUSTIFY_LEFT);

  GtkWidget * entry_hbox = gtk_hbox_new(FALSE, 10);

  gtk_box_pack_start(GTK_BOX(entry_hbox), location_label, FALSE, FALSE, 5);
  gtk_box_pack_end(GTK_BOX(entry_hbox), location_entry, FALSE, FALSE, 5);

  GtkWidget * entry_vbox = gtk_vbox_new(FALSE, 10);

  gtk_box_pack_start(GTK_BOX(entry_vbox), description_label, FALSE, FALSE, 5);
  gtk_box_pack_start(GTK_BOX(entry_vbox), entry_hbox, FALSE, FALSE, 5);

  GtkWidget * label_hbox = gtk_hbox_new(FALSE, 10);

  gtk_box_pack_start(GTK_BOX(label_hbox), image, FALSE, FALSE, 5);
  gtk_box_pack_start(GTK_BOX(label_hbox), entry_vbox, FALSE, FALSE, 5);

  gtk_box_pack_start(GTK_BOX(GTK_DIALOG(dialog)->vbox), label_hbox, TRUE, FALSE, 10);

  gtk_widget_show_all(dialog);

  gint response = GTK_RESPONSE_NONE;

  do {
    response = gtk_dialog_run(GTK_DIALOG(dialog));

    /* handle ACCEPT/OK response to process new location */
    switch(response) {
    case GTK_RESPONSE_ACCEPT:
      /* location must be entered... */
      if (gtk_entry_get_text_length(GTK_ENTRY(location_entry)) == 0) {
        gtk_weather_run_error_dialog(GTK_WINDOW(dialog), 
                                     _("You must specify a location."));

        break;
      }

      gchar * new_location = g_strdup(gtk_entry_get_text(GTK_ENTRY(location_entry)));
            
      /* start thread here, let the progress bar do its own magic */
      pthread_t      tid;
      pthread_attr_t tattr;
            
      int ret = pthread_attr_init(&tattr);
            
      if (ret != 0) {
        LOG_ERRNO(ret, "pthread_attr_init");
      }

      ret = pthread_create(&tid,
                           &tattr,
                           &gtk_weather_get_location_threadfunc,
                           new_location);

      if (ret != 0) {
        LOG_ERRNO(ret, "pthread_create");
      }

      ret = pthread_attr_destroy(&tattr);
      
      if (ret != 0) {
        LOG_ERRNO(ret, "pthread_attr_destroy");
      }
      
      priv->location_data.tid      = &tid;
      priv->location_data.location = new_location;

      /* show progress bar and lookup selected location */
      gtk_weather_show_location_progress_bar(GTK_WEATHER(widget));

      void * result = NULL;
      ret = pthread_join(tid, &result);
      if (ret != 0) {
        LOG_ERRNO(ret, "pthread_join");
      }

      gchar * error_msg = g_strdup_printf(_("Location '%s' not found!"), new_location);
      
      if (result && result != PTHREAD_CANCELED) {
        GList * list = (GList *)result;
          
        guint length = g_list_length(list);

        LXW_LOG(LXW_DEBUG, "Thread returned list of length %u", length);

        if (length > 0) {
          gtk_weather_show_location_list(GTK_WEATHER(widget), list);
        } else {
          gtk_weather_run_error_dialog(GTK_WINDOW(dialog), error_msg);
        }
          
        /* Free list */
        g_list_free_full(list, location_free);
          
        /* Repaint preferences dialog */
        gtk_weather_update_preferences_dialog(GTK_WEATHER(widget));
      } else if (result == PTHREAD_CANCELED) {
        /* nothing, user canceled search... */
      } else {
        gtk_weather_run_error_dialog(GTK_WINDOW(dialog), error_msg);
      }
      
      g_free(error_msg);

      g_free(new_location);

      break;

    default:
      LXW_LOG(LXW_DEBUG, "\tdefault: %d", response);

      break;
    }
        
  } while ( (response == GTK_RESPONSE_ACCEPT) &&
            (gtk_entry_get_text_length(GTK_ENTRY(location_entry)) == 0) );

  if (GTK_IS_WIDGET(dialog)) {
    gtk_widget_destroy(dialog);
  }

  priv->location_data.tid      = 0;
  priv->location_data.location = NULL;
     
  dialog = NULL;

  return TRUE;
}

/**
 * Handles the key-pressed event.
 *
 * @param widget Pointer to the instance on which the event occurred.
 * @param event  Pointer to the event structure with details.
 * @param data   Pointer to user-data.
 *
 * @return TRUE if the event should not be propagated further, FALSE otherwise.
 */
static gboolean
gtk_weather_key_pressed(GtkWidget * widget, GdkEventKey * event, gpointer data)
{
  LXW_LOG(LXW_DEBUG, "GtkWeather::key_pressed");

  if (GTK_IS_ENTRY(widget)) {
    /* See if it's enter */
    if (event->keyval == GDK_KEY_Return ||
        event->keyval == GDK_KEY_KP_Enter) {
      /* Check length and act accordingly */
      if (gtk_entry_get_text_length(GTK_ENTRY(widget)) == 0) {
        gtk_weather_run_error_dialog(GTK_WINDOW(data), 
                                     _("You must specify a location."));
      } else {
        gtk_dialog_response(GTK_DIALOG(data), GTK_RESPONSE_ACCEPT);
      }
          
    }
  } else if (GTK_IS_BUTTON(widget)) {
    if (event->keyval == GDK_KEY_Return ||
        event->keyval == GDK_KEY_KP_Enter ||
        event->keyval == GDK_KEY_space) {
      /* Don't care about the return value or the event pointer */
      gtk_weather_change_location(GTK_WIDGET(data), NULL);
    }

  }

  return FALSE;
}

/* GTK helper functions */
/**
 * Creates and shows an error dialog.
 *
 * @param parent    Parent window pointer.
 * @param error_msg Error message to display.
 */
static void
gtk_weather_run_error_dialog(GtkWindow * parent, gchar * error_msg)
{
  LXW_LOG(LXW_DEBUG, "GtkWeather::run_error_dialog(%s)", error_msg);

  static gboolean shown = FALSE;

  if (!shown) {
    GtkWidget * error_dialog = gtk_message_dialog_new(parent,
                                                      GTK_DIALOG_MODAL,
                                                      GTK_MESSAGE_ERROR,
                                                      GTK_BUTTONS_OK,
                                                      "%s", error_msg);
      
    gtk_weather_set_window_icon(GTK_WINDOW(error_dialog), "gtk-dialog-error");
      
    shown = TRUE;

    gtk_dialog_run(GTK_DIALOG(error_dialog));

    gtk_widget_destroy(error_dialog);

    shown = FALSE;
  }
}

/**
 * Creates a pop-up menu.
 *
 * @param weather Pointer to the instance of this widget.
 */
static void
gtk_weather_create_popup_menu(GtkWeather * weather)
{
  LXW_LOG(LXW_DEBUG, "GtkWeather::create_popup_menu()");

  GtkWeatherPrivate * priv = GTK_WEATHER_GET_PRIVATE(weather);

  priv->menu_data.menu = gtk_menu_new();

  priv->menu_data.preferences_item = gtk_image_menu_item_new_with_label(_("Preferences"));
  
  gtk_image_menu_item_set_image(GTK_IMAGE_MENU_ITEM(priv->menu_data.preferences_item), 
                                gtk_image_new_from_stock(GTK_STOCK_PREFERENCES,
                                                         GTK_ICON_SIZE_MENU));
  
  priv->menu_data.refresh_item = gtk_image_menu_item_new_with_label(_("Refresh"));

  gtk_image_menu_item_set_image(GTK_IMAGE_MENU_ITEM(priv->menu_data.refresh_item), 
                                gtk_image_new_from_stock(GTK_STOCK_REFRESH,
                                                         GTK_ICON_SIZE_MENU));

  priv->menu_data.quit_item = gtk_image_menu_item_new_with_label(_("Quit"));

  gtk_image_menu_item_set_image(GTK_IMAGE_MENU_ITEM(priv->menu_data.quit_item), 
                                gtk_image_new_from_stock(GTK_STOCK_QUIT,
                                                         GTK_ICON_SIZE_MENU));

  gtk_menu_shell_append(GTK_MENU_SHELL(priv->menu_data.menu), priv->menu_data.preferences_item);

  gtk_menu_shell_append(GTK_MENU_SHELL(priv->menu_data.menu), gtk_separator_menu_item_new());

  gtk_menu_shell_append(GTK_MENU_SHELL(priv->menu_data.menu), priv->menu_data.refresh_item);

  gtk_menu_shell_append(GTK_MENU_SHELL(priv->menu_data.menu), gtk_separator_menu_item_new());

  gtk_menu_shell_append(GTK_MENU_SHELL(priv->menu_data.menu), priv->menu_data.quit_item);

  /* connect signals appropriately */
  g_signal_connect_swapped(G_OBJECT(priv->menu_data.preferences_item), 
                           "activate",
                           G_CALLBACK(gtk_weather_run_preferences_dialog),
                           GTK_WIDGET(weather));

  g_signal_connect_swapped(G_OBJECT(priv->menu_data.refresh_item),
                           "activate",
                           G_CALLBACK(gtk_weather_get_forecast),
                           GTK_WIDGET(weather));

  g_signal_connect_swapped(G_OBJECT(priv->menu_data.quit_item),
                           "activate",
                           G_CALLBACK(gtk_main_quit),
                           NULL);

  gtk_menu_attach_to_widget(GTK_MENU(priv->menu_data.menu), GTK_WIDGET(weather), NULL);

  gtk_widget_show_all(priv->menu_data.menu);
}

/**
 * Callback for the preferences menu response.
 *
 * @param dialog   Pointer to the preferences dialog.
 * @param response ID of the response action.
 * @param data     Pointer to user data (weather widget instance).
 */
void
gtk_weather_preferences_dialog_response(GtkDialog *dialog, gint response, gpointer data)
{
  (void) dialog; /* unused - to squash a warning */

  LXW_LOG(LXW_DEBUG, "GtkWeather::popup_menu(%d)", response);

  GtkWeather * weather = GTK_WEATHER(data);

  GtkWeatherPrivate * priv = GTK_WEATHER_GET_PRIVATE(weather);

  const gchar * aliasstr = NULL;
  switch(response) {
  case GTK_RESPONSE_ACCEPT:
    if (priv->location) {
      LocationInfo * location = (LocationInfo *)priv->location;

      aliasstr = gtk_entry_get_text(GTK_ENTRY(priv->preferences_data.alias_entry));

      location_property_set(priv->location,
                            "alias",
                            aliasstr,
                            (aliasstr)?strlen(aliasstr):0);
          
      location->enabled_ = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(priv->preferences_data.auto_button));

      if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON((priv->preferences_data.c_button)))) {
        location->units_ = 'c';
      } else {
        location->units_ = 'f';
      }
          
      location->interval_ = (guint)gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(priv->preferences_data.auto_spin_button));

      /* Set this location as the valid one */
      location_copy(&priv->previous_location, priv->location);
      
      /* get forecast */
      gtk_weather_get_forecast(GTK_WIDGET(weather));
    }

    break;

  case GTK_RESPONSE_REJECT:
    if (priv->previous_location) {
      gtk_weather_set_location(weather, priv->previous_location, FALSE);
    }
      
    gtk_weather_get_forecast(GTK_WIDGET(weather));

    break;
  default:
    /* Leave everything as-is*/
    break;
  }

  if (GTK_IS_WIDGET(priv->preferences_data.dialog)) {
    gtk_widget_destroy(priv->preferences_data.dialog);
  }

  priv->preferences_data.dialog = NULL;
  
  priv->preferences_data.shown = FALSE;
}

/**
 * Shows the popup menu used for configuration.
 *
 * @param widget Pointer to the current instance of the weather widget.
 */
void
gtk_weather_run_popup_menu(GtkWidget * widget)
{
  GtkWeatherPrivate * priv = GTK_WEATHER_GET_PRIVATE(GTK_WEATHER(widget));

  LXW_LOG(LXW_DEBUG, "GtkWeather::popup_menu()");
  gtk_widget_show(GTK_WIDGET(priv->menu_data.quit_item));

  /* grey-out refresh, if no location is set */
  if (!priv->location) {
    gtk_widget_set_sensitive(priv->menu_data.refresh_item, FALSE);
  } else {
    gtk_widget_set_sensitive(priv->menu_data.refresh_item, TRUE);
  }
  
  gtk_menu_popup(GTK_MENU(priv->menu_data.menu), 
                 NULL, NULL, NULL, NULL, 
                 3, // right-click
                 gtk_get_current_event_time());
}

/**
 * Creates the preferences dialog.
 *
 * @param widget Pointer to the current instance of the weather object.
 *
 * @return pointer to the preferences dialog, or NULL on failure.
 */
GtkWidget *
gtk_weather_create_preferences_dialog(GtkWidget * widget)
{
  GtkWeather * weather = GTK_WEATHER(widget);

  /* @NOTE: watch for parent window when dealing with the plugin */
  /* @TODO: connect the response signal to the proper function */
  LXW_LOG(LXW_DEBUG, "GtkWeather::create_preferences_dialog()");

  GtkWeatherPrivate * priv = GTK_WEATHER_GET_PRIVATE(weather);

  if (priv->preferences_data.shown) {
    return priv->preferences_data.dialog;
  }

  priv->preferences_data.dialog =
    gtk_dialog_new_with_buttons(_("Weather Preferences"),
                                NULL,
                                GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                                GTK_STOCK_OK, GTK_RESPONSE_ACCEPT,
                                GTK_STOCK_CANCEL, GTK_RESPONSE_REJECT,
                                NULL);

  /* Set dialog window icon */
  gtk_weather_set_window_icon(GTK_WINDOW(priv->preferences_data.dialog),
                              "gtk-preferences");

  gtk_window_set_resizable(GTK_WINDOW(priv->preferences_data.dialog), FALSE);

  gtk_dialog_set_default_response(GTK_DIALOG(priv->preferences_data.dialog),
                                  GTK_RESPONSE_ACCEPT);

  GtkWidget * location_frame = gtk_frame_new(_("Current Location"));

  GtkWidget * location_hbox = gtk_hbox_new(FALSE, 1);

  priv->preferences_data.location_label = gtk_label_new(_("None configured"));

  priv->preferences_data.location_button = gtk_button_new_with_mnemonic(_("_Set"));

  g_signal_connect(G_OBJECT(priv->preferences_data.location_button),
                   "key-press-event",
                   G_CALLBACK(gtk_weather_key_pressed),
                   (gpointer)widget);

  g_signal_connect_swapped(G_OBJECT(priv->preferences_data.location_button),
                           "button-press-event", 
                           G_CALLBACK(gtk_weather_change_location),
                           GTK_WIDGET(weather));

  gtk_box_pack_start(GTK_BOX(location_hbox), 
                     priv->preferences_data.location_label, 
                     TRUE, FALSE, 1);

  gtk_box_pack_end(GTK_BOX(location_hbox),
                   priv->preferences_data.location_button, FALSE, FALSE, 10);

  gtk_container_add(GTK_CONTAINER(location_frame), location_hbox);

  GtkWidget * display_frame = gtk_frame_new(_("Display"));

  GtkWidget * display_table = gtk_table_new(2, 2, FALSE);

  GtkWidget * alias_label = gtk_label_new(_("Name:"));

  priv->preferences_data.alias_entry = gtk_entry_new(); 

  GtkWidget * button_label = gtk_label_new(_("Units:"));

  GtkWidget * button_hbox = gtk_hbox_new(TRUE, 10);

  priv->preferences_data.c_button = gtk_radio_button_new_with_mnemonic(NULL, _("_Metric (\302\260C)"));

  priv->preferences_data.f_button = gtk_radio_button_new_with_mnemonic_from_widget(GTK_RADIO_BUTTON(priv->preferences_data.c_button), _("_English (\302\260F)"));

  gtk_box_pack_end(GTK_BOX(button_hbox), priv->preferences_data.c_button, FALSE, FALSE, 1);
  gtk_box_pack_end(GTK_BOX(button_hbox), priv->preferences_data.f_button, FALSE, FALSE, 1);

  gtk_table_attach(GTK_TABLE(display_table), 
                   alias_label,
                   0,1,0,1,
                   GTK_EXPAND | GTK_FILL | GTK_SHRINK,
                   GTK_EXPAND | GTK_FILL | GTK_SHRINK,
                   10,5);

  gtk_table_attach(GTK_TABLE(display_table), 
                   priv->preferences_data.alias_entry,
                   1,2,0,1,
                   GTK_EXPAND | GTK_FILL | GTK_SHRINK,
                   GTK_EXPAND | GTK_FILL | GTK_SHRINK,
                   10,5);

  gtk_table_attach(GTK_TABLE(display_table), 
                   button_label,
                   0,1,1,2,
                   GTK_EXPAND | GTK_FILL | GTK_SHRINK,
                   GTK_EXPAND | GTK_FILL | GTK_SHRINK,
                   10,5);

  gtk_table_attach(GTK_TABLE(display_table), 
                   button_hbox,
                   1,2,1,2,
                   GTK_EXPAND | GTK_FILL | GTK_SHRINK,
                   GTK_EXPAND | GTK_FILL | GTK_SHRINK,
                   10,5);

  gtk_container_add(GTK_CONTAINER(display_frame), display_table);

  GtkWidget * forecast_frame = gtk_frame_new(_("Forecast"));
  GtkWidget * forecast_table = gtk_table_new(2, 2, FALSE);
  GtkWidget * update_label   = gtk_label_new(_("Updates:"));
  GtkWidget * update_vbox    = gtk_vbox_new(TRUE, 10);

  priv->preferences_data.manual_button = gtk_radio_button_new_with_mnemonic(NULL, _("Ma_nual"));

  priv->preferences_data.auto_button = 
    gtk_radio_button_new_with_mnemonic_from_widget(GTK_RADIO_BUTTON(priv->preferences_data.manual_button), 
                                                   _("_Automatic, every"));

  g_signal_connect_swapped(G_OBJECT(priv->preferences_data.manual_button),
                           "toggled",
                           G_CALLBACK(gtk_weather_auto_update_toggled),
                           widget);

  g_signal_connect(G_OBJECT(priv->preferences_data.dialog),
                   "response",
                   G_CALLBACK(gtk_weather_preferences_dialog_response),
                   widget);

  /*  g_signal_connect_swapped(G_OBJECT(priv->preferences_data.auto_button),
                           "toggled",
                           G_CALLBACK(gtk_weather_auto_update_toggled),
                           widget);*/

  GtkWidget * auto_hbox = gtk_hbox_new(FALSE, 2);

  priv->preferences_data.auto_spin_button = gtk_spin_button_new_with_range(1, 60, 1);
  
  GtkWidget * auto_min_label = gtk_label_new(_("minutes"));

  gtk_box_pack_start(GTK_BOX(auto_hbox),
                     priv->preferences_data.auto_button,
                     FALSE, FALSE, 1);
  
  gtk_box_pack_start(GTK_BOX(auto_hbox),
                     priv->preferences_data.auto_spin_button,
                     FALSE, FALSE, 1);
  
  gtk_box_pack_start(GTK_BOX(auto_hbox),
                     auto_min_label,
                     FALSE, FALSE, 1);

  gtk_box_pack_start_defaults(GTK_BOX(update_vbox), priv->preferences_data.manual_button);
  gtk_box_pack_start_defaults(GTK_BOX(update_vbox), auto_hbox);

  GtkWidget * source_label = gtk_label_new(_("Source:"));
  GtkWidget * yahoo_button = gtk_radio_button_new_with_mnemonic(NULL, "_Yahoo! Weather"); 

  gtk_widget_set_sensitive(yahoo_button, FALSE);

  gtk_table_attach(GTK_TABLE(forecast_table), 
                   update_label,
                   0,1,0,1,
                   GTK_EXPAND | GTK_FILL | GTK_SHRINK,
                   GTK_EXPAND | GTK_FILL | GTK_SHRINK,
                   10,5);

  gtk_table_attach(GTK_TABLE(forecast_table), 
                   update_vbox,
                   1,2,0,1,
                   GTK_EXPAND | GTK_FILL | GTK_SHRINK,
                   GTK_EXPAND | GTK_FILL | GTK_SHRINK,
                   10,5);

  gtk_table_attach(GTK_TABLE(forecast_table), 
                   source_label,
                   0,1,1,2,
                   GTK_EXPAND | GTK_FILL | GTK_SHRINK,
                   GTK_EXPAND | GTK_FILL | GTK_SHRINK,
                   10,5);

  gtk_table_attach(GTK_TABLE(forecast_table), 
                   yahoo_button,
                   1,2,1,2,
                   GTK_EXPAND | GTK_FILL | GTK_SHRINK,
                   GTK_EXPAND | GTK_FILL | GTK_SHRINK,
                   10,5);

  gtk_container_add(GTK_CONTAINER(forecast_frame), forecast_table);

  /* VBox packing starts here */
  gtk_box_pack_start_defaults(GTK_BOX(GTK_DIALOG(priv->preferences_data.dialog)->vbox), 
                              location_frame);

  gtk_box_pack_start_defaults(GTK_BOX(GTK_DIALOG(priv->preferences_data.dialog)->vbox),
                              display_frame);

  gtk_box_pack_start_defaults(GTK_BOX(GTK_DIALOG(priv->preferences_data.dialog)->vbox),
                              forecast_frame);

  gtk_weather_update_preferences_dialog(weather);

  return priv->preferences_data.dialog;
}

/**
 * Creates and shows the preferences dialog.
 *
 * @param widget Pointer to the current instance of the weather object.
 */
void
gtk_weather_run_preferences_dialog(GtkWidget * widget)
{
  GtkWeather * weather = GTK_WEATHER(widget);

  /* @NOTE: watch for parent window when dealing with the plugin */
  LXW_LOG(LXW_DEBUG, "GtkWeather::run_preferences_dialog()");

  GtkWeatherPrivate * priv = GTK_WEATHER_GET_PRIVATE(weather);

  if (priv->preferences_data.shown) {
    return;
  }

  /* this dialog is the same one as priv->preferences_data.dialog */
  GtkWidget * dialog = gtk_weather_create_preferences_dialog(widget);

  gtk_widget_show_all(dialog);

  priv->preferences_data.shown = TRUE;
}

/**
 * Creates and shows the preferences dialog window
 *
 * @param weather Pointer to the instance of this widget.
 */
static void
gtk_weather_update_preferences_dialog(GtkWeather * weather)
{
  // @NOTE: watch for parent window when dealing with the plugin.
  // @TODO: possibly set the position of dialog window right in the middle of the screen.
  LXW_LOG(LXW_DEBUG, "GtkWeather::update_preferences_dialog()");

  GtkWeatherPrivate * priv = GTK_WEATHER_GET_PRIVATE(weather);

  if (!priv->preferences_data.dialog) {
    return;
  }

  if (priv->location) {
    LocationInfo * location = (LocationInfo *)priv->location;

    /* populate location_label */
    gchar * loc = g_strconcat((location->city_)?location->city_:"",
                              (location->city_)?", ":"",
                              (location->state_)?location->state_:"",
                              (location->state_)?", ":"",
                              (location->country_)?location->country_:"",
                              NULL);

    gtk_label_set_text(GTK_LABEL(priv->preferences_data.location_label), loc);

    gtk_button_set_label(GTK_BUTTON(priv->preferences_data.location_button), _("C_hange"));

    /* populate the alias entry with alias_ */
    gtk_widget_set_sensitive(priv->preferences_data.alias_entry, TRUE);
    gtk_entry_set_text(GTK_ENTRY(priv->preferences_data.alias_entry), location->alias_);

    gtk_widget_set_sensitive(priv->preferences_data.c_button, TRUE);
    gtk_widget_set_sensitive(priv->preferences_data.f_button, TRUE);

    gtk_widget_set_sensitive(priv->preferences_data.manual_button, TRUE);
    gtk_widget_set_sensitive(priv->preferences_data.auto_button, TRUE);

    /* populate/activate proper c/f button */  
    if (location->units_ == 'c') {
      gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(priv->preferences_data.c_button), TRUE);
      gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(priv->preferences_data.f_button), FALSE);
    } else {
      gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(priv->preferences_data.c_button), FALSE);
      gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(priv->preferences_data.f_button), TRUE);
    }

    /* populate/activate auto/manual button with auto-spin, if configured */
    if (location->enabled_) {
      gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(priv->preferences_data.auto_button), TRUE);
      gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(priv->preferences_data.manual_button), FALSE);
      gtk_widget_set_sensitive(GTK_WIDGET(priv->preferences_data.auto_spin_button), TRUE);
      gtk_spin_button_set_value(GTK_SPIN_BUTTON(priv->preferences_data.auto_spin_button), 
                                (gdouble)location->interval_);
    } else {
      gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(priv->preferences_data.auto_button), FALSE);
      gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(priv->preferences_data.manual_button), TRUE);
      gtk_widget_set_sensitive(GTK_WIDGET(priv->preferences_data.auto_spin_button), FALSE);
    }

    g_free(loc);
  } else {
    gtk_button_set_label(GTK_BUTTON(priv->preferences_data.location_button), _("_Set"));

    gtk_label_set_text(GTK_LABEL(priv->preferences_data.location_label),
                       _("None configured"));

    gtk_entry_set_text(GTK_ENTRY(priv->preferences_data.alias_entry), "");
      
    gtk_widget_set_sensitive(priv->preferences_data.alias_entry, FALSE);

    gtk_widget_set_sensitive(priv->preferences_data.c_button, FALSE);
    gtk_widget_set_sensitive(priv->preferences_data.f_button, FALSE);

    gtk_widget_set_sensitive(priv->preferences_data.auto_button, FALSE);
    gtk_widget_set_sensitive(priv->preferences_data.manual_button, FALSE);
    gtk_widget_set_sensitive(GTK_WIDGET(priv->preferences_data.auto_spin_button), FALSE);
  }

}

/**
 * Creates the current conditions dialog.
 *
 * @param weather Pointer to the current instance of the weather object.
 */
static void
gtk_weather_create_conditions_dialog(GtkWeather * weather)
{
  GtkWeatherPrivate * priv = GTK_WEATHER_GET_PRIVATE(weather);

  ConditionsDialogData * data = &(priv->conditions_data);

  LXW_LOG(LXW_DEBUG,
          "GtkWeather::create_conditions_dialog(%s)",
          ((data && data->shown)  ? "SHOWN":
           (data && !data->shown) ? "HIDDEN" : "NULL"));

  if (data->shown) {
    return;
  }

  LocationInfo * location = (LocationInfo *)priv->location;

  gchar * dialog_title = g_strdup_printf(_("Current Conditions for %s"), 
                                         (location)?location->alias_:"");

  data->dialog = gtk_dialog_new_with_buttons(dialog_title,
                                             NULL,
                                             GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                                             GTK_STOCK_REFRESH, GTK_RESPONSE_APPLY,
                                             GTK_STOCK_OK, GTK_RESPONSE_ACCEPT,
                                             NULL);

  GtkWidget * everything_hbox = gtk_hbox_new(FALSE, 5);

  /* This vbox gets filled-in when the table is populated */
  GtkWidget * icon_vbox = gtk_vbox_new(FALSE, 1);

  GtkWidget * forecast_table = gtk_table_new(9, 2, FALSE);

  GtkWidget * location_name_label = gtk_label_new(_("Location:"));
  data->location_text_label       = gtk_label_new(NULL);

  GtkWidget * label_alignment = gtk_alignment_new(0, 0.5, 0, 0);
  GtkWidget * text_alignment = gtk_alignment_new(0, 0.5, 0, 0);

  gtk_container_add(GTK_CONTAINER(label_alignment), location_name_label);
  gtk_container_add(GTK_CONTAINER(text_alignment), data->location_text_label);
      
  gtk_table_attach(GTK_TABLE(forecast_table), 
                   label_alignment,
                   0,1,0,1,
                   GTK_EXPAND | GTK_FILL | GTK_SHRINK,
                   GTK_EXPAND | GTK_FILL | GTK_SHRINK,
                   2,2);

  gtk_table_attach(GTK_TABLE(forecast_table), 
                   text_alignment,
                   1,2,0,1,
                   GTK_EXPAND | GTK_FILL | GTK_SHRINK,
                   GTK_EXPAND | GTK_FILL | GTK_SHRINK,
                   2,2);

  GtkWidget * updated_label = gtk_label_new(_("Last updated:"));
  data->update_text_label   = gtk_label_new(NULL);

  GtkWidget * updated_alignment      = gtk_alignment_new(0, 0.5, 0, 0);
  GtkWidget * updated_text_alignment = gtk_alignment_new(0, 0.5, 0, 0);

  gtk_container_add(GTK_CONTAINER(updated_alignment), updated_label);
  gtk_container_add(GTK_CONTAINER(updated_text_alignment),
                    data->update_text_label);

  gtk_table_attach(GTK_TABLE(forecast_table),
                   updated_alignment,
                   0,1,1,2,
                   GTK_EXPAND | GTK_FILL | GTK_SHRINK,
                   GTK_EXPAND | GTK_FILL | GTK_SHRINK,
                   2,2);

  gtk_table_attach(GTK_TABLE(forecast_table), 
                   updated_text_alignment,
                   1,2,1,2,
                   GTK_EXPAND | GTK_FILL | GTK_SHRINK,
                   GTK_EXPAND | GTK_FILL | GTK_SHRINK,
                   2,2);

  GtkWidget * feels_label     = gtk_label_new(_("Feels like:"));
  data->windchill_text_label  = gtk_label_new(NULL);

  GtkWidget * feels_alignment = gtk_alignment_new(0, 0.5, 0, 0);
  gtk_container_add(GTK_CONTAINER(feels_alignment), feels_label);

  GtkWidget * feels_text_alignment = gtk_alignment_new(0, 0.5, 0, 0);
  gtk_container_add(GTK_CONTAINER(feels_text_alignment),
                    data->windchill_text_label);

  gtk_table_attach(GTK_TABLE(forecast_table), 
                   feels_alignment,
                   0,1,2,3,
                   GTK_EXPAND | GTK_FILL | GTK_SHRINK,
                   GTK_EXPAND | GTK_FILL | GTK_SHRINK,
                   2,2);

  gtk_table_attach(GTK_TABLE(forecast_table), 
                   feels_text_alignment,
                   1,2,2,3,
                   GTK_EXPAND | GTK_FILL | GTK_SHRINK,
                   GTK_EXPAND | GTK_FILL | GTK_SHRINK,
                   2,2);

  GtkWidget * humidity_label = gtk_label_new(_("Humidity:"));
  data->humidity_text_label  = gtk_label_new(NULL);

  GtkWidget * humidity_alignment = gtk_alignment_new(0, 0.5, 0, 0);
  gtk_container_add(GTK_CONTAINER(humidity_alignment), humidity_label);

  GtkWidget * humidity_text_alignment = gtk_alignment_new(0, 0.5, 0, 0);
  gtk_container_add(GTK_CONTAINER(humidity_text_alignment),
                    data->humidity_text_label);

  gtk_table_attach(GTK_TABLE(forecast_table), 
                   humidity_alignment,
                   0,1,3,4,
                   GTK_EXPAND | GTK_FILL | GTK_SHRINK,
                   GTK_EXPAND | GTK_FILL | GTK_SHRINK,
                   2,2);

  gtk_table_attach(GTK_TABLE(forecast_table), 
                   humidity_text_alignment,
                   1,2,3,4,
                   GTK_EXPAND | GTK_FILL | GTK_SHRINK,
                   GTK_EXPAND | GTK_FILL | GTK_SHRINK,
                   2,2);

  GtkWidget * pressure_label = gtk_label_new(_("Pressure:"));
  data->pressure_text_label  = gtk_label_new(NULL);

  GtkWidget * pressure_alignment = gtk_alignment_new(0, 0.5, 0, 0);
  gtk_container_add(GTK_CONTAINER(pressure_alignment), pressure_label);

  GtkWidget * pressure_text_alignment = gtk_alignment_new(0, 0.5, 0, 0);
  gtk_container_add(GTK_CONTAINER(pressure_text_alignment),
                    data->pressure_text_label);

  gtk_table_attach(GTK_TABLE(forecast_table), 
                   pressure_alignment,
                   0,1,4,5,
                   GTK_EXPAND | GTK_FILL | GTK_SHRINK,
                   GTK_EXPAND | GTK_FILL | GTK_SHRINK,
                   2,2);

  gtk_table_attach(GTK_TABLE(forecast_table), 
                   pressure_text_alignment,
                   1,2,4,5,
                   GTK_EXPAND | GTK_FILL | GTK_SHRINK,
                   GTK_EXPAND | GTK_FILL | GTK_SHRINK,
                   2,2);

  GtkWidget * visibility_label = gtk_label_new(_("Visibility:"));
  data->visibility_text_label  = gtk_label_new(NULL);

  GtkWidget * visibility_alignment = gtk_alignment_new(0, 0.5, 0, 0);
  gtk_container_add(GTK_CONTAINER(visibility_alignment), visibility_label);

  GtkWidget * visibility_text_alignment = gtk_alignment_new(0, 0.5, 0, 0);
  gtk_container_add(GTK_CONTAINER(visibility_text_alignment),
                    data->visibility_text_label);

  gtk_table_attach(GTK_TABLE(forecast_table), 
                   visibility_alignment,
                   0,1,5,6,
                   GTK_EXPAND | GTK_FILL | GTK_SHRINK,
                   GTK_EXPAND | GTK_FILL | GTK_SHRINK,
                   2,2);

  gtk_table_attach(GTK_TABLE(forecast_table), 
                   visibility_text_alignment,
                   1,2,5,6,
                   GTK_EXPAND | GTK_FILL | GTK_SHRINK,
                   GTK_EXPAND | GTK_FILL | GTK_SHRINK,
                   2,2);

  GtkWidget * wind_label = gtk_label_new(_("Wind:"));
  data->wind_text_label  = gtk_label_new(NULL);

  GtkWidget * wind_alignment = gtk_alignment_new(0, 0.5, 0, 0);
  gtk_container_add(GTK_CONTAINER(wind_alignment), wind_label);

  GtkWidget * wind_text_alignment = gtk_alignment_new(0, 0.5, 0, 0);
  gtk_container_add(GTK_CONTAINER(wind_text_alignment),
                    data->wind_text_label);

  gtk_table_attach(GTK_TABLE(forecast_table), 
                   wind_alignment,
                   0,1,6,7,
                   GTK_EXPAND | GTK_FILL | GTK_SHRINK,
                   GTK_EXPAND | GTK_FILL | GTK_SHRINK,
                   2,2);

  gtk_table_attach(GTK_TABLE(forecast_table), 
                   wind_text_alignment,
                   1,2,6,7,
                   GTK_EXPAND | GTK_FILL | GTK_SHRINK,
                   GTK_EXPAND | GTK_FILL | GTK_SHRINK,
                   2,2);

  GtkWidget * sunrise_label = gtk_label_new(_("Sunrise:"));
  data->sunrise_text_label  = gtk_label_new(NULL);

  GtkWidget * sunrise_alignment = gtk_alignment_new(0, 0.5, 0, 0);
  gtk_container_add(GTK_CONTAINER(sunrise_alignment), sunrise_label);

  GtkWidget * sunrise_text_alignment = gtk_alignment_new(0, 0.5, 0, 0);
  gtk_container_add(GTK_CONTAINER(sunrise_text_alignment),
                    data->sunrise_text_label);

  gtk_table_attach(GTK_TABLE(forecast_table), 
                   sunrise_alignment,
                   0,1,7,8,
                   GTK_EXPAND | GTK_FILL | GTK_SHRINK,
                   GTK_EXPAND | GTK_FILL | GTK_SHRINK,
                   2,2);

  gtk_table_attach(GTK_TABLE(forecast_table), 
                   sunrise_text_alignment,
                   1,2,7,8,
                   GTK_EXPAND | GTK_FILL | GTK_SHRINK,
                   GTK_EXPAND | GTK_FILL | GTK_SHRINK,
                   2,2);

  GtkWidget * sunset_label = gtk_label_new(_("Sunset:"));
  data->sunset_text_label  = gtk_label_new(NULL);

  GtkWidget * sunset_alignment = gtk_alignment_new(0, 0.5, 0, 0);
  gtk_container_add(GTK_CONTAINER(sunset_alignment), sunset_label);

  GtkWidget * sunset_text_alignment = gtk_alignment_new(0, 0.5, 0, 0);
  gtk_container_add(GTK_CONTAINER(sunset_text_alignment),
                    data->sunset_text_label);

  gtk_table_attach(GTK_TABLE(forecast_table), 
                   sunset_alignment,
                   0,1,8,9,
                   GTK_EXPAND | GTK_FILL | GTK_SHRINK,
                   GTK_EXPAND | GTK_FILL | GTK_SHRINK,
                   2,2);

  gtk_table_attach(GTK_TABLE(forecast_table), 
                   sunset_text_alignment,
                   1,2,8,9,
                   GTK_EXPAND | GTK_FILL | GTK_SHRINK,
                   GTK_EXPAND | GTK_FILL | GTK_SHRINK,
                   2,2);

  /* Image and conditions label. Image is filled after dialog is shown 
   * to nicely scale the image pixbuf.
   */
  data->conditions_image = gtk_image_new_from_stock(GTK_STOCK_MISSING_IMAGE,
                                                    GTK_ICON_SIZE_MENU);

  data->conditions_text_label = gtk_label_new(NULL);

  /* Pack boxes */
  gtk_box_pack_start(GTK_BOX(icon_vbox),
                     data->conditions_image,
                     FALSE,
                     FALSE,
                     1);

  gtk_box_pack_start(GTK_BOX(icon_vbox),
                     data->conditions_text_label,
                     FALSE,
                     FALSE,
                     1);

  gtk_box_pack_start(GTK_BOX(everything_hbox), icon_vbox, TRUE, TRUE, 35);
  gtk_box_pack_start(GTK_BOX(everything_hbox), forecast_table, FALSE, FALSE, 5);
      
  gtk_box_pack_start(GTK_BOX(GTK_DIALOG(data->dialog)->vbox),
                     everything_hbox,
                     FALSE,
                     FALSE,
                     5);

  /* Set dialog window icon */
  gtk_weather_set_window_icon(GTK_WINDOW(data->dialog), "gtk-about");

  gtk_window_set_resizable(GTK_WINDOW(data->dialog), FALSE);

  gtk_dialog_set_default_response(GTK_DIALOG(data->dialog), GTK_RESPONSE_ACCEPT);

  data->shown = TRUE;

  gtk_widget_show_all(data->dialog);

}

/**
 * Updates the current conditions dialog.
 *
 * @param weather Pointer to the current instance of the weather widget.
 */
static void
gtk_weather_update_conditions_dialog(GtkWeather * weather)
{
  GtkWeatherPrivate * priv = GTK_WEATHER_GET_PRIVATE(weather);

  ConditionsDialogData * data = &(priv->conditions_data);

  LXW_LOG(LXW_DEBUG,
          "GtkWeather::update_conditions_dialog(%s)",
          ((data && data->shown)  ? "SHOWN":
           (data && !data->shown) ? "HIDDEN" : "NULL"));

  LocationInfo * location = (LocationInfo *)priv->location;
  ForecastInfo * forecast = (ForecastInfo *)priv->forecast;

  if (location && forecast && data->dialog) {
    if (pthread_rwlock_rdlock(&(priv->rwlock)) == 0) {
      gchar * location_label_text = g_strconcat((location->city_)?location->city_:"",
                                                (location->city_)?", ":"",
                                                (location->state_)?location->state_:"",
                                                (location->state_)?", ":"",
                                                (location->country_)?location->country_:"",
                                                NULL);

      gtk_label_set_text(GTK_LABEL(data->location_text_label),
                         location_label_text);

      gtk_label_set_text(GTK_LABEL(data->update_text_label),
                         forecast->time_);

      gchar * windchill = g_strdup_printf("%d \302\260%s", 
                                          forecast->windChill_,
                                          forecast->units_.temperature_);

      gtk_label_set_text(GTK_LABEL(data->windchill_text_label),
                         windchill);

      gchar * humidity = g_strdup_printf("%d%%", forecast->humidity_);

      gtk_label_set_text(GTK_LABEL(data->humidity_text_label), humidity);

      gchar * pressure = g_strdup_printf("%4.2f %s", 
                                         forecast->pressure_,
                                         forecast->units_.pressure_);

      gtk_label_set_text(GTK_LABEL(data->pressure_text_label), pressure);

      gchar * visibility = g_strdup_printf("%4.2f %s", 
                                           forecast->visibility_,
                                           forecast->units_.distance_);

      gtk_label_set_text(GTK_LABEL(data->visibility_text_label), visibility);

      gchar * wind = g_strdup_printf("%s %d %s", 
                                     forecast->windDirection_,
                                     forecast->windSpeed_,
                                     forecast->units_.speed_);

      gtk_label_set_text(GTK_LABEL(data->wind_text_label), wind);
      gtk_label_set_text(GTK_LABEL(data->sunrise_text_label),
                         forecast->sunrise_);

      gtk_label_set_text(GTK_LABEL(data->sunset_text_label),
                         forecast->sunset_);

      gchar * conditions_label_text = g_strdup_printf("<b>%d \302\260%s %s</b>", 
                                                      forecast->temperature_,
                                                      forecast->units_.temperature_,
                                                      _(forecast->conditions_));

      gtk_label_set_markup(GTK_LABEL(data->conditions_text_label),
                           conditions_label_text);

      /* Free everything */
      g_free(conditions_label_text);
      g_free(wind);
      g_free(visibility);
      g_free(pressure);
      g_free(windchill);
      g_free(humidity);
      g_free(location_label_text);

      data->shown = TRUE;

      gtk_widget_show_all(data->dialog);

      /* Get dimensions to create proper icon... */
      GtkRequisition req;

      gtk_widget_size_request(data->dialog, &req);

      /* Need the minimum */
      gint dim = (req.width < req.height) ? req.width/2 : req.height/2;

      GdkPixbuf * icon_buf = gdk_pixbuf_scale_simple(forecast->image_,
                                                     dim, dim,
                                                     GDK_INTERP_BILINEAR);

      gtk_image_set_from_pixbuf(GTK_IMAGE(data->conditions_image), icon_buf);

      g_object_unref(icon_buf);

      pthread_rwlock_unlock(&(priv->rwlock));
    }

  }
}

/**
 * Creates and shows the current conditions dialog.
 *
 * @param widget Pointer to the current instance of the weather object.
 */
void
gtk_weather_run_conditions_dialog(GtkWidget * widget)
{
  GtkWeather * weather = GTK_WEATHER(widget);

  GtkWeatherPrivate * priv = GTK_WEATHER_GET_PRIVATE(weather);

  ConditionsDialogData * data = &(priv->conditions_data);

  LXW_LOG(LXW_DEBUG,
          "GtkWeather::run_conditions_dialog(%s)",
          ((data && data->shown)  ? "SHOWN":
           (data && !data->shown) ? "HIDDEN" : "NULL"));

  LocationInfo * location = (LocationInfo *)priv->location;
  ForecastInfo * forecast = (ForecastInfo *)priv->forecast;

  if (location && forecast) {
    if (data->shown) {
      return;
    }

    gtk_weather_create_conditions_dialog(weather);

    gtk_weather_update_conditions_dialog(weather);

    gint response = GTK_RESPONSE_NONE;

    do {
      response = gtk_dialog_run(GTK_DIALOG(data->dialog));

      if (response == GTK_RESPONSE_APPLY) {
        gtk_weather_get_forecast(widget);
      }

    } while (response != GTK_RESPONSE_ACCEPT);

    if (GTK_IS_WIDGET(data->dialog)) {
      gtk_widget_destroy(data->dialog);
    }

    data->dialog = NULL;

    data->shown = FALSE;
  } else if (!forecast && location) {
    gchar * error_msg = NULL;

    if (pthread_rwlock_rdlock(&(priv->rwlock)) == 0) {
      error_msg = g_strdup_printf(_("Forecast for %s unavailable."),
                                  location->alias_);

      pthread_rwlock_unlock(&(priv->rwlock));
    }

    gtk_weather_run_error_dialog(NULL, (error_msg) ? error_msg : "FAIL!");

    g_free(error_msg);
  } else {
    gtk_weather_run_error_dialog(NULL, _("Location not set."));
  }
  
}

/**
 * Creates and shows the location retrieval progress bar.
 *
 * @param weather Pointer to the instance of this widget.
 */
static void
gtk_weather_show_location_progress_bar(GtkWeather * weather)
{
  LXW_LOG(LXW_DEBUG, "GtkWeather::show_location_progress_bar()");

  GtkWeatherPrivate * priv = GTK_WEATHER_GET_PRIVATE(weather);

  gchar * progress_str = g_strdup_printf(_("Searching for '%s'..."),
                                         priv->location_data.location);

  GtkWidget * dialog = gtk_dialog_new_with_buttons(progress_str,
                                                   GTK_WINDOW(priv->preferences_data.dialog),
                                                   GTK_DIALOG_DESTROY_WITH_PARENT,
                                                   GTK_STOCK_CANCEL,
                                                   GTK_RESPONSE_CANCEL,
                                                   NULL);

  //  gtk_window_set_decorated(GTK_WINDOW(dialog), FALSE);

  GtkWidget * alignment    = gtk_alignment_new(0.5, 0.5, 0.5, 0.5);
  GtkWidget * progress_bar = gtk_progress_bar_new();

  priv->location_data.progress_bar = GTK_PROGRESS_BAR(progress_bar);

  priv->location_data.progress_dialog = dialog;

  gtk_progress_bar_set_text(GTK_PROGRESS_BAR(progress_bar), progress_str);

  gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(progress_bar), 0.5);

  gtk_container_add(GTK_CONTAINER(alignment), progress_bar);

  gtk_box_pack_start_defaults(GTK_BOX(GTK_DIALOG(dialog)->vbox), alignment);

  int timer = g_timeout_add(500,
                            gtk_weather_update_location_progress_bar,
                            &priv->location_data);

  gtk_widget_show_all(dialog);

  gint response = gtk_dialog_run(GTK_DIALOG(dialog));

  switch(response) {
  case GTK_RESPONSE_ACCEPT:
    break;

  case GTK_RESPONSE_CANCEL:
    if (pthread_kill(*(priv->location_data.tid), 0) != ESRCH) {
      int ret = pthread_cancel(*(priv->location_data.tid));

      if (ret != 0) {
        LOG_ERRNO(ret, "pthread_cancel");
      }
    }

    if (timer) {
      g_source_remove(timer);
    }

    break;

  default:
    break;
  }

  if (dialog && GTK_IS_WIDGET(dialog)) {
    gtk_widget_destroy(dialog);
  }

  g_free(progress_str);
}

/**
 * Updates the location progress bar at regular intervals.
 *
 * @param data Pointer to the location thread data
 *
 * @return TRUE if this function should be called again, FALSE otherwise
 */
static gboolean
gtk_weather_update_location_progress_bar(gpointer data)
{
  LocationThreadData * location_data = (LocationThreadData *)data;

  LXW_LOG(LXW_DEBUG, "GtkWeather::update_location_progress_bar(): %d percent complete.", 
          (location_data)?(int)(gtk_progress_bar_get_fraction(location_data->progress_bar) * 100):-1);

  if (!location_data) {
    return FALSE;
  }

  gboolean ret = TRUE;

  /* Get the percentage */

  /* If it's less than 100, check the thread. 
   * If the thread is still running, increment percentage.
   * Otherwise, cancel thread - something's wrong.
   */
  gint percentage = gtk_progress_bar_get_fraction(location_data->progress_bar) * 100;

  if ( (percentage >= 100) ||
       (pthread_kill(*(location_data->tid), 0) == ESRCH) ) {
    gtk_widget_destroy(location_data->progress_dialog);

    ret = FALSE;
  } else {
    percentage += 10;

    gtk_progress_bar_set_fraction(location_data->progress_bar, (gdouble)percentage/100);

    ret = TRUE;
  }
  
  return ret;
}


/**
 * Updates the location progress bar at regular intervals.
 *
 * @param data Pointer to the location thread data
 *
 * @return TRUE if this function should be called again, FALSE otherwise
 */
static gboolean
gtk_weather_update_ui(gpointer data)
{
  LXW_LOG(LXW_DEBUG, "GtkWeather::update_ui()");

  GtkWeather        * weather = GTK_WEATHER(data);
  GtkWeatherPrivate * priv    = GTK_WEATHER_GET_PRIVATE(weather);
  
  /* render happens inside, need to emit the changed signal */
  gtk_weather_set_forecast(weather, priv->forecast);

  return FALSE;
}

/**
 * Creates and shows the location list selection dialog.
 *
 * @param weather Pointer to the instance of this widget.
 * @param list   Pointer to the list of retrieved locations.
 */
static void
gtk_weather_show_location_list(GtkWeather * weather, GList * list)
{
  LXW_LOG(LXW_DEBUG, "GtkWeather::show_location_list(%d)", g_list_length(list));

  GtkWeatherPrivate * priv = GTK_WEATHER_GET_PRIVATE(weather);

  gchar * dialog_str = g_strdup_printf(_("Location matches for '%s'"), 
                                       priv->location_data.location);

  GtkWidget * dialog = gtk_dialog_new_with_buttons(dialog_str,
                                                   GTK_WINDOW(priv->preferences_data.dialog),
                                                   GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                                                   GTK_STOCK_OK, GTK_RESPONSE_ACCEPT,
                                                   GTK_STOCK_CANCEL, GTK_RESPONSE_REJECT,
                                                   NULL);

  gtk_widget_set_size_request(dialog, 300, 250);

  /* Set dialog window icon */
  gtk_weather_set_window_icon(GTK_WINDOW(dialog), "gtk-properties");

  /* TreeView */
  GtkWidget * treeview = gtk_tree_view_new();

  /* city */
  GtkCellRenderer   * cell_renderer   = gtk_cell_renderer_text_new();
  GtkTreeViewColumn * treeview_column = gtk_tree_view_column_new_with_attributes(_("City"),
                                                                                 cell_renderer,
                                                                                 "text",
                                                                                 CITY_COLUMN,
                                                                                 NULL);

  gtk_tree_view_append_column(GTK_TREE_VIEW(treeview), treeview_column);

  /* state */
  cell_renderer = gtk_cell_renderer_text_new();
  treeview_column = gtk_tree_view_column_new_with_attributes(_("State"),
                                                             cell_renderer,
                                                             "text",
                                                             STATE_COLUMN,
                                                             NULL);

  gtk_tree_view_append_column(GTK_TREE_VIEW(treeview), treeview_column);

  /* country */
  cell_renderer = gtk_cell_renderer_text_new();
  treeview_column = gtk_tree_view_column_new_with_attributes(_("Country"),
                                                             cell_renderer,
                                                             "text",
                                                             COUNTRY_COLUMN,
                                                             NULL);

  gtk_tree_view_append_column(GTK_TREE_VIEW(treeview), treeview_column);

  /* TreeView items */
  GtkListStore * list_store = gtk_list_store_new(MAX_COLUMNS, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING);

  GtkTreeIter iterator;

  guint length = g_list_length(list);

  guint index = 0;

  for (; index < length; ++index) {
    gtk_list_store_append(list_store, &iterator);

    LocationInfo * location = (LocationInfo *)g_list_nth_data(list, index);

    gtk_list_store_set(list_store,     &iterator, 
                       CITY_COLUMN,    location->city_,
                       STATE_COLUMN,   location->state_,
                       COUNTRY_COLUMN, location->country_, -1);
  }

  /* Set the model behind the tree view, and forget about it */
  gtk_tree_view_set_model(GTK_TREE_VIEW(treeview), GTK_TREE_MODEL(list_store));
  g_object_unref(list_store);

  GtkTreeSelection * selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(treeview));

  gtk_tree_selection_set_mode(selection, GTK_SELECTION_BROWSE);

  /* Internals of the dialog window */
  GtkWidget * scrolled_window = gtk_scrolled_window_new(NULL, NULL);
  gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled_window),
                                 GTK_POLICY_AUTOMATIC,
                                 GTK_POLICY_AUTOMATIC);

  gtk_container_add(GTK_CONTAINER(scrolled_window), treeview);

  gtk_box_pack_start_defaults(GTK_BOX(GTK_DIALOG(dialog)->vbox), scrolled_window);

  gtk_widget_show_all(dialog);

  gint response = gtk_dialog_run(GTK_DIALOG(dialog));

  GtkTreeModel * model;

  /* handle selection */
  switch(response) {
  case GTK_RESPONSE_ACCEPT:
    model = gtk_tree_view_get_model(GTK_TREE_VIEW(treeview));

    if (gtk_tree_selection_get_selected(selection, &model, &iterator)) {
      gchar * path = gtk_tree_model_get_string_from_iter(model, &iterator);

      gint index = (gint)g_ascii_strtoull(path, NULL, 10);

      LocationInfo * location = g_list_nth_data(list, index);

      gtk_weather_set_location(weather, (gpointer)location, TRUE);
      /* list of locations is released by the caller */

      /* preferences dialog is also repainted by caller */
      g_free(path);
    }
      
    break;

  default:
    break;
  }
  
  if (GTK_IS_WIDGET(dialog)) {
    gtk_widget_destroy(dialog);
  }
  
  g_free(dialog_str);
}

/**
 * Generates the text for the tooltip based on current location and forecast.
 *
 * @param widget Pointer to the current instance of the weather widget.
 *
 * @return Text to be shown as part of the tooltip. The caller must release
 *         the memory using g_free.
 */
gchar *
gtk_weather_get_tooltip_text(GtkWidget * widget)
{
  GtkWeatherPrivate * priv = GTK_WEATHER_GET_PRIVATE(GTK_WEATHER(widget));

  LXW_LOG(LXW_DEBUG, "GtkWeather::get_tooltip_text()");

  gchar * tooltip_text = NULL;

  if (priv->location && priv->forecast) {
    LocationInfo * location = priv->location;
    ForecastInfo * forecast = priv->forecast;

    gchar * temperature = g_strdup_printf("%d \302\260%s\n", 
                                          forecast->temperature_,
                                          forecast->units_.temperature_);

    gchar * days[FORECAST_MAX_DAYS] = { NULL };

    int d = 0;
    for (; d < FORECAST_MAX_DAYS; d++) {
      /* @TODO: can use the code_ member variable, too */
      const gchar * actual_day =
        (forecast->days_[d].day_) ? forecast->days_[d].day_ : "N/A";
      
      const gchar * conditions =
        (forecast->days_[d].conditions_) ? forecast->days_[d].conditions_ : "N/A";

      days[d] = g_strdup_printf("%s: %s %d\302\260 / %d\302\260",
                                _(actual_day),
                                _(conditions),
                                forecast->days_[d].low_,
                                forecast->days_[d].high_);
    }
    
    /* make it nice and pretty */
    tooltip_text = g_strconcat(_("Currently in "),location->alias_, ": ",
                               _(forecast->conditions_), " ", temperature, "",
                               days[FORECAST_DAY_1], "\n",
                               days[FORECAST_DAY_2], "\n",
                               days[FORECAST_DAY_3], "\n",
                               days[FORECAST_DAY_4], "\n",
                               days[FORECAST_DAY_5],
                               NULL);
                                 
    g_free(temperature);
    g_free(days[FORECAST_DAY_1]);
    g_free(days[FORECAST_DAY_2]);
    g_free(days[FORECAST_DAY_3]);
    g_free(days[FORECAST_DAY_4]);
    g_free(days[FORECAST_DAY_5]);
  } else if (priv->location) {
    tooltip_text = g_strdup_printf(_("Forecast for %s unavailable."),
                                   ((LocationInfo *)priv->location)->alias_);
  } else {
    tooltip_text = g_strdup_printf(_("Location not set."));
  }

  LXW_LOG(LXW_DEBUG, "\tReturning: %s", tooltip_text);

  return tooltip_text;
}

/**
 * Sets the icon on the specified window, if the icon id is found.
 *
 * @param window  Pointer to the GtkWindow to decorate.
 * @param icon_id The id of the icon to find.
 */
static void
gtk_weather_set_window_icon(GtkWindow * window, gchar * icon_id)
{
  LXW_LOG(LXW_DEBUG, "GtkWeather::set_window_icon(%s)", icon_id);

	if(gtk_icon_theme_has_icon(gtk_icon_theme_get_default(), icon_id)) {
    GdkPixbuf* window_icon = gtk_icon_theme_load_icon(gtk_icon_theme_get_default(), 
                                                      icon_id,
                                                      24, 
                                                      0,
                                                      NULL);

    gtk_window_set_icon(window, window_icon);
  }

}

/**
 * The location retrieval thread function.
 *
 * @param arg Pointer to argument data.
 *
 * @return Data based on thread completion.
 */
static void *
gtk_weather_get_location_threadfunc(void * arg)
{
  gchar * location = (gchar *)arg;
  
  GList * list = yahooutil_location_find(location);

  GList * iter = g_list_first(list);
  while (iter) {
    location_property_set(iter->data,
                          "alias",
                          location,
                          (location) ? strlen(location) : 0);

    iter = g_list_next(iter);
  }
  
  return list;  
}

// ----------- forecast retrieval functions begin here --------

/**
 * Starts the forecast thread if not already running.  This includes
 * the thread responsible for actually getting the latest forecast,
 * as well as the timer thread, if 'enabled' is 1
 *
 * @param weather Pointer to the weather instance.
 *
 * @return 0 on success, non-zero on failure.
 */
static gint
gtk_weather_forecast_thread_start(GtkWeather * weather)
{
  GtkWeatherPrivate * priv = GTK_WEATHER_GET_PRIVATE(weather);

  LocationInfo       * location = (LocationInfo *) priv->location; 
  ForecastThreadData * ftdata   = &(priv->forecast_data);

  int rc = 0;

  if (location) {
    /* if it's not already running */
    if (!ftdata->active) {
      pthread_attr_t tattr;
      if (pthread_attr_init(&tattr)) {
        LOG_ERRNO(errno, "pthread_attr_init()");
      }

      ftdata->active = 1;
      
      rc = pthread_create(&(ftdata->thread),
                          &tattr,
                          &gtk_weather_get_forecast_threadfunc,
                          weather);

      if (rc) {
        LOG_ERRNO(errno, "pthread_create()");
      }
    }
  }
  
  return rc;
}

/**
 * Stops the forecast retrieval thread and the corresponding timer thread.
 *
 * @param weather Pointer to the weather instance.
 *
 * @return 0 on success, non-zero on failure.
 */
static gint
gtk_weather_forecast_thread_stop(GtkWeather * weather)
{
  GtkWeatherPrivate * priv = GTK_WEATHER_GET_PRIVATE(weather);

  // @TODO: take the location thread stuff out of here
  LocationThreadData * ltdata = &(priv->location_data);
  ForecastThreadData * ftdata = &(priv->forecast_data);

  int rc = 0;
  
  ltdata->active = 0;
  ftdata->active = 0;

  // thread timer, first
  if (ftdata->timerid > 0) {
    g_source_remove(ftdata->timerid);
  }

  if (priv->location &&
      ((LocationInfo *) priv->location)->enabled_) {
    if (pthread_mutex_lock(&(priv->mutex)) == 0) {
      pthread_cond_signal(&(priv->cond));
      pthread_mutex_unlock(&(priv->mutex));
    }

    rc = pthread_join(ftdata->thread, NULL);
    if (rc) {
      if (rc == ETIMEDOUT) {
        LXW_LOG(LXW_ERROR, "Timed join timed out.");
      }
          
      LXW_LOG(LXW_ERROR, "Could not join thread: %d", errno);
    }
  }
  
  return rc;
}

/**
 * Retrieves the forecast. Starts the forecast timer, if enabled in
 * the particular location.
 *
 * @param widget Pointer to the current instance of the weather widget
 */
static void
gtk_weather_get_forecast(GtkWidget * widget)
{
  LXW_LOG(LXW_DEBUG, "GtkWeather::get_forecast()");

  GtkWeatherPrivate * priv = GTK_WEATHER_GET_PRIVATE(GTK_WEATHER(widget));

  LocationInfo * location = (LocationInfo *)priv->location;

  if (location && location->enabled_) {      
    /* resetting the timer as the interval may have changed */
    guint interval_in_seconds = 60 * ((location->interval_) ?
                                      location->interval_ : 1);

    if (priv->forecast_data.timerid > 0) {
      g_source_remove(priv->forecast_data.timerid);
    }

    priv->forecast_data.timerid =
      g_timeout_add_seconds(interval_in_seconds,
                            gtk_weather_get_forecast_timerfunc,
                            (gpointer)widget);
  } else if (priv->forecast_data.timerid > 0) {
    g_source_remove(priv->forecast_data.timerid);

    priv->forecast_data.timerid = 0;
  }
  
  /* One, single call just to get the latest forecast.
   * Note that the only thing that the timer function does is
   * signal the condition variable so that the forecast thread
   * can retrieve the latest forecast.
   */
  if (location) {
    gtk_weather_get_forecast_timerfunc((gpointer)widget);
  }
}

/**
 * The forecast retrieval timer function.
 *
 * @param data Pointer to user-data (GtkWeather instance).
 *
 * @return TRUE if the timer should be restarted, FALSE otherwise.
 */
static gboolean 
gtk_weather_get_forecast_timerfunc(gpointer data)
{
  GtkWeatherPrivate * priv = GTK_WEATHER_GET_PRIVATE(GTK_WEATHER(data));

  LocationInfo * location = (LocationInfo *) priv->location;
  
  LXW_LOG(LXW_DEBUG, "GtkWeather::get_forecast_timerfunc(%d %d)", 
          (location) ? location->enabled_: 0,
          (location) ? location->interval_ * 60 : 0);
  
  if (!location) {
    LXW_LOG(LXW_ERROR,
            "GtkWeather::get_forecast_timerfunc, location is NULL");

    return FALSE;
  }

  LXW_LOG(LXW_DEBUG, "\t timerfunc about to signal cond.");
  if (pthread_mutex_lock(&(priv->mutex)) == 0) {
    LXW_LOG(LXW_DEBUG, "\t timerfunc signalled cond.");
    pthread_cond_signal(&(priv->cond));
    pthread_mutex_unlock(&(priv->mutex));
  }

  return location->enabled_;
}

/**
 * The forecast retrieval thread function.
 *
 * @param arg Pointer to argument data.
 *
 * @return Data based on thread completion.
 */
static void *
gtk_weather_get_forecast_threadfunc(void * arg)
{
  GtkWeather * weather = (GtkWeather *) arg;

  if (!weather) {
    LXW_LOG(LXW_ERROR, "Invalid weather pointer in forecast thread!");
    return NULL;
  }
  
  GtkWeatherPrivate * priv = GTK_WEATHER_GET_PRIVATE(weather);

  LocationInfo       * location = (LocationInfo *) priv->location;
  ForecastThreadData * ftdata   = &(priv->forecast_data);

  LXW_LOG(LXW_DEBUG, "GtkWeather::threadfunc active is %d", ftdata->active);

  /* Need to force the first refresh on thread start if location is set */
  gboolean initial = (location) ? TRUE : FALSE;

  while (ftdata->active) {
    LXW_LOG(LXW_DEBUG, "\tabout to wait");
    if (!initial && pthread_mutex_lock(&(priv->mutex)) == 0) {
      pthread_cond_wait(&(priv->cond), &(priv->mutex));

      LXW_LOG(LXW_DEBUG, "\tPOPPED!");
      pthread_mutex_unlock(&(priv->mutex));
    }

    if (!ftdata->active) {
      break;
    }

    initial = FALSE;

    /* write lock */
    LXW_LOG(LXW_DEBUG, "\tabout to writelock");
    if (pthread_rwlock_wrlock(&(priv->rwlock)) == 0) {
      LXW_LOG(LXW_DEBUG, "\tgetting forecast for %s", location->woeid_);
      yahooutil_forecast_get(location->woeid_,
                             location->units_, 
                             &(priv->forecast));

      pthread_rwlock_unlock(&(priv->rwlock));

      /* render on main thread */
      g_idle_add(gtk_weather_update_ui, weather);
    }
    
  }

  return NULL;
}
