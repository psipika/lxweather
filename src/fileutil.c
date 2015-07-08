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

/* Defines the helper functions for configuration file handling 
  (reading and writing) */

#include "fileutil.h"
#include "location.h"
#include "logutil.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <ctype.h>
#include <stdarg.h>

#include <gio/gio.h>

static void     location_list_fill   (GList ** list, GKeyFile * keyfile);
static gboolean config_path_create   (const gchar * path);
static gboolean key_file_fill        (GKeyFile ** keyfile, GList * list);

/**
 * Reads configuration from the specified path and returns a list of 
 * LocationInfo pointers.
 *
 * @param path Path to configuration file with key=value pairs.
 *
 * @return a list of LocationInfo pointers, or NULL on error
 *
 * @note The caller must free the returned list.
 */
GList *
fileutil_config_locations_load(const gchar * path)
{
  LXW_LOG(LXW_DEBUG, "fileUtil::fileutil_config_locations_load(%s)", path);

  GList  * list = NULL;
  GError * pError = NULL;

  GKeyFile * keyfile = g_key_file_new();

  if (g_key_file_load_from_file(keyfile, path, G_KEY_FILE_NONE, &pError)) {
    location_list_fill(&list, keyfile);
  } else {
    /* An error occurred... */
    if (pError) {
      LXW_LOG(LXW_ERROR, "Failed to read configuration at %s: %s",
              path, pError->message);

      g_error_free(pError);
    }

  }

  g_key_file_free(keyfile);

  /* Reverse list, if filled */
  list = g_list_reverse(list);

  return list;
}

/**
 * Creates and fills 'Location' sections based on passed-in LocationInfo
 * objects.
 *
 * @param list Pointer to the list with LocationInfo objects.
 * @param path Path to the file where to save the locations.
 */
void
fileutil_config_locations_save(GList * list, const gchar * path)
{
  LXW_LOG(LXW_DEBUG, "fileUtil::fileutil_config_locations_save(%s)", path);

  GKeyFile * keyfile = g_key_file_new();

  /* populate key file object */
  if (!key_file_fill(&keyfile, list)) {
    /* No valid entries */
    return;
  }

  /* Check if directory exists, create if it doesn't */
  if (!config_path_create(path)) {
    return;
  }

  GFile * file = g_file_new_for_path(path);

  /* Get an output stream and write to it */
  GError * pError = NULL;

  GFileOutputStream * ostream = g_file_replace(file,
                                               NULL,
                                               TRUE,
                                               G_FILE_CREATE_PRIVATE,
                                               NULL,
                                               &pError);

  if (ostream) {
    /* Everything is OK */
    gsize datalen = 0;

    gchar * data = g_key_file_to_data(keyfile, &datalen, &pError);

    if (pError) {
      LXW_LOG(LXW_ERROR, "Failed to convert key file to data: %s", pError->message);

      g_error_free(pError);
    } else {
      gsize writelen = 0;

      if (!g_output_stream_write_all((GOutputStream *)ostream,
                                     data,
                                     datalen,
                                     &writelen,
                                     NULL,
                                     &pError)) {
        /* Failed */
        LXW_LOG(LXW_ERROR, "Failed to write to output stream: %s", pError->message);

        g_error_free(pError);
      }

    }

    g_free(data);
      
    g_object_unref(ostream);
  } else {
    LXW_LOG(LXW_ERROR, "Failed to create %s: %s", path, pError->message);

    g_error_free(pError);
  }

  g_object_unref(file);

  g_key_file_free(keyfile);
}

/**
 * Creates the configuration directory specified at startup.
 *
 * @param path Path to the configuration file to use.
 *
 * @return TRUE on success, FALSE on any error or failure.
 */
static gboolean
config_path_create(const gchar * path)
{
  LXW_LOG(LXW_DEBUG, "fileUtil::config_path_create(%s)", path);

  gboolean retval = FALSE;

  gchar * dirpath = g_path_get_dirname(path);

  struct stat st;

  if (stat(dirpath, &st) == -1) {
    /* Non-existence is not an error */
    if (errno == ENOENT) {
      if (mkdir(dirpath, S_IRWXU | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH) == 0) {
        retval = TRUE;
      } else {
        /* Check error number, if the directory exists, we're OK */
        if (errno == EEXIST) {
          retval = TRUE;
        } else {
          LXW_LOG(LXW_ERROR, "Failed to retrieve file information on %s: %s",
                  dirpath, strerror(errno));
        }

      }

    } else {
      LXW_LOG(LXW_ERROR, "Failed to retrieve file information on %s: %s",
              dirpath, strerror(errno));
    }

  } else if (S_ISDIR(st.st_mode)) {
    retval = TRUE;
  }

  g_free(dirpath);

  return retval;
}

/**
 * Goes through all 'Location' sections and creates LocationInfo objects
 * based on key=value pairs
 *
 * @param list    Pointer to the LocationInfo list pointer.
 * @param keyfile Pointer to the configuration file.
 */
