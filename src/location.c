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
freeLocation(gpointer location)
{
  if (!location)
    {
      return;
    }

  LocationInfo * pEntry = (LocationInfo *)location;

  g_free(pEntry->alias_);
  g_free(pEntry->city_);
  g_free(pEntry->state_);
  g_free(pEntry->country_);
  g_free(pEntry->woeid_);

  g_free(location);
}

/**
 * Prints the contents of the supplied entry to stdout
 *
 * @param location Entry contents of which to print.
 *
 */
void
printLocation(gpointer location G_GNUC_UNUSED)
{
#ifdef DEBUG
  if (!location)
    {
      LXW_LOG(LXW_ERROR, "location::printLocation(): Entry: NULL");
      
      return;
    }

  LocationInfo * pInfo = (LocationInfo *)location;

  LXW_LOG(LXW_VERBOSE, "Entry:");
  LXW_LOG(LXW_VERBOSE, "\tAlias: %s",    (const char *)pInfo->alias_);
  LXW_LOG(LXW_VERBOSE, "\tCity: %s",     (const char *)pInfo->city_);
  LXW_LOG(LXW_VERBOSE, "\tState: %s",    (const char *)pInfo->state_);
  LXW_LOG(LXW_VERBOSE, "\tCountry: %s",  (const char *)pInfo->country_);
  LXW_LOG(LXW_VERBOSE, "\tWOEID: %s",    (const char *)pInfo->woeid_);
  LXW_LOG(LXW_VERBOSE, "\tUnits: %c",    pInfo->units_);
  LXW_LOG(LXW_VERBOSE, "\tInterval: %u", pInfo->interval_);
  LXW_LOG(LXW_VERBOSE, "\tEnabled: %s",  (pInfo->enabled_)?"yes":"no");
#endif
}


/**
 * Sets the alias for the location
 *
 * @param location Pointer to the location to modify
 * @param alias    Alias value to use
 *
 */
void
setLocationAlias(gpointer location, gpointer alias)
{
  if (!location)
    {
      LXW_LOG(LXW_ERROR, "Location: NULL");

      return;
    }

  LocationInfo * pLocation = (LocationInfo *)location;

  const gchar * pAlias = (const gchar *)alias;

  gsize aliaslen = (pAlias)?strlen(pAlias):0;

  if (pLocation->alias_)
    {
      g_free(pLocation->alias_);
    }

  pLocation->alias_ = g_strndup(pAlias, aliaslen);
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
copyLocation(gpointer * dst, gpointer src)
{
  if (!src || !dst)
    {
      return;
    }

  if ((LocationInfo *) *dst)
    {
      /* Check if the two are the same, first */
      LocationInfo * pDstLocation = (LocationInfo *) *dst;

      LocationInfo * pSrcLocation = (LocationInfo *)src;

      if (!strncmp(pDstLocation->woeid_, pSrcLocation->woeid_, strlen(pSrcLocation->woeid_)))
        {
          /* they're the same, no need to copy, just assign alias */
          setLocationAlias(*dst, pSrcLocation->alias_);
          
          return;
        }

      freeLocation(*dst);

      *dst = NULL;
    }

  /* allocate new */
  *dst = g_try_new0(LocationInfo, 1);

  if (*dst)
    {
      LocationInfo * pDest = (LocationInfo *) *dst;
      LocationInfo * pSrc  = (LocationInfo *)src;

      pDest->alias_    = g_strndup(pSrc->alias_,   (pSrc->alias_)?strlen(pSrc->alias_):0);
      pDest->city_     = g_strndup(pSrc->city_,    (pSrc->city_)?strlen(pSrc->city_):0);
      pDest->state_    = g_strndup(pSrc->state_,   (pSrc->state_)?strlen(pSrc->state_):0);
      pDest->country_  = g_strndup(pSrc->country_, (pSrc->country_)?strlen(pSrc->country_):0);
      pDest->woeid_    = g_strndup(pSrc->woeid_,   (pSrc->woeid_)?strlen(pSrc->woeid_):0);
      pDest->units_    = (pSrc->units_) ? pSrc->units_ : 'f';
      pDest->interval_ = pSrc->interval_;
      pDest->enabled_  = pSrc->enabled_;
    }
  
}
