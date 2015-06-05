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

/* Provides implementation for ForecastInfo-specific functions */

#include "forecast.h" // includes glib.h
#include "logutil.h"

#include <stdio.h>
#include <string.h>

/**
 * Provides the mechanism to free any data associated with 
 * the Forecast structure
 *
 * @param forecast Entry to free.
 *
 */
static void
freeForecastForecast(Forecast * forecast)
{
  g_free(forecast->day_);
  g_free(forecast->conditions_);
}

/**
 * Provides the mechanism to free any data associated with 
 * the ForecastUnits structure
 *
 * @param units Entry to free.
 *
 */
static void
freeForecastUnits(ForecastUnits * units)
{
  g_free(units->distance_);
  g_free(units->pressure_);
  g_free(units->speed_);
  g_free(units->temperature_);
}

/**
 * Provides the mechanism to free any data associated with 
 * the ForecastInfo structure
 *
 * @param forecast Entry to free.
 *
 */
void
freeForecast(gpointer forecast)
{
  if (!forecast)
    {
      return;
    }

  ForecastInfo * info = (ForecastInfo *)forecast;

  freeForecastUnits(&info->units_);

  freeForecastForecast(&info->today_);
  freeForecastForecast(&info->tomorrow_);

  g_free(info->windDirection_);
  g_free(info->sunrise_);
  g_free(info->sunset_);
  g_free(info->time_);
  g_free(info->conditions_);
  g_free(info->imageURL_);
  
  if (info->image_)
    {
      g_object_unref(info->image_);
    }

  g_free(forecast);
}

/**
 * Prints the contents of the supplied entry to stdout
 *
 * @param forecast Entry contents of which to print.
 *
 */
void
printForecast(gpointer forecast G_GNUC_UNUSED)
{
#ifdef DEBUG
  if (!forecast)
    {
      LXW_LOG(LXW_ERROR, "forecast::printForecast(): Entry: NULL");
      
      return;
    }
  
  ForecastInfo * info = (ForecastInfo *)forecast;
  
  LXW_LOG(LXW_VERBOSE, "Forecast at %s:",     (const char *)info->time_);
  LXW_LOG(LXW_VERBOSE, "\tTemperature: %d%s",
          info->temperature_,
          (const char *)info->units_.temperature_);

  LXW_LOG(LXW_VERBOSE, "\tHumidity: %d%", info->humidity_);
  LXW_LOG(LXW_VERBOSE, "\tWind chill: %d%s, speed: %d%s, direction %s", 
          info->windChill_,
          (const char *)info->units_.temperature_,
          info->windSpeed_,
          (const char *)info->units_.speed_,
          info->windDirection_);
  LXW_LOG(LXW_VERBOSE, "\tPressure: %2.02f%s and %s", 
          info->pressure_,
          (const char *)info->units_.pressure_,
          ((info->pressureState_ == STEADY)?"steady":
           (info->pressureState_ == RISING)?"rising":
           (info->pressureState_ == FALLING)?"falling":"?"));
  LXW_LOG(LXW_VERBOSE, "\tConditions: %s", (const char *)info->conditions_);
  LXW_LOG(LXW_VERBOSE, "\tVisibility: %3.02f%s", 
          info->visibility_,
          (const char *)info->units_.distance_);
  LXW_LOG(LXW_VERBOSE, "\tSunrise: %s", (const char *)info->sunrise_);
  LXW_LOG(LXW_VERBOSE, "\tSunset: %s", (const char *)info->sunset_);
  LXW_LOG(LXW_VERBOSE, "\tImage URL: %s", info->imageURL_);

  LXW_LOG(LXW_VERBOSE, "\tTwo-day forecast:");
  LXW_LOG(LXW_VERBOSE, "\t\t%s: High: %d%s, Low: %d%s, Conditions: %s",
          (const char *)info->today_.day_,
          info->today_.high_,
          (const char *)info->units_.temperature_,
          info->today_.low_,
          (const char *)info->units_.temperature_,
          (const char *)info->today_.conditions_);
  LXW_LOG(LXW_VERBOSE, "\t\t%s: High: %d%s, Low: %d%s, Conditions: %s",
          (const char *)info->tomorrow_.day_,
          info->tomorrow_.high_,
          (const char *)info->units_.temperature_,
          info->tomorrow_.low_,
          (const char *)info->units_.temperature_,
          (const char *)info->tomorrow_.conditions_);
#endif
}
