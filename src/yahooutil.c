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

#include "httputil.h"
#include "location.h"
#include "forecast.h"
#include "logutil.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <locale.h>

#include <libxml/parser.h>
#include <libxml/tree.h>
#include <libxml/xpath.h>
#include <libxml/xmlstring.h>
#include <libxml/uri.h>

#include <pthread.h>

#include <glib.h>

#include <gtk/gtk.h>
#include <gio/gio.h>

#define XMLCHAR_P(x)      (xmlChar *)(x)
#define CONSTXMLCHAR_P(x) (const xmlChar *)(x)
#define CONSTCHAR_P(x)    (const char *)(x)
#define CHAR_P(x)         (char *)(x)

#define WOEID_QUERY       "SELECT%20*%20FROM%20geo.placefinder%20WHERE%20text="
#define FORECAST_QUERY_P1 "SELECT%20*%20FROM%20weather.forecast%20WHERE%20woeid="
#define FORECAST_QUERY_P2 "%20and%20u="
#define FORECAST_URL      "http://query.yahooapis.com/v1/public/yql?format=xml&q="

/* the '7' is for two extra '%22' and a '\0' */
#define WOEID_QUERY_LEN \
  strlen(FORECAST_URL WOEID_QUERY) + 7

/* all strings plus four quotes '%27', units char and a '\0' */
#define FORECAST_QUERY_LEN \
  strlen(FORECAST_URL FORECAST_QUERY_P1 FORECAST_QUERY_P2) + 14

static gint g_initialized = 0;

/**
 * Generates the WOEID query string
 *
 * @param query    Buffer to contain the query
 * @param location Location string
 *
 * @return 0 on success, -1 on failure
 */
static gint
woeid_query_gen(gchar * query, const gchar * location)
{
  gsize totalsz = WOEID_QUERY_LEN + strlen(location);
  
  snprintf(query, totalsz, "%s%s%%22%s%%22",
           FORECAST_URL, WOEID_QUERY, location);

  return 0;
}

/**
 * Generates the forecast query string
 *
 * @param query Buffer to contain the query
 * @param woeid WOEID string
 * @param units Units character (length of 1)
 *
 * @return 0 on success, -1 on failure
 */
static gint
forecast_query_gen(gchar * query, const gchar * woeid, const gchar units)
{
  gsize totalsz = FORECAST_QUERY_LEN + strlen(woeid);
 
  snprintf(query, totalsz, "%s%s%%22%s%%22%s%%22%c%%22",
           FORECAST_URL,
           FORECAST_QUERY_P1, woeid,
           FORECAST_QUERY_P2, units);

  return 0;
}

/**
 * Converts the passed-in string from UTF-8 to ASCII for http transmisison.
 *
 * @param instr String to convert
 *
 * @return The converted string which MUST BE FREED BY THE CALLER.
 */
static gchar *
locale_to_ascii(const gchar *instr)
{
  // for UTF-8 to ASCII conversions
  setlocale(LC_CTYPE, "en_US");

  GError * pError = NULL;

  gsize readsz  = 0;
  gsize writesz = 0;

  gchar * outstr = g_convert(instr,
                             strlen(instr),
                             "ASCII//TRANSLIT",
                             "UTF-8",
                             &readsz,
                             &writesz,
                             &pError);

  if (pError) {
    LXW_LOG(LXW_ERROR, "yahooutil::locale_to_ascii(%s): Error: %s", 
            instr, pError->message);

    g_error_free(pError);

    outstr = g_strndup(instr, strlen(instr));
  }

  // now escape space, if any
  xmlChar * escapedstr = xmlURIEscapeStr((const xmlChar *)outstr, NULL);

  if (escapedstr) {
    // release ConvertedString, reset it, then release EscapedString.
    // I know it's confusing, but keeps everything as a gchar and g_free
    g_free(outstr);

    outstr = g_strndup((const gchar *)escapedstr,
                       strlen((const gchar *)escapedstr));

    xmlFree(escapedstr);
  }

  // restore locale to default
  setlocale(LC_CTYPE, "");

  return outstr;
}

