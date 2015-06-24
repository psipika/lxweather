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

/* Provides implementation for LocationInfo-specific functions */

#include "location.h" // includes glib.h
#include "logutil.h"

#include <stdio.h>
#include <string.h>

#define RESET_PROP_VALUE(p, v, sz)  \
  do {                              \
    if (p) {                        \
      g_free(p);                    \
    }                               \
    p = g_strndup(v, sz);           \
  } while (0);

const gchar * LocationInfoFieldNames[] = { "alias",
                                           "city",
                                           "state",
                                           "country",
                                           "woeid",
                                           "units",
                                           "interval",
                                           "enabled",
                                           NULL};

/**
 * Provides the mechanism to free any data associated with 
 * the LocationInfo structure
 *
 * @param location Location entry to free.
 *
 */
void
location_free(gpointer location)
{
  if (!location) {
    return;
  }

  LocationInfo * info = (LocationInfo *)location;

  g_free(info->alias_);
  g_free(info->city_);
  g_free(info->state_);
  g_free(info->country_);
  g_free(info->woeid_);

  g_free(location);
}

/**
 * Prints the contents of the supplied entry to stdout
 *
 * @param location Entry contents of which to print.
 *
 */
void
location_print(gpointer location G_GNUC_UNUSED)
{
#ifdef DEBUG
  if (!location) {
    LXW_LOG(LXW_ERROR, "location::printLocation(): Entry: NULL");
      
    return;
  }

  LocationInfo * info = (LocationInfo *)location;

  LXW_LOG(LXW_VERBOSE, "Entry:");
  LXW_LOG(LXW_VERBOSE, "\tAlias: %s",    (const char *)info->alias_);
  LXW_LOG(LXW_VERBOSE, "\tCity: %s",     (const char *)info->city_);
  LXW_LOG(LXW_VERBOSE, "\tState: %s",    (const char *)info->state_);
  LXW_LOG(LXW_VERBOSE, "\tCountry: %s",  (const char *)info->country_);
  LXW_LOG(LXW_VERBOSE, "\tWOEID: %s",    (const char *)info->woeid_);
  LXW_LOG(LXW_VERBOSE, "\tUnits: %c",    (info->units_)?info->units_:'A');
  LXW_LOG(LXW_VERBOSE, "\tInterval: %u", info->interval_);
  LXW_LOG(LXW_VERBOSE, "\tEnabled: %s",  (info->enabled_)?"yes":"no");
#endif
}


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
                      gsize         len)
{
  if (!location) {
    LXW_LOG(LXW_ERROR, "Location: NULL");

    return;
  }

  LocationInfo * info = (LocationInfo *)location;

  if (!strcmp(property, "city")) {
    RESET_PROP_VALUE(info->city_, value, len);
  } else if (!strcmp(property, "state")) {
    RESET_PROP_VALUE(info->state_, value, len);
  } else if (!strcmp(property, "country")) {
    RESET_PROP_VALUE(info->country_, value, len);
  } else if (!strcmp(property, "woeid")) {
    RESET_PROP_VALUE(info->woeid_, value, len);
  } else if (!strcmp(property, "line2")) {
    RESET_PROP_VALUE(info->alias_, value, len);
  } else if (!strcmp(property, "line4")) {
    RESET_PROP_VALUE(info->country_, value, len);
  }
}

/**
 * Copies a location entry.
 *
 * @param dst Address of the pointer to the location to set.
 * @param src      Pointer to the location to use/copy.
 *
 * @note Destination is first freed, if non-NULL, otherwise a new allocation
 *       is made. Both source and destination locations must be released by
 *       the caller.
 */
void
location_copy(gpointer * dst, gpointer src)
{
  if (!src || !dst) {
    return;
  }

  if ((LocationInfo *) *dst) {
    /* Check if the two are the same, first */
    LocationInfo * dstinfo = (LocationInfo *) *dst;
    LocationInfo * srcinfo = (LocationInfo *) src;

    if (!strncmp(dstinfo->woeid_, srcinfo->woeid_, strlen(srcinfo->woeid_))) {
      /* they're the same, no need to copy, just assign alias */
      location_property_set(*dst,
                            "alias",
                            srcinfo->alias_,
                            (srcinfo->alias_) ? strlen(srcinfo->alias_) : 0);
          
      return;
    }

    location_free(*dst);

    *dst = NULL;
  }

  /* allocate new */
  *dst = g_try_new0(LocationInfo, 1);

  if (*dst) {
    LocationInfo * dstinfo = (LocationInfo *) *dst;
    LocationInfo * srcinfo  = (LocationInfo *)src;

    dstinfo->alias_ = g_strndup(srcinfo->alias_,
                                (srcinfo->alias_)?strlen(srcinfo->alias_):0);
    
    dstinfo->city_ = g_strndup(srcinfo->city_,
                               (srcinfo->city_)?strlen(srcinfo->city_):0);
    
    dstinfo->state_ = g_strndup(srcinfo->state_,
                                (srcinfo->state_)?strlen(srcinfo->state_):0);
    
    dstinfo->country_ = g_strndup(srcinfo->country_,
                                  (srcinfo->country_)?strlen(srcinfo->country_):0);
    
    dstinfo->woeid_ = g_strndup(srcinfo->woeid_,
                                (srcinfo->woeid_)?strlen(srcinfo->woeid_):0);
    
    dstinfo->units_    = (srcinfo->units_) ? srcinfo->units_ : 'f';
    dstinfo->interval_ = srcinfo->interval_;
    dstinfo->enabled_  = srcinfo->enabled_;
  }
  
}
