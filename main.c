/*
 * nodewatcher-agent - remote monitoring daemon
 *
 * Copyright (C) 2014 Jernej Kos <jernej@kos.mx>
 *
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of the GNU Affero General Public License as published by the
 * Free Software Foundation, either version 3 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU Affero General Public License
 * for more details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#include <unistd.h>
#include <libubox/uloop.h>
#include <libubus.h>
#include <uci.h>
#include <signal.h>
#include <syslog.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <stdlib.h>

#include <nodewatcher-agent/module.h>
#include <nodewatcher-agent/scheduler.h>
#include <nodewatcher-agent/output.h>
#include <nodewatcher-agent/utils.h>

/* Global ubus connection context */
static struct ubus_context *ubus;
/* Global UCI context */
static struct uci_context *uci;

/**
 * Nodewatcher agent entry point.
 */
int main(int argc, char **argv)
{
  struct stat s;
  const char *ubus_socket = NULL;
  int log_option = 0;
  int c;

  while ((c = getopt(argc, argv, "fs:")) != -1) {
    switch (c) {
      case 's': ubus_socket = optarg; break;
      case 'f': log_option |= LOG_PERROR; break;
      default: break;
    }
  }

  /* Open the syslog facility */
  openlog("nw-agent", log_option, LOG_DAEMON);

  /* Create directory for temporary run files */
  if (stat("/var/run/nodewatcher-agent", &s))
    mkdir("/var/run/nodewatcher-agent", 0700);

  umask(0077);

  /* Setup signal handlers */
  signal(SIGPIPE, SIG_IGN);
  /* TODO: Handle SIGHUP to reload? */

  /* Seed random generator */
  unsigned int seed;
  int rc = nw_read_random_bytes(&seed, sizeof(seed));
  if (rc < 0) {
    fprintf(stderr, "ERROR: Failed to seed random generator!\n");
    return -1;
  }

  srandom(seed);

  /* Initialize event loop */
  uloop_init();

  /* Attempt to establish connection to ubus daemon */
  for (;;) {
    ubus = ubus_connect(ubus_socket);
    if (!ubus) {
      syslog(LOG_WARNING, "Failed to connect to ubus!");
      sleep(10);
      continue;
    }

    break;
  }

  ubus_add_uloop(ubus);

  /* Initialize UCI context */
  uci = uci_alloc_context();
  if (!uci) {
    syslog(LOG_ERR, "Failed to initialize UCI!");
    return -1;
  }

  /* Discover and initialize modules */
  if (nw_module_init(ubus, uci) != 0) {
    syslog(LOG_ERR, "Unable to initialize modules!");
    return -1;
  }

  /* Initialize the scheduler */
  if (nw_scheduler_init() != 0) {
    syslog(LOG_ERR, "Unable to initialize scheduler!");
    return -1;
  }

  /* Initialize the output exporter */
  if (nw_output_init(uci) != 0) {
    syslog(LOG_ERR, "Unable to initialize output exporter!");
    return -1;
  }

  /* Enter the event loop */
  uloop_run();
  ubus_free(ubus);
  uci_free_context(uci);
  uloop_done();

  return 0;
}
