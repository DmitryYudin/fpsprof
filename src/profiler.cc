/*
 * Copyright © 2020 Dmitry Yudin. All rights reserved.
 * Licensed under the Apache License, Version 2.0
 */

#define NOMINMAX
#include "fpsprof/fpsprof.h"
#include "profpoint.h"
#include "reporter.h"
#include "fastwrite_storage.h"

#include <string.h>
#include <list>
#include <algorithm>
#include <fstream>

#define USE_FASTWRITE_STORAGE 1
namespace fpsprof {

class ThreadMgr {
public:
    ThreadMgr() {
        timer::wallclock_t start_wc = timer::wallclock::timestamp();
        unsigned n = 10*1000;
        uint64_t sum_wc = 0;
        for(unsigned i = 0; i < n; i++) {
            sum_wc -= timer::wallclock::timestamp();
            timer::thread::now();
            sum_wc += timer::wallclock::timestamp();
            timer::thread::now();
        }
        timer::wallclock_t stop_wc = timer::wallclock::timestamp();

        int64_t self_nsec = timer::wallclock::diff(sum_wc, 0);
        int64_t children_nsec = timer::wallclock::diff(stop_wc, start_wc);
#ifndef NDEBUG
        fprintf(stderr, "wallclock penalty per %d marks: self %.8f sec (%.0f nsec), children %.8f sec (%.0f nsec)\n",
                n, self_nsec*1e-9, (double)self_nsec, children_nsec*1e-9, (double)children_nsec);
#endif
        // children 0.01087290 sec (10872900 nsec) per 10000 marks,
        // self     0.00542990 sec ( 5429900 nsec)

        // children 0.01043500 sec (10435000 nsec) per 10000 marks
        // self     0.00521110 sec ( 5211100 nsec)

        // children 0.10455560 sec (104555600 nsec) per 100000 marks,
        // self     0.05223850 sec ( 52238500 nsec)
        _penalty_denom = n;
        _penalty_self_nsec = self_nsec;
        _penalty_children_nsec = children_nsec;
    }

    ~ThreadMgr() {
        if (!_serialize_filename.empty()) {
            std::ofstream ofs(_serialize_filename, std::ios::out | std::ios::binary | std::ios::trunc);
            if (ofs.is_open()) {
                _reporter.Serialize(ofs);
                ofs.close();
            }
        }
        FILE *fp = !_report_filename.empty() ? fopen(_report_filename.c_str(), "wb") : _report;
        if (fp) {
/*           
            Disable detailed report print out since children go to first parent by the design
            and this produce unexpected call stack view. The summary report fixes this issue.

            LoopFilterLcu(fake)
            ...
            LoopFilterLcu(do_job)
            V
             -- LoopFilterLcu            --  0.0   0.0   121172.8    30278.3    0.25   98.6
            |     DeblockingFilterLCU   |    2.4   2.4      842.4   202101.9  239.88  104.2
            |     SaoLcu                |   13.5   7.6      152.6    36596.5  239.88   93.2
            |       ProcessSao          |    3.4   0.6      615.1   147557.9  239.88   91.6
            |   EncodeLCU               |    0.0   0.0   219132.1    54755.5    0.25    0.0
             -> LoopFilterLcu            -> 16.5   0.3      125.2    29992.2  239.63   94.5

*/
            fprintf(fp, "%s\n", _reporter.Report().c_str());
            if (!_report_filename.empty()) {
                fclose(fp);
            }
        }
    }
    void get_penalty(unsigned& penalty_denom, int64_t& penalty_self_nsec, int64_t& penalty_children_nsec) {
        penalty_denom = _penalty_denom;
        penalty_self_nsec = _penalty_self_nsec;
        penalty_children_nsec = _penalty_children_nsec;
    }

    void set_serialize_stream(FILE* stream) {
        _serialize = stream;
    }
    void set_serialize_file(const char* filename) {
        _serialize_filename = filename ? filename : "";
    }
    void set_report_stream(FILE* stream) {
        _report = stream;
    }
    void set_report_file(const char* filename) {
        _report_filename = filename ? filename : "";
    }
    void onThreadProfExit(std::list<ProfPoint>&& marks) {
        _reporter.AddProfPoints(std::move(marks));
    }
private:
    FILE* _serialize = NULL;
    std::string _serialize_filename;
    FILE* _report = NULL;
    std::string _report_filename;

    Reporter _reporter;

    unsigned _penalty_denom;
    int64_t _penalty_children_nsec;
    int64_t _penalty_self_nsec;
};

struct ThreadProf {
    explicit ThreadProf(ThreadMgr& threadMgr) : _threadMgr(threadMgr) {
    }
    ~ThreadProf() {
        if (_storage.size() == 0) {
            return;
        }
        std::list<ProfPoint> storage;
#if !USE_FASTWRITE_STORAGE
        storage = std::move(_storage);
#else
        storage = _storage.to_list();
#endif
        _threadMgr.onThreadProfExit(std::move(storage));
    }

    ProfPoint* push(const char* name, bool frame_flag) {
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
    void pop(ProfPoint* pp) {
        _stack_level--;

        if (pp->stack_level() != _stack_level) {
            panic_and_exit(pp);
        }
        pp->Stop(_storage.get_overhead_wc());
#ifndef NDEBUG
        _pp_last_out = pp;
#endif
    }
private:
    void panic_and_exit(ProfPoint* pp) {
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
    ThreadMgr& _threadMgr;
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

timer::wallclock_t ProfPoint::_init_wc = timer::wallclock::timestamp();
static ThreadMgr gThreadMgr;
static thread_local ThreadProf gThreadProf(gThreadMgr);

extern void GetPenalty(unsigned& penalty_denom, int64_t& penalty_self_nsec, int64_t& penalty_children_nsec)
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
    return fpsprof::gThreadProf.push(name, true);
}
extern "C" void* FPSPROF_start(const char* name)
{
    return fpsprof::gThreadProf.push(name, false);
}
extern "C" void FPSPROF_stop(void* handle)
{
    fpsprof::gThreadProf.pop((fpsprof::ProfPoint*)handle);
}
