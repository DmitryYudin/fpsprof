/*
 * Copyright © 2021 Dmitry Yudin. All rights reserved.
 * Licensed under the Apache License, Version 2.0
 */

#pragma once

#include "profpoint.h"
#include "fastwrite_storage.h"
#include "profthreadmgr.h"

namespace fpsprof {

#define USE_FASTWRITE_STORAGE 1

struct ProfThread {
    explicit ProfThread(IProfThreadMgr& threadMgr) : _threadMgr(threadMgr) {}
    ~ProfThread();
    ProfPoint* push(const char* name, bool frame_flag);
    void pop(ProfPoint* pp);

private:
    void panic_and_exit(ProfPoint* pp);

    IProfThreadMgr& _threadMgr;
#if !USE_FASTWRITE_STORAGE
    std::list<ProfPoint> _storage;
#else
    fastwrite_storage_t<ProfPoint> _storage;
#endif

    int _stack_level = 0;
    unsigned _events_count_prev = 0;
    unsigned _events_num_max = 0;

#ifndef NDEBUG
    const ProfPoint* _pp_last_in = NULL;
    const ProfPoint* _pp_last_out = NULL;
#endif
};

}
