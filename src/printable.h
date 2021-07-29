/*
 * Copyright © 2021 Dmitry Yudin. All rights reserved.
 * Licensed under the Apache License, Version 2.0
 */

#pragma once

#include <stdint.h>
#include <string>

namespace fpsprof {

class Printable {
public:
    virtual ~Printable() = 0;
    static void setNameColumnLen(unsigned nameLen, unsigned stack_level, unsigned num_recursions);
    static void setFrameCounters(uint64_t realtime_used, unsigned count);


    virtual std::string doPrint(uint64_t realtime_used, unsigned count) = 0;


protected:
    std::string formatName(const char *name, unsigned stack_level, unsigned num_recursions);

    static unsigned _nameColumnLen;
    static uint64_t _frameRealTimeUsed;
    static unsigned _frameCount;
};

}