/**
 * Compares two strings and then sets the storage variable to the second
 * value if the two do not match. The storage variable is cleared first.
 *
 * @param dststr Pointer to the storage location with the first value.
 * @param srcstr The second string.
 * @param srclen The length of the second string.
 *
 * @return 0 on succes, -1 on failure.
 */
static gint
string_if_different_set(gchar ** dststr, 
                     const gchar * srcstr,
                     const gsize srclen)
{
  // if diffrent, clear and set
  if (g_strcmp0(*dststr, srcstr)) {
    g_free(*dststr);

    *dststr = g_strndup(srcstr, srclen);
  }

  return 0;
}

/**
 * Compares the URL of an image to the 'new' value. If the two
 * are different, the image at the 'new' URL is retrieved and replaces
 * the old one. The old one is freed.
 *
 * @param dsturl    Pointer to the storage location with the first value.
 * @param image     Pointer to the image storage.
 * @param newurl    The new url.
 * @param newurllen The length of the new URL.
 *
 * @return 0 on succes, -1 on failure.
 */
static gint
image_if_different_set(gchar       ** dsturl,
                       GdkPixbuf   ** image,
                       const gchar * newurl,
                       const gsize newurllen)
{
  GInputStream * instream = NULL;
  int err = 0;

  // if diffrent, clear and set
  if (g_strcmp0(*dsturl, newurl)) {
    g_free(*dsturl);

    *dsturl = g_strndup(newurl, newurllen);

    if (*image) {
      g_object_unref(*image);

      *image = NULL;
    }
      
    // retrieve the URL and create the new image
    gint rc      = 0;
    gint datalen = 0;

    gpointer response = httputil_url_get(newurl, &rc, &datalen);

    if (!response || rc != HTTP_STATUS_OK) {
      LXW_LOG(LXW_ERROR, "yahooutil::image_if_different_set(): Failed to get URL (%d, %d)", 
              rc, datalen);

      return -1;
    }

    instream = g_memory_input_stream_new_from_data(response,
                                                   datalen,
                                                   g_free);

    GError * pError = NULL;

    *image = gdk_pixbuf_new_from_stream(instream,
                                        NULL,
                                        &pError);

    if (!*image) {
      LXW_LOG(LXW_ERROR,
              "yahooutil::image_if_different_set(): PixBuff allocation failed: %s",
              pError->message);

      g_error_free(pError);
          
      err = -1;
    }

    if (!g_input_stream_close(instream, NULL, &pError)) {
      LXW_LOG(LXW_ERROR,
              "yahooutil::image_if_different_set(): InputStream closure failed: %s",
              pError->message);

      g_error_free(pError);

      err = -1;
    } else {
      g_object_unref(instream);
    }
      
  }

  return err;
}

/**
 * Compares an integer to a converted string and then sets the storage variable
 * to the second value if the two do not match.
 *
 * @param dst    Pointer to the storage location with the first value.
 * @param srcstr The second string.
 *
 * @return 0 on succes, -1 on failure.
 */
static gint
int_if_different_set(gint * dst, const gchar * srcstr)
{
  gint value = (gint)g_ascii_strtoll((srcstr)?srcstr:"0", NULL, 10);

  // if diffrent, set
  if (*dst != value) {
    *dst = value;
  }

  return 0;
}

/**
 * Processes the passed-in node to generate a LocationInfo entry
 *
 * @param node Pointer to the XML Result Node.
 *
 * @return A newly created LocationInfo entry on success, or NULL on failure.
 */
static gpointer
result_node_process(xmlNodePtr node)
{
  if (!node) {
    return NULL;
  }

  LocationInfo * location = (LocationInfo *)g_try_new0(LocationInfo, 1);

  if (!location) {
    return NULL;
  }

  xmlNodePtr curr = node->xmlChildrenNode;

  for (; curr != NULL; curr = curr->next) {
    if (curr->type == XML_ELEMENT_NODE) {
      const char * content = CONSTCHAR_P(xmlNodeListGetString(curr->doc, 
                                                              curr->xmlChildrenNode, 
                                                              1));
          
      gsize contentlen = ((content)?strlen(content):0); // 1 is null char

      location_property_set(location,
                            CONSTCHAR_P(curr->name),
                            content,
                            contentlen);

      xmlFree(XMLCHAR_P(content));
    }

  }

  return location;
}

