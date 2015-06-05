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

/* Provides http protocol utility functions */

#include "httputil.h"

#include <string.h>

#include <libxml/nanohttp.h>
#include <libxml/xmlmemory.h>

#define READ_BUFSZ 1024

/**
 * Cleans up the nano HTTP state
 *
 * @param ctxt        HTTP Context
 * @param contenttype Content-type container
 */
static void
cleanup(void * ctxt, char * contenttype)
{
  if (ctxt)
    {
      xmlNanoHTTPClose(ctxt);
    }

  if (contenttype)
    {
      xmlFree(contenttype);
    }

  xmlNanoHTTPCleanup();
}

/**
 * Returns the contents of the requested URL
 *
 * @param url     The URL to retrieve.
 * @param rc      The return code supplied with the response.
 * @param datalen The resulting data length [out].
 *
 * @return A pointer to a null-terminated buffer containing the textual 
 *         representation of the response. Must be freed by the caller.
 */
gpointer
getURL(const gchar * url, gint * rc, gint * datalen)
{
  /* nanohttp magic */
  gint readlen = 0;
  gint currlen = 0;

  gpointer inbuf    = NULL;
  gpointer inbufRef = NULL;

  gchar readbuf[READ_BUFSZ];
  bzero(readbuf, READ_BUFSZ);

  xmlNanoHTTPInit();

  char * contenttype = NULL;
  void * ctxt        = NULL;

  ctxt = xmlNanoHTTPOpen(url, &contenttype);

  if (!ctxt)
    {
      // failure
      cleanup(ctxt, contenttype);

      *rc = -1;

      return inbuf; // it's NULL
    }

  *rc = xmlNanoHTTPReturnCode(ctxt);

  if (*rc != HTTP_STATUS_OK)
    {
      // failure
      cleanup(ctxt, contenttype);

      return inbuf; // it's NULL
    }

  while ((readlen = xmlNanoHTTPRead(ctxt, readbuf, READ_BUFSZ)) > 0)
    {
      // set return code
      *rc = xmlNanoHTTPReturnCode(ctxt);

      /* Maintain pointer to old location, free on failure */
      inbufRef = inbuf;

      inbuf = g_try_realloc(inbuf, currlen + readlen);

      if (!inbuf || *rc != HTTP_STATUS_OK)
        {
          // failure
          cleanup(ctxt, contenttype);

          g_free(inbufRef);

          return inbuf; // it's NULL
        }

      memcpy(inbuf + currlen, readbuf, readlen);
      
      currlen += readlen;

      // clear read buffer
      bzero(readbuf, READ_BUFSZ);

      *datalen = currlen;
    }

  if (readlen < 0)
    {
      // error
      g_free(inbuf);

      inbuf = NULL;
    }
  else
    {
      /* Maintain pointer to old location, free on failure */
      inbufRef = inbuf;

      // need to add '\0' at the end
      inbuf = g_try_realloc(inbuf, currlen + 1);

      if (!inbuf)
        {
          // failure
          g_free(inbufRef);

          inbuf = NULL;
        }
      else
        {
          memcpy(inbuf + currlen, "\0", 1);
        }
    }
  
  // finish up
  cleanup(ctxt, contenttype);

  return inbuf;
}
