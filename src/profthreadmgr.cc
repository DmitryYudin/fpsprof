/*
 * Copyright © 2021 Dmitry Yudin. All rights reserved.
 * Licensed under the Apache License, Version 2.0
 */

#define NOMINMAX

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

static std::list<uint64_t> collect_counters(unsigned n)
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
    const std::list<ProfPoint>& storage = gDummyProfThreadMgr.storage;

    std::list<uint64_t> data;
    for(const auto& pp: storage) {
        data.push_back(pp.realtime_stop() - pp.realtime_start());
    }
    return data;
}

static void refine_counter_lo(const std::list<uint64_t>& data, uint64_t& refined_sum, unsigned& refined_cnt)
{
    if(data.empty()) {
        refined_sum = 0;
        refined_cnt = 0;
        return;
    }
    unsigned hist_sz = 1024;
    unsigned *hist = new unsigned[hist_sz];
    memset(hist, 0, sizeof(*hist)*hist_sz);

    uint64_t min = 0, max = data.front();
    for(auto d: data) {
        min = std::min(min, d);
        max = std::max(max, d);
    }
    uint64_t bin_width = (max - min + hist_sz - 1)/hist_sz;
    bin_width = std::max(bin_width, (uint64_t)1);
    for(auto d: data) {
        unsigned idx = (unsigned)( (d - min)/bin_width );
        assert(idx < hist_sz);
        hist[idx] += 1;
    }

    // first (nonzero) max bin
    unsigned hist_max = 0, bin_max = 0;
    for(unsigned i = 1; i < hist_sz; i++) {
        if(hist_max < hist[i]) {
            hist_max = hist[i];
            bin_max = i;
        }
    }

    int idx_end = bin_max;
    for(; idx_end < (int)hist_sz; idx_end++) {
        if(hist[idx_end] < (unsigned)(hist[bin_max]*.95)) {
            break;
        }
    }
    unsigned fac = 5;
    idx_end = (bin_max+1)*fac; // only consider values x times greater than max_at_bin_max

    unsigned cnt = 0;
    uint64_t sum = 0, sum2 = 0;
    for(auto d: data) {
        int idx = (int)( (d - min)/bin_width );
        if(idx < idx_end) {
            sum += d;
            cnt += 1;
        } else {
            sum2 += d;
        }
    }

    refined_sum = sum;
    refined_cnt = cnt;

    delete [] hist;
}

static double refine_counter_hi(const std::list< double >& data)
{
    double sum = 0;
    for(const auto d: data) {
        sum += d;
    }
    double avg = sum / data.size(), sigma = 0;
    for(const auto d: data) {
        sigma += (d - avg)*(d - avg);
    }
    sigma = sqrtl(sigma / data.size());

    unsigned n = 0;
    sum = 0;
    for(const auto d: data) { // sigma ~ 68%, 2*sigma ~ 95%
        if( avg - sigma < d && d < avg + sigma) {
            sum += d;
            n += 1;
        }
    }
    if(n == 0) { // maybe
        for(const auto d: data) {
            sum += d;
        }
        n = (unsigned)data.size();
    }
    return sum / n;
}

ProfThreadMgr::ProfThreadMgr()
    : _reporter(new Reporter)
{
    std::list< double > stat_s, stat_c;
    unsigned num_outer = 100, num_inner = 10000;
    while(num_outer--) {
        auto data = collect_counters(num_inner);

        uint64_t children_nsec = data.front();
        data.pop_front();

        uint64_t self_nsec;
        unsigned refined_cnt;
        refine_counter_lo(data, self_nsec, refined_cnt);
        
        stat_s.push_back((double)self_nsec / refined_cnt);
        stat_c.push_back((double)children_nsec / data.size());
    }

    double sum_s = 0;
    for(const auto d: stat_s) {
        sum_s += d;
    }

    double self_nsec = sum_s / stat_s.size();
    //double children_nsec = sum_c / stat_c.size();
    double children_nsec = refine_counter_hi(stat_c);

#ifndef NDEBUG
    fprintf(stderr, "wallclock penalty (nsec): self %8.2f, children %8.2f\n", self_nsec, children_nsec);
#endif
    _penalty_denom = 10000;
    _penalty_self_nsec = (uint64_t)(_penalty_denom * self_nsec);
    _penalty_children_nsec = (uint64_t)(_penalty_denom * children_nsec);
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