/**
 * Processes the passed-in node to generate a LocationInfo entry
 *
 * @param forecast Pointer to the pointer to the ForecastInfo entry being filled.
 * @param node     Pointer to the XML Item Node.
 *
 * @return 0 on success, -1 on failure
 */
static gint
item_node_process(gpointer * forecast, xmlNodePtr node)
{
  if (!node || !forecast) {
    return -1;
  }

  ForecastInfo * info = *((ForecastInfo **)forecast);

  xmlNodePtr curr = node->xmlChildrenNode;

  int forecastcnt = 0;

  for (; curr != NULL; curr = curr->next){
    if (curr->type == XML_ELEMENT_NODE) {
      if (xmlStrEqual(curr->name, CONSTXMLCHAR_P("condition"))) {
        const char * date = CONSTCHAR_P(xmlGetProp(curr, XMLCHAR_P("date")));
        const char * temp = CONSTCHAR_P(xmlGetProp(curr, XMLCHAR_P("temp")));
        const char * text = CONSTCHAR_P(xmlGetProp(curr, XMLCHAR_P("text")));

        gsize datelen = ((date)?strlen(date):0);
        gsize textlen = ((text)?strlen(text):0);

        string_if_different_set(&(info->time_),       date, datelen);
        string_if_different_set(&(info->conditions_), text, textlen);

        int_if_different_set(&(info->temperature_), temp);

        xmlFree(XMLCHAR_P(date));
        xmlFree(XMLCHAR_P(temp));
        xmlFree(XMLCHAR_P(text));
      } else if (xmlStrEqual(curr->name, CONSTXMLCHAR_P("description"))) {
        char * content = CHAR_P(xmlNodeListGetString(curr->doc,
                                                     curr->xmlChildrenNode, 
                                                     1));
          
        char * saveptr = NULL;

        // need to skip quotes ("), both of them
        strtok_r(content, "\"", &saveptr);
        char * url = strtok_r(NULL, "\"", &saveptr);

        // found the image
        if (url && strstr(url, "yimg.com")) {
          LXW_LOG(LXW_DEBUG, "yahooutil::item_node_process(): IMG URL: %s",
                  url);

          image_if_different_set(&info->imageURL_, 
                                 &info->image_,
                                 url, 
                                 strlen(url));
        }
                  
        xmlFree(XMLCHAR_P(content));
      } else if (xmlStrEqual(curr->name, CONSTXMLCHAR_P("forecast"))) {
        const char * day  = CONSTCHAR_P(xmlGetProp(curr, XMLCHAR_P("day")));
        const char * high = CONSTCHAR_P(xmlGetProp(curr, XMLCHAR_P("high")));
        const char * low  = CONSTCHAR_P(xmlGetProp(curr, XMLCHAR_P("low")));
        const char * text = CONSTCHAR_P(xmlGetProp(curr, XMLCHAR_P("text")));
        const char * code = CONSTCHAR_P(xmlGetProp(curr, XMLCHAR_P("code")));

        gsize daylen  = ((day)?strlen(day):0);
        gsize textlen = ((text)?strlen(text):0);

        string_if_different_set(&(info->days_[forecastcnt].day_),        day,  daylen);
        string_if_different_set(&(info->days_[forecastcnt].conditions_), text, textlen);

        int_if_different_set(&(info->days_[forecastcnt].high_), high);
        int_if_different_set(&(info->days_[forecastcnt].low_),  low);
        int_if_different_set(&(info->days_[forecastcnt].code_), code);

        forecastcnt++;

        xmlFree(XMLCHAR_P(day));
        xmlFree(XMLCHAR_P(high));
        xmlFree(XMLCHAR_P(low));
        xmlFree(XMLCHAR_P(text));
        xmlFree(XMLCHAR_P(code));

        if (forecastcnt >= FORECAST_MAX_DAYS) {
          /* just to be on the safe side... */
          break;
        }
      }

    }

  }

  return 0;
}


/**
 * Processes the passed-in node to generate a ForecastInfo entry
 *
 * @param node     Pointer to the XML Channel Node.
 * @param forecast Pointer to the ForecastInfo entry to be filled in.
 *
 * @return A newly created ForecastInfo entry on success, or NULL on failure.
 */
