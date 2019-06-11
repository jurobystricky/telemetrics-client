/*
 * This program is part of the Clear Linux Project
 *
 * Copyright 2019 Intel Corporation
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

#define _GNU_SOURCE

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <getopt.h>

#include "config.h"
#include "gettext.h"
#include "common.h"
#include "journal.h"

#define _(String) gettext (String)

static void print_usage(void)
{
        printf(_("Usage:\n"));
        printf(_("  telem_journal [-Vi] [-r <record_id>] [-e <event_id>] [-c <classification>] [-b <boot_id>]\n\n"));
        printf(_("Where:\n"));
        printf(_("  -r,  --record_id        Print record with specific record_id\n"));
        printf(_("  -e,  --event_id         Print records with specific event_id\n"));
        printf(_("  -c,  --classification   Print records with specific classification\n"));
        printf(_("  -b,  --boot_id          Print records with specific boot_id\n"));
        printf(_("  -i,  --include_record   Include record content if available.\n"));
        printf(_("                          Content only available when telemetry is configured\n"));
        printf(_("                          with \"record_retention_enabled=true\"\n"));
        printf(_("  -V,  --verbose          Verbose output\n"));
        printf(_("  -h,  --help             Display this help message\n"));
}

int main(int argc, char **argv)
{

        int rc = EXIT_SUCCESS;
        int count = 0;
        bool verbose_output = false;
        bool record = false;
        char *boot_id = NULL;
        char *record_id = NULL;
        char *event_id = NULL;
        char *classification = NULL;
        struct TelemJournal *telem_journal = NULL;

        /** opts */
        int c;
        int opt_index = 0;
        struct option opts[] = {
                { "record_id", 1, NULL, 'r' },
                { "event_id", 1, NULL, 'e' },
                { "classification", 1, NULL, 'c' },
                { "boot_id", 1, NULL, 'b' },
                { "verbose", 0, NULL, 'V' },
                { "include_record", 0, NULL, 'i' },
                { "help", 0, NULL, 'h' },
                { NULL, 0, NULL, 0 }
        };

        setlocale(LC_ALL, "");
        bindtextdomain(PACKAGE, LOCALEDIR);
        textdomain(PACKAGE);

        while ((c = getopt_long(argc, argv, "r:e:c:b:Vih", opts, &opt_index)) != -1) {
                switch (c) {
                        case 'r':
                                record_id = optarg;
                                break;
                        case 'e':
                                event_id = optarg;
                                break;
                        case 'c':
                                classification = optarg;
                                break;
                        case 'b':
                                boot_id = optarg;
                                break;
                        case 'V':
                                verbose_output = true;
                                break;
                        case 'i':
                                record = true;
                                break;
                        case 'h':
                                print_usage();
                                exit(EXIT_SUCCESS);
                        default:
                                print_usage();
                                exit(EXIT_FAILURE);
                }
        }

        if ((telem_journal = open_journal(JOURNAL_PATH))) {
                if (verbose_output) {
                        fprintf(stdout, "%-30s %-27s %-32s %-32s %-36s\n", _("Classification"), _("Time stamp"),
                                "Record ID", "Event ID", "Boot ID");
                }
                count = print_journal(telem_journal, classification, record_id, event_id, boot_id, record);
                if (verbose_output) {
                        fprintf(stdout, _("Total records: %d\n"), count);
                }
                close_journal(telem_journal);
        } else {
                fprintf(stderr, _("Unable to open journal\n"));
                rc = EXIT_FAILURE;
        }

        return rc;
}
