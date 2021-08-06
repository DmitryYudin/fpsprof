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

#define N (1000*1000)

static void build_hist(const std::list<uint64_t>& data, uint64_t children_nsec)
{
    unsigned hist_sz = 1024;
    unsigned *hist = new unsigned[hist_sz];

    memset(hist, 0, sizeof(*hist)*hist_sz);
    if(data.empty()) {
        return;
    }
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

    int cnt_remove = (int)(data.size()*.05);
    int idx_end = hist_sz - 1;
    for(;idx_end > 0; idx_end--) {
        cnt_remove -= hist[idx_end];
        if(cnt_remove <= 0) {
            break;
        }
    }

    // first (nonzero) max bin
    unsigned hist_max = 0, bin_max = 0;
    for(unsigned i = 1; i < hist_sz; i++) {
        if(hist_max < hist[i]) {
            hist_max = hist[i];
            bin_max = i;
        }
    }

    /*
    idx_end = hist_sz - 1;
    for(;idx_end > 0; idx_end--) {
        if(hist[idx_end] > (unsigned)(hist[bin_max]*.95)) {
            break;
        }
    }
    */
    for(idx_end = bin_max; idx_end < (int)hist_sz; idx_end++) {
        if(hist[idx_end] < (unsigned)(hist[bin_max]*.95)) {
            break;
        }
    }
    idx_end = 10;
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

    fprintf(stderr, "hist (nsec): %8.2f %8.2f\n", (double)sum / cnt, (double)(children_nsec - sum2) / cnt);
/*
    unsigned hist_max = 0, hist_pos = 0;
    for(unsigned i = 0; i < hist_sz; i++) {
        if(hist_max < hist[i]) {
            hist_max = hist[i];
            hist_pos = i;
        }
    }

    uint64_t sum = 0;
    for(auto d: data) {
        unsigned idx = (unsigned)( (d - min)/bin_width );
        if(idx == hist_pos) {
            sum += d;
        }
    }
    sum = sum*N/hist[hist_pos];
    fprintf(stderr, "hist:%d %10.0f at %6d //// %.8f sec (%10.0f nsec)\n", hist_sz, (double)hist[hist_pos]/N*100, hist_pos, sum*1e-9, (double)sum);
    */
    delete [] hist;
}

ProfThreadMgr::ProfThreadMgr()
    : _reporter(new Reporter)
{
    unsigned n = N;
    auto data = collect_counters(n);

    uint64_t children_nsec = data.front();
    data.pop_front();
    uint64_t self_nsec = 0;
    for(const auto& nsec: data) {
        self_nsec += nsec;
    }
    uint64_t avg = self_nsec / n;
    uint64_t sigma = 0;
    for(const auto& nsec: data) {
        sigma += (nsec - avg)*(nsec - avg);
    }
    sigma = (uint64_t)sqrtl((double)sigma/n);

    unsigned n2 = 0;
    uint64_t s2 = 0, low = avg > 2*sigma ? avg - 2*sigma : 0, hi = avg + 2*sigma;
    for(const auto& nsec: data) {
        if(low < nsec && nsec < hi) {
            s2 += nsec;
            n2++;
        }
    }
    s2 *= n;
    s2 /= n2; // normalize to n
//#ifndef NDEBUG
    //fprintf(stderr, "wallclock penalty per %d marks: self %.8f sec (%10.0f nsec), children %.8f sec (%10.0f nsec) / %.8f sec (%10.0f nsec)\n",
//            n, self_nsec*1e-9, (double)self_nsec, children_nsec*1e-9, (double)children_nsec, s2*1e-9, (double)s2);
// 
    fprintf(stderr, "wallclock penalty (nsec): self %8.2f, children %8.2f / %8.2f\n",
            (double)self_nsec/n, (double)children_nsec/n, (double)s2/n);
//#endif
    _penalty_denom = n;
    _penalty_self_nsec = self_nsec;
    _penalty_children_nsec = children_nsec;

    build_hist(data, children_nsec);
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
