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

/* Provides logging utilities */

#include "logutil.h"

#include <syslog.h>
#include <stdarg.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>

static int g_initialized = 0;
static LXWEATHER_LOGLEVEL g_level = LXW_NONE;
static int g_fd = -1; /* -1 is syslog, 0 is std{out|err} */

/**
 * Initializes the logging subsystem
 *
 * @param path Path to a file to log to (can be NULL for std{out|err},
 *             or 'syslog' for syslog)
 */
void 
initializeLogUtil(const char * path)
{
#ifndef DEBUG
  return;
#endif

  if (g_initialized)
    {
      return;
    }

  if (path)
    {
      if (strncmp(path, "syslog", 6) == 0)
        {
          /* syslog */
          openlog("LXWeather", LOG_NDELAY | LOG_PID, LOG_USER);
        }
      else if (strncmp(path, "std", 3) == 0)
        {
          /* std{out|err} */
          g_fd = 0;
        }
      else
        {
          /* Attempt to open this file for writing */
          g_fd = open(path, 
                      O_WRONLY | O_CREAT | O_CLOEXEC | O_TRUNC, 
                      S_IRWXU | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH);

          if (g_fd < 0)
            {
              /* Failed */
              fprintf(stderr, "LXWeather::initalizeLogUtil(): Failed to open %s: %s\n",
                      path, strerror(errno));

              /* Initialized flag is 0, so no logging will happen */
              return;
            }

        }

    }
  else
    {
      /* stdout/err */
      g_fd = 0;
    }

  g_initialized = 1;
}

/**
 * Cleans up the logging subsystem
 *
 */
void 
cleanupLogUtil()
{
#ifndef DEBUG
  return;
#endif

  if (g_initialized)
    {
      switch (g_fd)
        {
        case -1:
          closelog();
          break;

        case 0:
          /* std{out|err} */
          break;

        default:
          /* Close the file */
          close(g_fd); /* Don't care about errors */
        }

      g_initialized = 0;
    }

}

/**
 * Logs the message using the specified level.
 *
 * @param level The level to log at
 * @param msg Message to log
 */
void 
logUtil(LXWEATHER_LOGLEVEL level, const char * msg, ...)
{
#ifndef DEBUG
  return;
#endif

  if (g_initialized && (level <= g_level) && (g_level > LXW_NONE))
    {
      va_list ap;
            
      va_start(ap, msg);

      if (g_fd == -1)
        {
          vsyslog(((level == LXW_ERROR) ? LOG_ERR : LOG_NOTICE), msg, ap);
        }
      else
        {
          char buf[1024];
          
          pid_t pid = getpid();

          /* This is not portable, due to pid_t... */
          size_t bufsz = snprintf(buf, sizeof(buf), "LXWeather [%ld] [%5s] ", 
                                  (long)pid, 
                                  (level == LXW_ERROR) ? "ERROR" : "DEBUG");
          
          bufsz += vsnprintf(buf + bufsz, sizeof(buf) - bufsz, msg, ap);
          
          bufsz += snprintf(buf + bufsz, sizeof(buf) - bufsz, "\n");

          if (g_fd == 0)
            {
              /* std{out|err} */

              if (level == LXW_ERROR)
                {
                  fprintf(stderr, "%s", buf);
                }
              else
                {
                  fprintf(stdout, "%s", buf);
                }
            }
          else
            {
              /* write to file */
              (void) write(g_fd, buf, bufsz); /* to prevent compile warning */
            }
        }
           
      va_end(ap);

    }

}

/**
 * Sets the maximum allowed log level
 *
 * @param level The level to use for all messages to follow.
 *
 * @return Previous value of the maximum log level.
 */
LXWEATHER_LOGLEVEL 
setMaxLogLevel(LXWEATHER_LOGLEVEL level)
{
#ifndef DEBUG
  return g_level;
#endif

  LXWEATHER_LOGLEVEL previous = g_level;

  if (g_initialized && level <= LXW_ALL)
    {
      g_level = level;
    }

  return previous;
}
