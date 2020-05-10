/*
 * Copyright © 2020 Dmitry Yudin. All rights reserved.
 * Licensed under the Apache License, Version 2.0
 */

#include "prof.h"

#include <stdio.h>
#include <thread>
#include <chrono>

static void encode_frame(int n)
{
    FPSPROF_SCOPED_FRAME("encode_frame")
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    if (n & 0x1) {
        // .5 calls per frame
        FPSPROF_SCOPED("foo_odd")
        for (unsigned i = 0; i < 5; i++) {
            // loop label, will appear multiple times in a detailed report
            FPSPROF_SCOPED("foo_odd_inner")
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    } else {
        FPSPROF_SCOPED("foo_even")
        for (unsigned i = 0; i < 2; i++) {
            FPSPROF_SCOPED("foo_even_inner")
            std::this_thread::sleep_for(std::chrono::milliseconds(15));
        }
    }
    // 1 call/frame
    FPSPROF_START(foo_all, "foo_all")
    for (unsigned i = 0; i < 3; i++) {
        FPSPROF_SCOPED("foo_all_inner")
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    FPSPROF_STOP(foo_all)
}

int main(int argc, char *argv[])
{
    FPSPROF_SERIALIZE_STREAM(stdout)
    FPSPROF_REPORT_STREAM(stderr)

    for(unsigned i = 0; i < 10; i++) {
        encode_frame(i);
    }

    return 0;
}
