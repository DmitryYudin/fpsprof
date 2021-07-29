/*
 * Copyright � 2021 Dmitry Yudin. All rights reserved.
 * Licensed under the Apache License, Version 2.0
 */

#pragma once

#include <stdint.h>
#include <string>

namespace fpsprof {

class Printable {
public:
    virtual ~Printable() = 0;
    static void setNameColumnWidth(unsigned nameLen, unsigned stack_level, unsigned num_recursions);
    static void setFrameCounters(uint64_t realtime_used, unsigned count);

    virtual std::string doPrint() const = 0;

protected:
    static std::string formatData(const char *name, int stack_level, unsigned num_recursions,
        uint64_t realtime_used,
        uint64_t children_realtime_used,
        unsigned count,
        uint64_t cpu_used
    );

private:
    static std::string formatName(const char *name, unsigned stack_level, unsigned num_recursions);

    static unsigned _nameColumnWidth;
    static uint64_t _frameRealTimeUsed;
    static unsigned _frameCount;
};

}
