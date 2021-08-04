/*
 * Copyright © 2020 Dmitry Yudin. All rights reserved.
 * Licensed under the Apache License, Version 2.0
 */

#pragma once

#include "timers.h"
#include <assert.h>

namespace fpsprof {

struct ProfPoint {
    enum state_t { CREATED = 0, STARTED = 1, COMPLETE = 2 };

    ProfPoint(const char name[], int stack_level, bool frame_flag, bool measure_process_time)
        : _state(CREATED), _name(name), _stack_level(stack_level)
        , _frame_flag(frame_flag), _measure_process_time(measure_process_time) {
    }
    void Start(timer::wallclock_t penalty_wc) {
        assert(_state == CREATED);
        _state = STARTED;
        _start_wc = timer::wallclock::timestamp() - penalty_wc;
        _start_cpu = _measure_process_time ? timer::process::now() : timer::thread::now();
    }
    void Stop(timer::wallclock_t penalty_wc) {
        assert(_state == STARTED);
        _state = COMPLETE;
        _stop_wc = timer::wallclock::timestamp() - penalty_wc;
        _stop_cpu = _measure_process_time ? timer::process::now() : timer::thread::now();
    }
    const char* name() const {
        return _name;
    }
    int stack_level() const {
        return _stack_level;
    }
    bool frame_flag() const {
        return _frame_flag;
    }
    bool measure_process_time() const {
        return _measure_process_time;
    }
    uint64_t realtime_start() const {
        return timer::wallclock::diff(_start_wc, _init_wc);
    }
    uint64_t realtime_stop() const {
        return timer::wallclock::diff(_stop_wc, _init_wc);
    }
    uint64_t cputime_delta() const {
        int64_t cpu_delta = _stop_cpu - _start_cpu;
        return cpu_delta < 0 ? 0 : cpu_delta;
    }
    bool complete() const {
        return _state == COMPLETE;
    }
private:
    static timer::wallclock_t _init_wc;

    state_t _state;

    const char* _name;
    int _stack_level;
    bool _frame_flag;
    bool _measure_process_time;
    timer::wallclock_t _start_wc;
    timer::wallclock_t _stop_wc;
    uint64_t _start_cpu;
    uint64_t _stop_cpu;
};

}
