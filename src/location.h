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

/* Defines the layout of the location structure */

#ifndef LXWEATHER_LOCATION_HEADER
#define LXWEATHER_LOCATION_HEADER

#include <glib.h>
#include <string.h>

/* */
#define LOCATIONINFO_GROUP_NAME "Location"
#define LOCATIONINFO_GROUP_NAME_LENGTH strlen(LOCATIONINFO_GROUP_NAME)

/* LocationInfo struct definition */
typedef struct 
{
  gchar *  alias_;
  gchar *  city_;
  gchar *  state_;
  gchar *  country_;
  gchar *  woeid_;
  gchar    units_;
  guint    interval_;
  gboolean enabled_;
} LocationInfo;

/* Configuration helpers */
typedef enum
{
  ALIAS = 0,
  CITY,
  STATE,
  COUNTRY,
  WOEID,
  UNITS,
  INTERVAL,
  ENABLED,
  LOCATIONINFO_FIELD_COUNT
} LocationInfoField;

/* Defined in the .c file - specifies the array of field names */
extern const gchar * LocationInfoFieldNames[];

/**
 * Provides the mechanism to free any data associated with 
 * the LocationInfo structure
 *
 * @param location Location entry to free.
 *
 */
void
location_free(gpointer location);

/**
 * Prints the contents of the supplied entry to stdout
 *
 * @param locatrion Location entry contents of which to print.
 *
 */
void
location_print(gpointer location);

/**
 * Sets the given property for the location
 *
 * @param location Pointer to the location to modify.
 * @param property Name of the property to set.
 * @param value    Value to assign to the property.
 * @param len      Length of the value to assign to the property (think strlen())
 */
void
location_property_set(gpointer      location,
                      const gchar * property,
                      const gchar * value,
                      gsize         len);

/**
 * Copies a location entry.
 *
 * @param dst Address of the pointer to the location to set.
 * @param src Pointer to the location to use/copy.
 *
 * @note Destination is first freed, if non-NULL, otherwise a new allocation
 *       is made. Both source and destination locations must be released by
 *       the caller.
 */
void
location_copy(gpointer * dst, gpointer src);

#endif
