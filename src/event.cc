/*
 * Copyright © 2020 Dmitry Yudin. All rights reserved.
 * Licensed under the Apache License, Version 2.0
 */

#define NOMINMAX

#include "event.h"
#include "profpoint.h"

#include <string.h>
#include <ostream>
#include <iomanip>
#include <map>
#include <stack>
#include <vector>
#include <set>
#include <algorithm>
#include <assert.h>

namespace fpsprof {

std::ostream& operator<<(std::ostream& os, const Event& event)
{
    os << std::setw(1) << event.frame_flag() << " "
        << std::setw(1) << event.measure_process_time() << " "
        << std::setw(2) << event.stack_level() << " "
        << std::setw(20) << std::left << event.name() << std::right << " "
        << std::setw(14) << event.start_nsec() << " "
        << std::setw(14) << event.stop_nsec() << " "
        << std::setw(12) << event.cpu_used() << " "
        << std::setw(0)
        ;
    return os;
}

#define READ_NEXT_TOKEN(s, err_action) s = strtok(NULL, " "); if (!s) { err_action; };
#define READ_NUMERIC(s, val, err_action, strtoNum) \
    READ_NEXT_TOKEN(s, err_action) \
    { char* end; val = strtoNum(s, &end, 10); if (*end != '\0') { err_action; } }
#define READ_LONG(s, val, err_action) READ_NUMERIC(s, val, err_action, strtol)
#define READ_LONGLONG(s, val, err_action) READ_NUMERIC(s, val, err_action, strtoll)

char* Event::desirialize(char *s)
{
    std::string name_str;
    READ_LONG(s, _frame_flag, return NULL)
    READ_LONG(s, _measure_process_time, return NULL)
    READ_LONG(s, _stack_level, return NULL)
    READ_NEXT_TOKEN(s, return NULL) name_str = s;
    READ_LONGLONG(s, _start_nsec, return NULL)
    READ_LONGLONG(s, _stop_nsec, return NULL)
    READ_LONGLONG(s, _cpu_used, return NULL)

    static std::map<std::string, const char*> cache;
    auto it = cache.find(name_str);
    if (it != cache.end()) {
        _name = it->second;
    } else {
        if (cache.size() == 0) { // sanitary
            atexit([]() { for (auto& x : cache) { delete[] x.second; }; cache.clear(); });
        }
        char* val = new char[name_str.size() + 1];
        strcpy(val, name_str.c_str());
        cache[name_str] = val;
        _name = val;
    }
    return s;
}

}