static gpointer
channel_node_process(xmlNodePtr node, ForecastInfo * forecast)
{
  if (!node) {
    return NULL;
  }

  xmlNodePtr curr = node->xmlChildrenNode;

  for (; curr != NULL; curr = curr->next) {
    if (curr->type == XML_ELEMENT_NODE) {
      if (xmlStrEqual(curr->name, CONSTXMLCHAR_P("title"))) {
        /* Evaluate title to see if there was an error */
        char * content = CHAR_P(xmlNodeListGetString(curr->doc, 
                                                     curr->xmlChildrenNode, 
                                                     1));
              
        if (strstr(content, "Error")) {
          xmlFree(XMLCHAR_P(content));
                  
          do {
            curr = curr->next;
          } while (curr && !xmlStrEqual(curr->name, CONSTXMLCHAR_P("item")));

          xmlNodePtr child = (curr)?curr->xmlChildrenNode:NULL;
                  
          for (; child != NULL; child = child->next) {
            if (child->type == XML_ELEMENT_NODE && 
                xmlStrEqual(child->name, CONSTXMLCHAR_P("title"))) {
              content = CHAR_P(xmlNodeListGetString(child->doc, 
                                                    child->xmlChildrenNode, 
                                                    1));

              LXW_LOG(LXW_ERROR,
                      "yahooutil::channel_node_process(): Forecast retrieval error: %s",
                      content);

              xmlFree(XMLCHAR_P(content));
            }
          }

          return NULL;
        }
        
        xmlFree(XMLCHAR_P(content));
      } else if (xmlStrEqual(curr->name, CONSTXMLCHAR_P("item"))) {
        /* item child element gets 'special' treatment */
        item_node_process((gpointer *)&forecast, curr);
      } else if (xmlStrEqual(curr->name, CONSTXMLCHAR_P("units"))) {
        // distance
        const char * distance = CONSTCHAR_P(xmlGetProp(curr, XMLCHAR_P("distance")));

        gsize distancelen = ((distance)?strlen(distance):0);

        string_if_different_set(&forecast->units_.distance_, distance, distancelen);

        xmlFree(XMLCHAR_P(distance));

        // pressure
        const char * pressure = CONSTCHAR_P(xmlGetProp(curr, XMLCHAR_P("pressure")));

        gsize pressurelen = ((pressure)?strlen(pressure):0);

        string_if_different_set(&forecast->units_.pressure_, pressure, pressurelen);

        xmlFree(XMLCHAR_P(pressure));

        // speed
        const char * speed = CONSTCHAR_P(xmlGetProp(curr, XMLCHAR_P("speed")));

        gsize speedlen = ((speed)?strlen(speed):0);

        string_if_different_set(&forecast->units_.speed_, speed, speedlen);

        xmlFree(XMLCHAR_P(speed));

        // temperature
        const char * temperature = CONSTCHAR_P(xmlGetProp(curr, XMLCHAR_P("temperature")));

        gsize temperaturelen = ((temperature)?strlen(temperature):0);

        string_if_different_set(&forecast->units_.temperature_, temperature, temperaturelen);

        xmlFree(XMLCHAR_P(temperature));
      } else if (xmlStrEqual(curr->name, CONSTXMLCHAR_P("wind"))) {
        // chill
        const char * chill = CONSTCHAR_P(xmlGetProp(curr, XMLCHAR_P("chill")));

        int_if_different_set(&forecast->windChill_, chill);

        xmlFree(XMLCHAR_P(chill));

        // direction
        const char * direction = CONSTCHAR_P(xmlGetProp(curr, XMLCHAR_P("direction")));

        gint value = (gint)g_ascii_strtoll((direction)?direction:"999", NULL, 10);

        const gchar * dirvalue = WIND_DIRECTION(value);

        string_if_different_set(&forecast->windDirection_, dirvalue, strlen(dirvalue));

        xmlFree(XMLCHAR_P(direction));

        // speed
        const char * speed = CONSTCHAR_P(xmlGetProp(curr, XMLCHAR_P("speed")));

        int_if_different_set(&forecast->windSpeed_, speed);

        xmlFree(XMLCHAR_P(speed));
      } else if (xmlStrEqual(curr->name, CONSTXMLCHAR_P("atmosphere"))) {
        // humidity
        const char * humidity = CONSTCHAR_P(xmlGetProp(curr, XMLCHAR_P("humidity")));

        int_if_different_set(&forecast->humidity_, humidity);

        xmlFree(XMLCHAR_P(humidity));

        // pressure
        const char * pressure = CONSTCHAR_P(xmlGetProp(curr, XMLCHAR_P("pressure")));

        forecast->pressure_ = g_ascii_strtod((pressure)?pressure:"0", NULL);

        xmlFree(XMLCHAR_P(pressure));

        // visibility
        const char * visibility = CONSTCHAR_P(xmlGetProp(curr, XMLCHAR_P("visibility")));

        forecast->visibility_ = g_ascii_strtod((visibility)?visibility:"0", NULL);

        // need to divide by 100
        //forecast->dVisibility_ = forecast->dVisibility_/100;

        xmlFree(XMLCHAR_P(visibility));

        // state
        const char * state = CONSTCHAR_P(xmlGetProp(curr, XMLCHAR_P("rising")));
                            
        forecast->pressureState_ = (PressureState) g_ascii_strtoll((state)?state:"0",
                                                                   NULL,
                                                                   10);

        xmlFree(XMLCHAR_P(state));
      } else if (xmlStrEqual(curr->name, CONSTXMLCHAR_P("astronomy"))) {
        // sunrise
        const char * sunrise = CONSTCHAR_P(xmlGetProp(curr, XMLCHAR_P("sunrise")));

        gsize sunriselen = ((sunrise)?strlen(sunrise):0);

        string_if_different_set(&forecast->sunrise_, sunrise, sunriselen);

        xmlFree(XMLCHAR_P(sunrise));

        // sunset
        const char * sunset = CONSTCHAR_P(xmlGetProp(curr, XMLCHAR_P("sunset")));

        gsize sunsetlen = ((sunset)?strlen(sunset):0);

        string_if_different_set(&forecast->sunset_, sunset, sunsetlen);

        xmlFree(XMLCHAR_P(sunset));
      }
          
    }

  }

  return forecast;
}

