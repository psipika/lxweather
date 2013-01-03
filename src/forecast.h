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

/* Defines the layout of the forecast structure */

#ifndef LXWEATHER_FORECAST_HEADER
#define LXWEATHER_FORECAST_HEADER

#include <glib.h>
#include <gtk/gtk.h>

#define WIND_DIRECTION(x) ( \
  ((x>=350 && x<=360) || (x>=0 && x<=11 ))?"N": \
  (x>11   && x<=33 )?"NNE": \
  (x>33   && x<=57 )?"NE":  \
  (x>57   && x<=79 )?"ENE": \
  (x>79   && x<=101)?"E":   \
  (x>101  && x<=123)?"ESE": \
  (x>123  && x<=147)?"SE":  \
  (x>147  && x<=169)?"SSE": \
  (x>169  && x<=192)?"S":   \
  (x>192  && x<=214)?"SSW": \
  (x>214  && x<=236)?"SW":  \
  (x>236  && x<=258)?"WSW": \
  (x>258  && x<=282)?"W":   \
  (x>282  && x<=304)?"WNW": \
  (x>304  && x<=326)?"NW":  \
  (x>326  && x<=349)?"NNW":"")

typedef enum
{
  STEADY, // 0
  RISING, // 1
  FALLING // 2
} PressureState;

typedef struct
{
  gchar * pcDay_;
  gint iHigh_;
  gint iLow_;
  gchar * pcConditions_;
} Forecast;

typedef struct
{
  gchar * pcDistance_;
  gchar * pcPressure_;
  gchar * pcSpeed_;
  gchar * pcTemperature_;
} ForecastUnits;

typedef struct 
{
  ForecastUnits units_;
  PressureState pressureState_;
  Forecast today_;
  Forecast tomorrow_;
  gint iWindChill_;
  gchar * pcWindDirection_;
  gint iWindSpeed_;
  gint iHumidity_;
  gdouble dPressure_;
  gdouble dVisibility_;
  gchar * pcSunrise_;
  gchar * pcSunset_;
  gchar * pcTime_;
  gint iTemperature_;
  gchar * pcConditions_;
  gchar * pcImageURL_;
  GdkPixbuf * pImage_;
} ForecastInfo;

/**
 * Provides the mechanism to free any data associated with 
 * the ForecastInfo structure
 *
 * @param pData Entry to free.
 *
 */
void
freeForecast(gpointer pData);

/**
 * Prints the contents of the supplied entry to stdout
 *
 * @param pEntry Entry contents of which to print.
 *
 */
void
printForecast(gpointer pEntry);

#endif
