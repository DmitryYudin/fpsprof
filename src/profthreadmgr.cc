/*
 * Copyright © 2021 Dmitry Yudin. All rights reserved.
 * Licensed under the Apache License, Version 2.0
 */

#include "profthreadmgr.h"
#include "profthread.h"
#include "reporter.h"

#include <fstream>

namespace fpsprof {

#if __GNUC__ || __clang__
    #define _noinline __attribute__ ((noinline))
#else
    #define _noinline __declspec(noinline)
#endif
/*
    Simulate real profiler call
*/
static ProfThread *gDummyProfThread = NULL;
extern "C" _noinline void* FPSPROF_start_dummy(const char* name)
{
    return fpsprof::gDummyProfThread->push(name, false);
}
extern "C" _noinline void FPSPROF_stop_dummy(void* handle)
{
    fpsprof::gDummyProfThread->pop((fpsprof::ProfPoint*)handle);
}

static std::list<ProfPoint> collect_counters(unsigned n)
{
    struct DummyProfThreadMgr : public IProfThreadMgr {
        void onProfThreadExit(std::list<ProfPoint>&& marks) override { storage = std::move(marks); }
        std::list<ProfPoint> storage;
    };

    DummyProfThreadMgr gDummyProfThreadMgr;
    gDummyProfThread = new ProfThread(gDummyProfThreadMgr); // dump all events to Mgr on destroy

    void *outer = FPSPROF_start_dummy("outer");    
    for(unsigned i = 0; i < n; i++) {
        void *inner = FPSPROF_start_dummy("dummy");
        FPSPROF_stop_dummy(inner);
    }
    FPSPROF_stop_dummy(outer);

    delete gDummyProfThread; // dump events
    return std::move(gDummyProfThreadMgr.storage);
}

ProfThreadMgr::ProfThreadMgr()
    : _reporter(new Reporter)
{
    unsigned n = 1000*1000;
    auto storage = collect_counters(n);

    auto pp_outer = storage.front();
    storage.pop_front();
    uint64_t children_nsec = pp_outer.realtime_stop() - pp_outer.realtime_start();

    uint64_t self_nsec = 0;
    for(const auto& pp_inner: storage) {
        self_nsec += pp_inner.realtime_stop() - pp_inner.realtime_start();
    }

//#ifndef NDEBUG
    fprintf(stderr, "wallclock penalty per %d marks: self %.8f sec (%10.0f nsec), children %.8f sec (%10.0f nsec)\n",
            n, self_nsec*1e-9, (double)self_nsec, children_nsec*1e-9, (double)children_nsec);
//#endif
    _penalty_denom = n;
    _penalty_self_nsec = self_nsec;
    _penalty_children_nsec = children_nsec;
}

ProfThreadMgr::~ProfThreadMgr() {
    if (!_serialize_filename.empty()) {
        std::ofstream ofs(_serialize_filename, std::ios::out | std::ios::binary | std::ios::trunc);
        if (ofs.is_open()) {
            _reporter->Serialize(ofs);
            ofs.close();
        }
    }
    FILE *fp = !_report_filename.empty() ? fopen(_report_filename.c_str(), "wb") : _report;
    if (fp) {
        fprintf(fp, "%s\n", _reporter->Report().c_str());
        if (!_report_filename.empty()) {
            fclose(fp);
        }
    }
    delete _reporter;
}

void ProfThreadMgr::onProfThreadExit(std::list<ProfPoint>&& marks)
{
    // TODO: critical section
    _reporter->AddRawThread(std::move(marks));
}

}
