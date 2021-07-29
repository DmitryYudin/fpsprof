/*
 * Copyright © 2021 Dmitry Yudin. All rights reserved.
 * Licensed under the Apache License, Version 2.0
 */

#define NOMINMAX

#include "printable.h"

#include <math.h>
#include <algorithm>
#include <string>

#define LOG10(x) (log(x)/log(10))

namespace fpsprof {

unsigned Printable::_nameColumnWidth = 60;
uint64_t Printable::_frameRealTimeUsed = 0;
unsigned Printable::_frameCount = 0;

Printable::~Printable()
{
}

#define FILL_LEN(stack_level) std::min(128u, 2 * (1 + stack_level))

void Printable::setNameColumnWidth(unsigned nameLen, unsigned stack_level, unsigned num_recursions)
{
    _nameColumnWidth = FILL_LEN(stack_level) + nameLen  + (num_recursions ? 1 + (unsigned)LOG10(num_recursions) + 3 : 0);
}
void Printable::setFrameCounters(uint64_t realtime_used, unsigned count)
{
    _frameRealTimeUsed = realtime_used;
    _frameCount = count;
}

std::string Printable::formatName(const char *name, unsigned stack_level, unsigned num_recursions)
{
    std::string res(FILL_LEN(stack_level), ' ');
    res += name;
    if (num_recursions) {
        res += std::string("[+") + std::to_string(num_recursions) + "]";
    }
    if(_nameColumnWidth > res.length()) {
        res.append(_nameColumnWidth - res.length(), ' ');
    }
    return res;
}

std::string Printable::formatData(
    const char *name,
    int stack_level,
    unsigned num_recursions,
    uint64_t realtime_used,
    uint64_t children_realtime_used,
    unsigned count,
    uint64_t cpu_used
)
{
    const char* NA = "-";

    char totInclP[32], totExclP[32], totInclFPS[32];
    if (_frameRealTimeUsed > 0) {
        double inclP = 100.f * realtime_used / _frameRealTimeUsed;
        double chldP = 100.f * children_realtime_used / _frameRealTimeUsed;
        double exclP = inclP - chldP;
        double inclFPS = 1.f / (1e-9f * realtime_used / _frameCount);
        sprintf(totInclP, "%6.2f", inclP);
        sprintf(totExclP, "%6.2f", exclP);
        sprintf(totInclFPS, "%10.1f", inclFPS);
    } else {
        sprintf(totInclP, "%6s", NA);
        sprintf(totExclP, "%6s", NA);
        sprintf(totInclFPS, "%10s", NA);
    }

    char callsPerFrame[32];
    if(_frameCount > 0) {
        double n = ((double)count) / _frameCount;
        sprintf(callsPerFrame, "%7.2f", n);
    } else {
        sprintf(callsPerFrame, "%7s", NA);
    }
    char cpuP[32];
    if (realtime_used > 0) {
        double inclP = 100.f* cpu_used / realtime_used;
        sprintf(cpuP, "%6.1f", inclP);
    } else {
        sprintf(cpuP, "%6.1f", 0.);
    }

    std::string res = formatName(name, stack_level, num_recursions);
    res.append(" ").append(totInclP);
    res.append(" ").append(totExclP);
    res.append(" ").append(totInclFPS);
    res.append(" ").append(callsPerFrame);
    res.append(" ").append(cpuP);

    return res;
}

}
