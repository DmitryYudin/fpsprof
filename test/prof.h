/*
 * Copyright © 2020 Dmitry Yudin. All rights reserved.
 * Licensed under the Apache License, Version 2.0
 */

#pragma once

// Integration example

#if !FPSPROF_DISABLE
    #include <fpsprof/fpsprof.h>
#else
    #define FPSPROF_SERIALIZE_STREAM(stream)
    #define FPSPROF_SERIALIZE_FILE(filename)
    #define FPSPROF_REPORT_STREAM(stream)
    #define FPSPROF_REPORT_FILE(filename)

    #define FPSPROF_START_FRAME(handle, name)
    #define FPSPROF_START(handle, name)
    #define FPSPROF_STOP(handle)

    #define FPSPROF_SCOPED_FRAME(name)
    #define FPSPROF_SCOPED(name)
#endif