/**
 * Evaluates an XPath expression on the passed-in context.
 *
 * @param ctxt Pointer to the context.
 * @param expr The XPath expression to evaluate
 *
 * @return xmlNodeSetPtr pointing to the resulting node set, must be
 *         freed by the caller.
 */
static xmlNodeSetPtr
xpath_evaluate(xmlXPathContextPtr ctxt, const char * expr)
{
  xmlXPathObjectPtr pObject = xmlXPathEval(CONSTXMLCHAR_P(expr), ctxt);

  if (!pObject || !pObject->nodesetval) {
    return NULL;
  }

  xmlNodeSetPtr nodeset = pObject->nodesetval;

  xmlXPathFreeNodeSetList(pObject);

  return nodeset;
}

/**
 * Parses the location response and fills in the supplied list with entries
 * (if any).
 *
 * @param response Pointer to the response received.
 * @param list     Pointer to the pointer to the list to populate.
 *
 * @return 0 on success, -1 on failure
 *
 * @note If the list pointer is NULL nothing is done and failure is
 *       returned. Otherwise, the appropriate pointer is set based on the name
 *       of the XML element: 'Result' for GList (list)
 */
static gint
location_response_parse(gpointer response, GList ** list)
{
  xmlDocPtr pDoc = xmlReadMemory(CONSTCHAR_P(response),
                                 strlen(response),
                                 "",
                                 NULL,
                                 0);

  if (!pDoc) {
    // failed
    LXW_LOG(LXW_ERROR,
            "yahooutil::location_response_parse(): Failed to parse response %s",
            CONSTCHAR_P(response));

    return -1;
  }

  xmlNodePtr root = xmlDocGetRootElement(pDoc);
  
  // the second part of the if can be broken out
  if (!root || !xmlStrEqual(root->name, CONSTXMLCHAR_P("query"))) {
    // failed
    LXW_LOG(LXW_ERROR,
            "yahooutil::location_response_parse(): Failed to retrieve root %s",
            CONSTCHAR_P(response));

    xmlFreeDoc(pDoc);

    return -1;
  }

  // use xpath to find /query/results/Result
  xmlXPathInit();

  xmlXPathContextPtr pCtxt = xmlXPathNewContext(pDoc);

  const char * expr = "/query/results/Result";

  // have some results...
  xmlNodeSetPtr nodeset = xpath_evaluate(pCtxt, expr);

  if (!nodeset) {
    xmlXPathFreeContext(pCtxt);

    xmlFreeDoc(pDoc);

    return -1;
  }

  int nodecnt   = 0;
  int nodetotal = nodeset->nodeNr;

  gint retval = 0;

  for (; nodecnt < nodetotal; ++nodecnt) {
    if (nodeset->nodeTab) {
      xmlNodePtr node = nodeset->nodeTab[nodecnt];

      if (node && node->type == XML_ELEMENT_NODE) {
        if (xmlStrEqual(node->name, CONSTXMLCHAR_P("Result"))) {
          gpointer entry = result_node_process(node);
                  
          if (entry && list) {
            *list = g_list_prepend(*list, entry);
          }
        }

      } // end if element

    } // end if nodeTab

  } // end for noteTab size

  xmlXPathFreeNodeSet(nodeset);

  xmlXPathFreeContext(pCtxt);

  xmlFreeDoc(pDoc);

  return retval;
}