static void
location_list_fill(GList ** list, GKeyFile * keyfile)
{
  LXW_LOG(LXW_DEBUG, "fileUtil::location_list_fill()");

  gsize groupcnt = 0;
  gsize groupidx = 0;

  gchar ** groupnames = g_key_file_get_groups(keyfile, &groupcnt);

  for (; groupidx < groupcnt; ++groupidx) {
    /* See if this group is 'Location N' */
      
    gchar ** nametokens = g_strsplit(groupnames[groupidx], " ", 2);
                                             
    if (g_ascii_strncasecmp(nametokens[0],
                            LOCATIONINFO_GROUP_NAME,
                            LOCATIONINFO_GROUP_NAME_LENGTH)) {
      /* A match would produce a FALSE return value, 
       * so this group is not 'Location N' */
      LXW_LOG(LXW_ERROR, "Group: '%s' not handled", groupnames[groupidx]);
    } else {          
      gchar * woeid = g_key_file_get_string(keyfile,
                                            groupnames[groupidx],
                                            LocationInfoFieldNames[WOEID],
                                            NULL);
              
      gchar * alias = g_key_file_get_string(keyfile,
                                            groupnames[groupidx],
                                            LocationInfoFieldNames[ALIAS],
                                            NULL);

      LXW_LOG(LXW_DEBUG, "Group name: %s, Alias: %s, WOEID: %s",
              groupnames[groupidx], alias, woeid);

      /* We MUST have WOEID and Alias */
      if (!woeid || !strlen(woeid) || !alias || !strlen(alias)) {
        /* just in case they're emtpy strings */
        g_free(woeid);
        g_free(alias);

        continue;
      }

      gchar * city = g_key_file_get_string(keyfile,
                                           groupnames[groupidx],
                                           LocationInfoFieldNames[CITY],
                                           NULL);

      gchar * state = g_key_file_get_string(keyfile,
                                            groupnames[groupidx],
                                            LocationInfoFieldNames[STATE],
                                            NULL);

      gchar * country = g_key_file_get_string(keyfile,
                                              groupnames[groupidx],
                                              LocationInfoFieldNames[COUNTRY],
                                              NULL);

      gchar * units = g_key_file_get_string(keyfile,
                                            groupnames[groupidx],
                                            LocationInfoFieldNames[UNITS],
                                            NULL);

      gint interval = g_key_file_get_integer(keyfile,
                                             groupnames[groupidx],
                                             LocationInfoFieldNames[INTERVAL],
                                             NULL);

      gboolean enabled = g_key_file_get_boolean(keyfile,
                                                groupnames[groupidx],
                                                LocationInfoFieldNames[ENABLED],
                                                NULL);

      LocationInfo * location = g_try_new0(LocationInfo, 1);

      if (location) {
        location->alias_     = g_strndup(alias, strlen(alias));
        location->city_      = (city)?g_strndup(city, strlen(city)):NULL;
        location->state_     = (state)?g_strndup(state, strlen(state)):NULL;
        location->country_   = (country)?g_strndup(country, strlen(country)):NULL;
        location->woeid_     = g_strndup(woeid, strlen(woeid));
        location->units_     = units ? units[0] : 'f';
        location->interval_  = (interval > 0) ? interval : 1;
        location->enabled_   = enabled;

        *list = g_list_prepend(*list, location);
      }

      g_free(alias);
      g_free(city);
      g_free(state);
      g_free(country);
      g_free(woeid);
      g_free(units);
    }

    /* Free the token list */
    g_strfreev(nametokens);
  }

  g_strfreev(groupnames);
}

/**
 * Fills the supplied key file with data from the list.
 *
 * @param keyfile Pointer to the key file to fill in
 * @param list    Pointer to the list to use data from
 *
 * @return TRUE if there was at least one location to save, FALSE otherwise.
 */
static gboolean
key_file_fill(GKeyFile ** keyfile, GList * list)
{
  LXW_LOG(LXW_DEBUG, "fileUtil::key_file_fill()");

  gboolean retval = FALSE;

  gint listsz = g_list_length(list);

  int i = 0;
  for (; i < listsz; ++i) {
    LocationInfo * location = (LocationInfo *)g_list_nth_data(list, i);

    if (location) {
      gchar * group = g_strdup_printf("Location %d", i + 1);

      g_key_file_set_string(*keyfile, group, 
                            LocationInfoFieldNames[ALIAS],
                            location->alias_);
          
      if (location->city_) {
        g_key_file_set_string(*keyfile, group, 
                              LocationInfoFieldNames[CITY],
                              location->city_);
      }

      if (location->state_) {
        g_key_file_set_string(*keyfile, group, 
                              LocationInfoFieldNames[STATE],
                              location->state_);
      }

      if (location->country_) {
        g_key_file_set_string(*keyfile, group, 
                              LocationInfoFieldNames[COUNTRY],
                              location->country_);
      }

      g_key_file_set_string(*keyfile,
                            group,
                            LocationInfoFieldNames[WOEID],
                            location->woeid_);
          
      g_key_file_set_string(*keyfile,
                            group, 
                            LocationInfoFieldNames[UNITS],
                            &location->units_);
          
      g_key_file_set_integer(*keyfile,
                             group, 
                             LocationInfoFieldNames[INTERVAL],
                             (gint)location->interval_);
          
      g_key_file_set_boolean(*keyfile, group,
                             LocationInfoFieldNames[ENABLED],
                             location->enabled_);

      g_free(group);

      retval = TRUE;
    }

  }

  return retval;
}
