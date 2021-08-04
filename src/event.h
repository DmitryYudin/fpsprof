/*
 * Copyright © 2020 Dmitry Yudin. All rights reserved.
 * Licensed under the Apache License, Version 2.0
 */

#pragma once

#include "profpoint.h"

#include <stdint.h>

namespace fpsprof {

struct ProfPoint;

// Some common stuff we always want to access + serialize/deserialize
class Event {
    friend struct ThreadMap; // desirialize
public:
    explicit Event(const ProfPoint& pp)
        : _name(pp.name())
        , _stack_level(pp.stack_level())
        , _frame_flag(pp.frame_flag())
        , _measure_process_time(pp.measure_process_time())
        , _start_nsec(pp.realtime_start())
        , _stop_nsec(pp.realtime_stop())
        , _cpu_used(pp.cputime_delta()) {
    }
    Event()
        : _name(NULL) { // this is for deserialization only, since we to not want to use exceptions
    }
    const char* name() const { return _name; }
    int stack_level() const { return _stack_level; }
    bool frame_flag() const { return _frame_flag; }
    bool measure_process_time() const { return _measure_process_time; }
    uint64_t start_nsec() const { return _start_nsec; }
    uint64_t stop_nsec() const { return _stop_nsec; }
    uint64_t cpu_used() const { return _cpu_used; }

protected:
    const char* _name;
    int _stack_level;
    bool _frame_flag;
    bool _measure_process_time;

    uint64_t _start_nsec;
    uint64_t _stop_nsec;
    uint64_t _cpu_used;
};

}
