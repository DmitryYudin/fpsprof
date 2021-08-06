/*
 * Copyright © 2021 Dmitry Yudin. All rights reserved.
 * Licensed under the Apache License, Version 2.0
 */

#define NOMINMAX

#include "profthread.h"

#include <algorithm>

namespace fpsprof {

ProfThread::~ProfThread()
{
    std::list<ProfPoint> storage;
#if !USE_FASTWRITE_STORAGE
    storage = std::move(_storage);
#else
    storage = _storage.to_list();
#endif
    _threadMgr.onProfThreadExit(std::move(storage));
}

ProfPoint* ProfThread::push(const char* name, bool frame_flag)
{
    bool measure_process_time = false;
#if !USE_FASTWRITE_STORAGE
    _storage.emplace_back(name, _stack_level++, frame_flag, measure_process_time);
    ProfPoint* pp = &_storage.back(); // safe for a list<>
#else
    if (_stack_level == 0) {
        unsigned events_count = _storage.size();
        unsigned events_num = events_count - _events_count_prev;
        _events_num_max = std::max(_events_num_max, events_num);
        _events_count_prev = events_count;
        _storage.reserve(3 * _events_num_max);
    }
    ProfPoint* pp = _storage.alloc_item();
    *pp = ProfPoint(name, _stack_level++, frame_flag, measure_process_time);
#endif
#ifndef NDEBUG
    _pp_last_in = pp;
#endif
    pp->Start(_storage.get_overhead_wc());
    return pp;
}
void ProfThread::pop(ProfPoint* pp)
{
    _stack_level--;

    if (pp->stack_level() != _stack_level) {
        panic_and_exit(pp);
    }
    pp->Stop(_storage.get_overhead_wc());
    #ifndef NDEBUG
    _pp_last_out = pp;
    #endif
}
void ProfThread::panic_and_exit(ProfPoint* pp) {
    const char* exit_name = pp->name();
    unsigned exit_level = pp->stack_level();
    std::list<ProfPoint> storage;
#if !USE_FASTWRITE_STORAGE
    storage = std::move(_storage);
#else
    storage = _storage.to_list();
#endif
    for (const auto& mark : storage) {
        if (mark.complete()) {
            continue;
        }
        unsigned n = mark.stack_level();
        char info[32] = "";
        if (n == exit_level && mark.name() == exit_name) {
            strcpy(info, " <- exit is here");
        }
        fprintf(stderr, "%2u: %*s %s%s\n", n, 2*n, "", mark.name(), info);
    }
    fprintf(stderr, "error: pop '%s' event with a stack level of %u, "
        "but current stack level is %u\n",  exit_name, exit_level, _stack_level);

    exit(1);
}

}
