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

//@TODO: this module may need a static mutex to protect the library.

/**
 * Cleans up the nano HTTP state
 *
 * @param ctxt        HTTP Context
 * @param contenttype Content-type container
 */
static void
cleanup(void * ctxt, char * contenttype)
{
  if (ctxt) {
    xmlNanoHTTPClose(ctxt);
  }

  if (contenttype) {
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
httputil_url_get(const gchar * url, gint * rc, gint * datalen)
{
  /* nanohttp magic */
  gint readlen = 0;
  gint currlen = 0;

  gpointer inbuf    = NULL;
  gpointer inbufref = NULL;

  gchar readbuf[READ_BUFSZ];
  memset(readbuf, 0, READ_BUFSZ);

  xmlNanoHTTPInit();

  char * contenttype = NULL;
  void * ctxt        = NULL;
  
  ctxt = xmlNanoHTTPOpen(url, &contenttype);

  if (!ctxt) {
    cleanup(ctxt, contenttype);

    *rc = -1;

    return inbuf; // it's NULL
  }

  *rc = xmlNanoHTTPReturnCode(ctxt);

  if (*rc != HTTP_STATUS_OK) {
    cleanup(ctxt, contenttype);

    return inbuf; // it's NULL
  }
  
  while ((readlen = xmlNanoHTTPRead(ctxt, readbuf, READ_BUFSZ)) > 0) {
    *rc = xmlNanoHTTPReturnCode(ctxt);

    /* Maintain pointer to old location, free on failure */
    inbufref = inbuf;

    inbuf = g_try_realloc(inbuf, currlen + readlen);

    if (!inbuf || *rc != HTTP_STATUS_OK) {
      cleanup(ctxt, contenttype);

      g_free(inbufref);

      return inbuf; // it's NULL
    }

    memcpy(inbuf + currlen, readbuf, readlen);
      
    currlen += readlen;

    memset(readbuf, 0, READ_BUFSZ);

    *datalen = currlen;
  }

  if (readlen < 0) {
    g_free(inbuf);

    inbuf = NULL;
  } else {
    /* Maintain pointer to old location, free on failure */
    inbufref = inbuf;

    /* Need to add '\0' at the end since we're going to treat this buffer
     * as a char buffer */
    inbuf = g_try_realloc(inbuf, currlen + 1);

    if (!inbuf) {
      g_free(inbufref);

      inbuf = NULL;
    } else {
      memcpy(inbuf + currlen, "\0", 1);
    }
  }
  
  cleanup(ctxt, contenttype);

  return inbuf;
}
