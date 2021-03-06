/*
 * This program is part of the Clear Linux Project
 *
 * Copyright 2018 Intel Corporation
 *
 * This program is free software; you can redistribute it and/or modify it under
 * the terms and conditions of the GNU Lesser General Public License, as
 * published by the Free Software Foundation; either version 2.1 of the License,
 * or (at your option) any later version.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT ANY
 * WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR
 * A PARTICULAR PURPOSE. See the GNU Lesser General Public License for more
 * details.
 */

#include <stdio.h>
#include <stdlib.h>
#include <getopt.h>
#include <unistd.h>
#include <stdbool.h>
#include <sys/stat.h>

#include "telemetry.h"
#include "log.h"
#include "spool.h"
#include "configuration.h"
#include "telempostdaemon.h"

/*
 *  Using a function pointer for unit testing to isolate the call to actual post function.
 *  The call to post the record to the server is stubbed out in the unit tests
 *  using pointer to a fake function.
 */

bool (*post_record_ptr)(char *[], char *, char *) = post_record_http;

void print_usage(char *prog)
{
        printf("%s: Usage\n", prog);
        printf("  -f,  --config_file    Configuration file. This overides the other parameters\n");
        printf("  -h,  --help           Display this help message\n");
        printf("  -V,  --version        Print the program version\n");
}

int main(int argc, char **argv)
{

        int c;
        int opt_index = 0;
        TelemPostDaemon daemon;

        struct option opts[] = {
                { "config_file", 1, NULL, 'f' },
                { "help", 0, NULL, 'h' },
                { "version", 0, NULL, 'V' },
                { NULL, 0, NULL, 0 }
        };

        while ((c = getopt_long(argc, argv, "f:hV", opts, &opt_index)) != -1) {
                switch (c) {
                        case 'f':
                                if (tm_set_config_file(optarg) != 0) {
                                    telem_log(LOG_ERR, "Configuration file"
                                                  " path not valid\n");
                                    exit(EXIT_FAILURE);
                                }
                                break;
                        case 'V':
                                printf(PACKAGE_VERSION "\n");
                                exit(EXIT_SUCCESS);
                        case 'h':
                                print_usage(argv[0]);
                                exit(EXIT_SUCCESS);
                        case '?':
                                print_usage(argv[0]);
                                exit(EXIT_FAILURE);
                }
        }

        initialize_post_daemon(&daemon);

        daemon.current_spool_size = get_spool_dir_size();

        /* When path activated this will process
         * the activating message or previously
         * spooled data */
        staging_records_loop(&daemon);

        /* This function is blocking, it will
        * block until a signal is received */
        run_daemon(&daemon);

        close_daemon(&daemon);

        return 0;
}
