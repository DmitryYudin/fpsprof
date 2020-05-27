/*
 * Copyright � 2020 Dmitry Yudin. All rights reserved.
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

#define USE_FASTWRITE_STORAGE 1
namespace fpsprof {

class ThreadMgr {
public:
    ~ThreadMgr() {
        FILE* fp = !_serialize_filename.empty() ? fopen(_serialize_filename.c_str(), "wb") : _serialize;
        if (fp) {
            fprintf(fp, "%s\n", _reporter.Serialize().c_str());
            if (!_serialize_filename.empty()) {
                fclose(fp);
            }
        }

        fp = !_report_filename.empty() ? fopen(_report_filename.c_str(), "wb") : _report;
        if (fp) {
            int reportFlags = -1;
            fprintf(fp, "%s\n", _reporter.Report(reportFlags).c_str());
            if (!_report_filename.empty()) {
                fclose(fp);
            }
        }
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
    void onThreadProfExit(const std::list<ProfPoint>& marks) {
        _reporter.AddProfPoints(marks);
    }
private:
    FILE* _serialize = NULL;
    std::string _serialize_filename;
    FILE* _report = NULL;
    std::string _report_filename;

    Reporter _reporter;
};

struct ThreadProf {
    explicit ThreadProf(ThreadMgr& threadMgr) : _threadMgr(threadMgr) {
    }
    ~ThreadProf() {
        if (_marks.size() == 0) {
            return;
        }
        std::list<ProfPoint> marks;
#if !USE_FASTWRITE_STORAGE
        marks = std::move(_marks);
#else
        marks = _marks.to_list();
#endif
        _threadMgr.onThreadProfExit(marks);
    }

    ProfPoint* push(const char* name, bool frame_flag) {
        bool measure_process_time = false;
#if !USE_FASTWRITE_STORAGE
        _marks.emplace_back(name, _stack_level++, frame_flag, measure_process_time);
        ProfPoint* pp = &_marks.back(); // safe for a list<>
#else
        if (_stack_level == 0) {
            unsigned events_count = _marks.size();
            unsigned events_num = events_count - _events_count_prev;
            _events_num_max = std::max(_events_num_max, events_num);
            _events_count_prev = events_count;
            _marks.reserve(3 * _events_num_max);
        }
        ProfPoint* pp = _marks.alloc_item();
        *pp = ProfPoint(name, _stack_level++, frame_flag, measure_process_time);
#endif
#ifndef NDEBUG
        _pp_last_in = pp;
#endif
        pp->Start();
        return pp;
    }
    void pop(ProfPoint* pp) {
        _stack_level--;

        if (pp->stack_level() != _stack_level) {
            panic_and_exit(pp);
        }
        pp->Stop();
#ifndef NDEBUG
        _pp_last_out = pp;
#endif
    }
private:
    void panic_and_exit(ProfPoint* pp) {
        const char* exit_name = pp->name();
        unsigned exit_level = pp->stack_level();
        std::list<ProfPoint> marks;
#if !USE_FASTWRITE_STORAGE
        marks = std::move(_marks);
#else
        marks = _marks.to_list();
#endif
        for (const auto& mark : marks) {
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
    std::list<ProfPoint> _marks;
#else
    fastwrite_storage_t<ProfPoint> _marks;
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