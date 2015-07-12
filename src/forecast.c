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

#define SAFE_STRNDUP(p, v)                       \
  do {                                           \
    if (!v) break;                               \
    p = g_strndup((v), strlen(v));               \
  } while (0);

/**
 * Provides the mechanism to free all data associated with
 * the ForecastDay structure
 *
 * @param day Entry to free.
 *
 */
static void
forecast_forecastday_free(ForecastDay * day)
{
  g_free(day->day_);
  g_free(day->conditions_);
}

/**
 * Provides the mechanism to copy all data associated with
 * the ForecastDay structure
 *
 * @param dst Pointer to copy to.
 * @param src Pointer to copy from.
 */
static void
forecast_forecastday_copy(ForecastDay * dst, ForecastDay * src)
{
  SAFE_STRNDUP(dst->day_,        src->day_);
  SAFE_STRNDUP(dst->conditions_, src->conditions_);

  dst->high_ = src->high_;
  dst->low_  = src->low_;
  dst->code_ = src->code_;
}

/**
 * Provides the mechanism to free all data associated with
 * the ForecastUnits structure
 *
 * @param units Entry to free.
 *
 */
static void
forecast_units_free(ForecastUnits * units)
{
  g_free(units->distance_);
  g_free(units->pressure_);
  g_free(units->speed_);
  g_free(units->temperature_);
}

/**
 * Provides the mechanism to copy all data associated with
 * the ForecastUnits structure
 *
 * @param dst Pointer to copy to.
 * @param src Pointer to copy from.
 *
 */
static void
forecast_units_copy(ForecastUnits * dst, ForecastUnits * src )
{
  SAFE_STRNDUP(dst->distance_,    src->distance_);
  SAFE_STRNDUP(dst->pressure_,    src->pressure_);
  SAFE_STRNDUP(dst->speed_,       src->speed_);
  SAFE_STRNDUP(dst->temperature_, src->temperature_);
}

/**
 * Provides the mechanism to free any data associated with
 * the ForecastInfo structure
 *
 * @param forecast Entry to free.
 *
 */
void
forecast_free(gpointer forecast)
{
  if (!forecast) {
    return;
  }

  ForecastInfo * info = (ForecastInfo *)forecast;

  forecast_units_free(&info->units_);

  int i = 0;
  for (; i < FORECAST_MAX_DAYS; i++) {
    forecast_forecastday_free(&(info->days_[i]));
  }

  g_free(info->windDirection_);
  g_free(info->sunrise_);
  g_free(info->sunset_);
  g_free(info->conditions_);
  g_free(info->time_);
  g_free(info->imageURL_);
  
  if (info->image_) {
    g_object_unref(info->image_);
  }

  g_free(forecast);
}

/**
 * Copies a forecast entry.
 *
 * @param dst Address of the pointer to the forecast to set.
 * @param src Pointer to the forecast to use/copy.
 *
 * @note Destination is first freed, if non-NULL, otherwise a new allocation
 *       is made. Both source and destination forecast ptrs must be released by
 *       the caller.
 */
void
forecast_copy(gpointer * dst, gpointer src)
{
  if (!src || !dst) {
    return;
  }

  forecast_free(*dst);

  *dst = g_try_new0(ForecastInfo, 1);

  if (*dst) {
    ForecastInfo * df = (ForecastInfo *) *dst;
    ForecastInfo * sf = (ForecastInfo *)  src;

    df->units_         = sf->units_;
    df->pressureState_ = sf->pressureState_;

    int i = 0;
    for (; i < FORECAST_MAX_DAYS; i++) {
      forecast_forecastday_copy(&(df->days_[i]), &(sf->days_[i]));
    }

    forecast_units_copy(&(df->units_), &(sf->units_));

    df->windChill_   = sf->windChill_;
    df->windSpeed_   = sf->windSpeed_;
    df->humidity_    = sf->humidity_;
    df->pressure_    = sf->pressure_;
    df->visibility_  = sf->visibility_;
    df->temperature_ = sf->temperature_;
    SAFE_STRNDUP(df->windDirection_, sf->windDirection_);
    SAFE_STRNDUP(df->sunrise_,       sf->sunrise_);
    SAFE_STRNDUP(df->sunset_,        sf->sunset_);
    SAFE_STRNDUP(df->time_,          sf->time_);
    SAFE_STRNDUP(df->conditions_,    sf->conditions_);
    SAFE_STRNDUP(df->imageURL_,      sf->imageURL_);

    df->image_ = sf->image_;
    g_object_ref(df->image_);
  }
}

/**
 * Prints the contents of the supplied entry to stdout
 *
 * @param forecast Entry contents of which to print.
 *
 */
void
forecast_print(gpointer forecast G_GNUC_UNUSED)
{
#ifdef DEBUG
  if (!forecast) {
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

  LXW_LOG(LXW_VERBOSE, "\t%d-day forecast:", FORECAST_MAX_DAYS);

  int i = 0;
  for (; i <
         FORECAST_MAX_DAYS; i++) {
    LXW_LOG(LXW_VERBOSE, "\t\t%s: High: %d%s, Low: %d%s, Conditions[%d]: %s",
          (const char *)info->days_[i].day_,
                        info->days_[i].high_,
          (const char *)info->units_.temperature_,
                        info->days_[i].low_,
          (const char *)info->units_.temperature_,
                        info->days_[i].code_,
          (const char *)info->days_[i].conditions_);
  }

#endif
}
