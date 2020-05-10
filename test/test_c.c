/*
 * Copyright © 2020 Dmitry Yudin. All rights reserved.
 * Licensed under the Apache License, Version 2.0
 */

#include "prof.h"
#include <stdio.h>

int main(int argc, char *argv[])
{
    FPSPROF_SERIALIZE_STREAM(stdout)
    FPSPROF_REPORT_STREAM(stderr)

    FPSPROF_START_FRAME(outer, "outer")
    FPSPROF_START(inner1, "inner1")
    FPSPROF_START(inner2, "inner2")

    FPSPROF_STOP(inner2)
    FPSPROF_STOP(inner1)
    FPSPROF_STOP(outer)

    return 0;
}