/**
 * Parses the response and fills in the supplied forecast pointer.
 *
 * @param response Pointer to the response received.
 * @param forecast Pointer to the pointer to the forecast to retrieve.
 *
 * @return 0 on success, -1 on failure
 *
 * @note If the the forecast pointer is NULL, nothing is done and failure is
 *       returned. Otherwise, the appropriate pointer is set based on the name
 *       of the XML element: 'channel' for Forecast (forecast)
 */
//static gint
gint
forecast_response_parse(gpointer response, gpointer * forecast)
{
  xmlDocPtr pDoc = xmlReadMemory(CONSTCHAR_P(response),
                                 strlen(response),
                                 "",
                                 NULL,
                                 0);

  if (!pDoc) {
    // failed
    LXW_LOG(LXW_ERROR,
            "yahooutil::forecast_response_parse(): Failed to parse response %s",
            CONSTCHAR_P(response));

    return -1;
  }

  xmlNodePtr root = xmlDocGetRootElement(pDoc);
  
  // the second part of the if can be broken out
  if (!root || !xmlStrEqual(root->name, CONSTXMLCHAR_P("query"))) {
    // failed
    LXW_LOG(LXW_ERROR,
            "yahooutil::forecast_response_parse(): Failed to retrieve root %s",
            CONSTCHAR_P(response));

    xmlFreeDoc(pDoc);

    return -1;
  }

  // use xpath to find /query/results/Result
  xmlXPathInit();

  xmlXPathContextPtr pCtxt = xmlXPathNewContext(pDoc);

  const char * expr = "/query/results/channel";

  // have some results...
  xmlNodeSetPtr nodeset = xpath_evaluate(pCtxt, expr);

  if (!nodeset) {
    // error, or no results found -- failed
    xmlXPathFreeContext(pCtxt);

    xmlFreeDoc(pDoc);

    return -1;
  }

  int nodecnt   = 0;
  int nodetotal = nodeset->nodeNr;

  gint retval = 0;

  for (; nodecnt < nodetotal; ++nodecnt) {
    if (nodeset->nodeTab) {
      xmlNodePtr node = nodeset->nodeTab[nodecnt];

      if (node && node->type == XML_ELEMENT_NODE) {
        if (xmlStrEqual(node->name, CONSTXMLCHAR_P("channel"))) {
          ForecastInfo * entry = NULL;
                  
          gboolean newed = FALSE;

          /* Check if forecast is allocated, if not, 
           * allocate and populate 
           */
          if (forecast) {
            if (*forecast) {
              entry = (ForecastInfo *)*forecast;
            } else {
              entry = (ForecastInfo *)g_try_new0(ForecastInfo, 1);

              newed = TRUE;
            }
                  
            if (!entry) {
              retval = -1;
            } else {
              *forecast = channel_node_process(node, entry);
                          
              if (!*forecast) {
                /* Failed, forecast is freed by caller */
                              
                /* Unless it was just newed... */
                if (newed) {
                  g_free(entry);
                }
                          
                retval = -1;
              }
            }

          } // end if forecast

        } // end else if 'channel'

      } // end if element

    } // end if nodeTab

  } // end for noteTab size

  xmlXPathFreeNodeSet(nodeset);

  xmlXPathFreeContext(pCtxt);

  xmlFreeDoc(pDoc);

  return retval;
}

