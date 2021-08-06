/*
 * Copyright © 2020 Dmitry Yudin. All rights reserved.
 * Licensed under the Apache License, Version 2.0
 */

#include "../src/reporter.h"

#include <stdio.h>
#include <getopt.h>

static void usage(void)
{
    printf(
"Convert raw profiler report to human readable form.\n"
"\n"
"Usage:\n"
"  fpsprof <options> profiler.log\n"
"\n"
"Options:\n"
"  -h, --help     Print this help.\n"
"\n"
    );
}

static bool check_file_exist(const char *filename)
{
    FILE *fp = fopen(filename, "r");
    if(fp) {
        fclose(fp);
    }
    return fp != NULL;

}
#ifdef NDEBUG
#define TRACE_ERR(cond, fmt, ...) if (cond) { fprintf(stderr, "error:" fmt "\n", ##__VA_ARGS__); return 1; }
#else
#define TRACE_ERR(cond, fmt, ...) if (cond) { fprintf(stderr, __FILE__ "(%u): error:" fmt "\n", __LINE__, ##__VA_ARGS__); return 1; }
#endif

int main(int argc, char *argv[])
{
    if (argc <= 1) {
        usage();
        return 1;
    }
    static const struct option long_options[] = {
        { "help",   no_argument,        0, 'h' },
        { "input",  required_argument,  0, 'i' },
        { "self",  required_argument,  0, 's' },
        { "children",  required_argument,  0, 'c' },
        //{ "report", required_argument,  0, 'r' },
        //{ "stack",  required_argument,  0, 's' },
    };
    const char* filename = NULL;
    double self_nsec = -1, children_nsec = -1;
    int ch;
    while ((ch = getopt_long(argc, argv, "hi:s:c:", long_options, 0)) != EOF) {
        switch (ch) {
        case 'h':
            return usage(), 0;
        case 'i':
            filename = optarg;
            break;
        case 's':
            if (sscanf(optarg, "%lf", &self_nsec) != 1) {
                TRACE_ERR(1, "invalid argument for '-s' option: %s", optarg)
            }
            break;
        case 'c':
            if (sscanf(optarg, "%lf", &children_nsec) != 1) {
                TRACE_ERR(1, "invalid argument for '-c' option: %s", optarg)
            }
            break;
        //case 'r':
        //    if (sscanf(optarg, "%u", &reportFlags) != 1) {
        //        TRACE_ERR(1, "invalid argument for '-r' option: %s", optarg)
        //    }
        //    break;
        //case 's':
        //    if (sscanf(optarg, "%u", &stackLevelMax) != 1) {
        //        TRACE_ERR(1, "invalid argument for '-s' option: %s", optarg)
        //    }
        //    break;

        default:
            usage();
            return 1;
        }
    }
    if (optind < argc && filename == NULL) {
        filename = argv[optind++];
    }
    if (argc != optind) {
        return usage(), 1;
    }

    TRACE_ERR(filename == NULL, "input file name required")
    TRACE_ERR(!check_file_exist(filename), "input file does not exist")

    fpsprof::Reporter reporter;
    TRACE_ERR(!reporter.Deserialize(filename), "failed to parse profiler log: %s", filename)

    std::string report = reporter.Report(self_nsec, children_nsec);
    printf("%s\n", report.c_str());


    return 0;
}
