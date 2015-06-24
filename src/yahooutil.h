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

/* Provides utilities to use Yahoo's weather services */

#ifndef LXWEATHER_YAHOOUTIL_HEADER
#define LXWEATHER_YAHOOUTIL_HEADER

#include <glib.h>

/**
 * Retrieves the details for the specified location
 *
 * @param location The string containing the name/code of the location
 *
 * @return A pointer to a list of LocationInfo entries, possibly empty, 
 *         if no details were found. Caller is responsible for freeing the list.
 */
GList *
yahooutil_location_find(const gchar * location);

/**
 * Retrieves the forecast for the specified location WOEID
 *
 * @param woeid The string containing the WOEID of the location
 * @param units The character containing the units for the forecast (c|f)
 * @param forecast The pointer to the forecast to be filled. If set to NULL,
 *                 a new one will be allocated.
 *
 */
void
yahooutil_forecast_get(const gchar * woeid, const gchar units, gpointer forecast);

/**
 * Initializes the internals: XML and HTTP
 *
 */
void
yahooutil_init(void);

/**
 * Cleans up the internals: XML and HTTP
 *
 */
void
yahooutil_cleanup(void);

gint
forecast_response_parse(gpointer response, gpointer * forecast);

#endif
