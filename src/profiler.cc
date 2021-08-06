/*
 * Copyright © 2020 Dmitry Yudin. All rights reserved.
 * Licensed under the Apache License, Version 2.0
 */

#include "fpsprof/fpsprof.h"
#include "profpoint.h"
#include "profthread.h"
#include "profthreadmgr.h"

namespace fpsprof {

timer::wallclock_t ProfPoint::_init_wc = timer::wallclock::timestamp();

static ProfThreadMgr gThreadMgr;
static thread_local ProfThread gProfThread(gThreadMgr);

extern void GetPenalty(unsigned& penalty_denom, uint64_t& penalty_self_nsec, uint64_t& penalty_children_nsec)
{
    gThreadMgr.get_penalty(penalty_denom, penalty_self_nsec, penalty_children_nsec);
}

}

extern "C" void FPSPROF_serialize_stream(FILE* fp)
{
    fpsprof::gThreadMgr.set_serialize_stream(fp);
}
extern "C" void FPSPROF_serialize_file(const char* filename)
{
    fpsprof::gThreadMgr.set_serialize_file(filename);
}
extern "C" void FPSPROF_report_stream(FILE* fp)
{
    fpsprof::gThreadMgr.set_report_stream(fp);
}
extern "C" void FPSPROF_report_file(const char* filename)
{
    fpsprof::gThreadMgr.set_report_file(filename);
}
extern "C" void* FPSPROF_start_frame(const char* name)
{
    return fpsprof::gProfThread.push(name, true);
}
extern "C" void* FPSPROF_start(const char* name)
{
    return fpsprof::gProfThread.push(name, false);
}
extern "C" void FPSPROF_stop(void* handle)
{
    fpsprof::gProfThread.pop((fpsprof::ProfPoint*)handle);
}
