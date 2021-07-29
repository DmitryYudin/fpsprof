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

unsigned Printable::_nameColumnLen = 60;
uint64_t Printable::_frameRealTimeUsed = 0;
unsigned Printable::_frameCount = 0;

Printable::~Printable()
{
}

#define FILL_LEN(stack_level) std::min(128u, 2 * (1 + stack_level))

void Printable::setNameColumnLen(unsigned nameLen, unsigned stack_level, unsigned num_recursions)
{
    _nameColumnLen = FILL_LEN(stack_level) + nameLen  + (num_recursions ? 1 + (unsigned)LOG10(num_recursions) + 3 : 0);
}

std::string Printable::formatName(const char *name, unsigned stack_level, unsigned num_recursions)
{
    std::string res(FILL_LEN(stack_level), ' ');
    res += name;
    if (num_recursions) {
        res += std::string("[+") + std::to_string(num_recursions) + "]";
    }
    return res;
}

}