/**
 * Initializes the internals: XML 
 *
 */
void
yahooutil_init(void)
{
  if (!g_initialized) {
    xmlInitParser();

    g_initialized = 1;
  }
}

/**
 * Cleans up the internals: XML 
 *
 */
void
yahooutil_cleanup(void)
{
  if (g_initialized) {
    xmlCleanupParser();

    g_initialized = 0;
  }
}

/**
 * Retrieves the details for the specified location
 *
 * @param location The string containing the name/code of the location
 *
 * @return A pointer to a list of LocationInfo entries, possibly empty, 
 *         if no details were found. Caller is responsible for freeing the list.
 */
GList *
yahooutil_location_find(const gchar * location)
{
  gint rc = 0;
  gint datalen = 0;

  GList * list = NULL;

  gchar * locationascii = locale_to_ascii(location);

  gsize len = WOEID_QUERY_LEN + strlen(locationascii);

  gchar * querybuf = g_malloc0(len);

  gint ret = woeid_query_gen(querybuf, locationascii);

  g_free(locationascii);

  LXW_LOG(LXW_DEBUG, "yahooutil::yahooutil_find_location(%s): query[%d]: %s",
          location, ret, querybuf);

  gpointer response = httputil_url_get(querybuf, &rc, &datalen);

  if (!response || rc != HTTP_STATUS_OK) {
    LXW_LOG(LXW_ERROR, "yahooutil::yahooutil_find_location(%s): Failed with error code %d",
            location, rc);
  } else {
    LXW_LOG(LXW_DEBUG, "yahooutil::yahooutil_find_location(%s): Response code: %d, size: %d",
            location, rc, datalen);

    LXW_LOG(LXW_VERBOSE, "yahooutil::getLocation(%s): Contents: %s", 
            location, (const char *)response);

    ret = location_response_parse(response, &list);
      
    LXW_LOG(LXW_DEBUG, "yahooutil::getLocation(%s): Response parsing returned %d",
            location, ret);

    if (ret) {
      // failure
      g_list_free_full(list, location_free);
    }
  }

  g_free(querybuf);
  g_free(response);

  return list;
}

/**
 * Retrieves the forecast for the specified location WOEID
 *
 * @param woeid    The string containing the WOEID of the location
 * @param units    The character containing the units for the forecast (c|f)
 * @param forecast The pointer to the forecast to be filled. If set to NULL,
 *                  a new one will be allocated.
 *
 */
void
yahooutil_forecast_get(const gchar * woeid, const gchar units, gpointer * forecast)
{
  gint rc = 0;
  gint datalen = 0;

  gsize len = FORECAST_QUERY_LEN + strlen(woeid);

  gchar * querybuf = g_malloc0(len);

  gint ret = forecast_query_gen(querybuf, woeid, units);

  LXW_LOG(LXW_DEBUG, "yahooutil::yahooutil_forecast_get(%s): query[%d]: %s",
          woeid, ret, querybuf);

  gpointer response = httputil_url_get(querybuf, &rc, &datalen);

  if (!response || rc != HTTP_STATUS_OK) {
    LXW_LOG(LXW_ERROR, "yahooutil::yahooutil_forecast_get(%s): Failed with error code %d",
            woeid, rc);
  } else {
    LXW_LOG(LXW_DEBUG, "yahooutil::yahooutil_forecast_get(%s): Response code: %d, size: %d",
            woeid, rc, datalen);
    
    LXW_LOG(LXW_VERBOSE, "yahooutil::yahooutil_forecast_get(%s): Contents: %s",
            woeid, (const char *)response);
    
    ret = forecast_response_parse(response, forecast);
    
    LXW_LOG(LXW_DEBUG,
            "yahooutil::yahooutil_forecast_get(%s): Response parsing returned %d",
            woeid, ret);

    if (ret) {
      forecast_free(*forecast);

      *forecast = NULL;
    }

  }

  g_free(querybuf);
  g_free(response);
}
