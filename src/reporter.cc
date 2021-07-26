/*
 * Copyright © 2020 Dmitry Yudin. All rights reserved.
 * Licensed under the Apache License, Version 2.0
 */

#define NOMINMAX
#include "reporter.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>
#include <math.h>

#include <sstream>
#include <iostream>
#include <iomanip>
#include <fstream>
#include <string>
#include <vector>
#include <algorithm>

#define LOG10(x) (log(x)/log(10))

#define READ_NEXT_TOKEN(s, err_action) s = strtok(NULL, " "); if (!s) { err_action; };
#define READ_NUMERIC(s, val, err_action, strtoNum) \
    READ_NEXT_TOKEN(s, err_action) \
    { char* end; val = strtoNum(s, &end, 10); if (*end != '\0') { err_action; } }
#define READ_LONG(s, val, err_action) READ_NUMERIC(s, val, err_action, strtol)

#define STREAM_PREFIX "prof:event:"

namespace fpsprof {

void Reporter::Serialize(std::ostream& os) const
{
    for (const auto& rawEvents : _rawThreadMap) {
        int thread_id = rawEvents.first;
        const auto& events = rawEvents.second;
        for (const auto& event : events) {
            os << STREAM_PREFIX << " "
                << std::setw(2) << thread_id << " "
                << event
                << std::endl;
        }
    }
}

bool Reporter::Deserialize(const char* filename)
{
    std::ifstream ifs;
    ifs.open(filename, std::ifstream::in | std::ifstream::binary);
    if (!ifs.is_open()) {
        return false;
    }
    char line[2048];
    unsigned count = 0;
    while (!ifs.getline(line, sizeof(line)).eof()) {
        if (ifs.fail()) {
            return false;
        }
        count++;

        char* s = strtok(line, " ");
        if (!s) {
            continue;
        }
        if (0 != strncmp(s, STREAM_PREFIX, strlen(STREAM_PREFIX))) {
            continue;
        }

        int thread_id;
        READ_LONG(s, thread_id, return false)

        RawEvent event;
        if (!event.desirialize(s)) {
            fprintf(stderr, "parse fail at line %d\n", count);
            return false;
        }
        _rawThreadMap[thread_id].push_back(event);
    }

    return true;
}

void Reporter::AddProfPoints(std::list<ProfPoint>&& marks)
{
    if (marks.empty()) {
        return;
    }

    std::list<RawEvent> rawEvents;
    while (!marks.empty()) {
        const auto& pp = marks.front();
        assert(pp.complete());
        rawEvents.push_back((RawEvent)pp);
        marks.pop_front();
    }

    int thread_id = (int)_rawThreadMap.size();
    _rawThreadMap[thread_id] = std::move(rawEvents);
}

static
void print_header(std::ostream& os, const std::string& header, unsigned nameLenMax)
{
    const std::string delim = std::string(nameLenMax + 51, '-');
    os << delim << std::endl;
    os << header << std::endl;
    os << delim << std::endl;

    char s[2048];
#if 0
    sprintf(s, "%-*s %9s (%6s) %9s (%6s) %10s %7s %6s\n", nameLenMax, "name", 
        "incMHz/Fr", "inc%", "excMHz/Fr", "exc%", "call/sec", "call/fr", "cpu%");
#else
    sprintf(s, "%-*s %6s %6s %10s %10s %7s %6s\n", nameLenMax, "name",
        "inc%", "exc%", "fps", "call/sec", "call/fr", "cpu%");
#endif
    os << s;
}

struct PrettyName // unsafe
{
    static const char* name(const EventAcc& accum) // unsafe
    {
        unsigned n = PrettyName::fill_size(accum);
        memset(_name, ' ', n);
        strcpy(_name + n, accum.name());

        if (accum.num_recursions()) {
            sprintf(_name + strlen(_name), "[x%u]", accum.num_recursions());
        }
        return _name;
    }
    static unsigned name_size(const EventAcc& accum) {
        unsigned recur_size = 0;
        if (accum.num_recursions()) {
            recur_size = 1 + (unsigned)LOG10(accum.num_recursions()); // rare case
            recur_size += 3; // "[x%u] "
        }
        unsigned n = PrettyName::fill_size(accum);
        return n + (unsigned)strlen(accum.name()) + recur_size;
    }

private:
    static unsigned fill_size(const EventAcc& accum) {
        unsigned stack_fill_width = std::min(128, 2 * (1 + accum.stack_level()));
        return stack_fill_width;
    }
    static char _name[1024];
};
char PrettyName::_name[];

static
void print_event(
    std::ostream& os,
    const EventAcc& frameTop,
    const EventAcc& accum,
    int nameLenMax
)
{
    const char* name = PrettyName::name(accum);
    const char* NA = "-";

    double cpuFreqMHz = 2800; // 2.8 GHz
    char totInclP[32], totExclP[32], totInclFPS[32], totInclMHzPerFrame[32], totExclMHzPerFrame[32];
    if (frameTop.realtime_used_sum() > 0) {
        double fpsHead = 1.f / (1e-9f * frameTop.realtime_used_avg());
        double inclP = 100.f * accum.realtime_used_sum() / frameTop.realtime_used_sum();
        double chldP = 100.f * accum.children_realtime_used_sum() / frameTop.realtime_used_sum();
        double exclP = inclP - chldP;
        double inclMHzPerFrame = inclP / 100.f * cpuFreqMHz / fpsHead;
        double exclMHzPerFrame = exclP / 100.f * cpuFreqMHz / fpsHead;
        double inclFPS = 1.f / (1e-9f * accum.realtime_used_sum() / frameTop.count());
        sprintf(totInclP, "%6.2f", inclP);
        sprintf(totExclP, "%6.2f", exclP);
        sprintf(totInclMHzPerFrame, "%9.1f", inclMHzPerFrame);
        sprintf(totExclMHzPerFrame, "%9.1f", exclMHzPerFrame);
        sprintf(totInclFPS, "%10.1f", inclFPS);
    } else {
        sprintf(totInclP, "%6s", NA);
        sprintf(totExclP, "%6s", NA);
        sprintf(totInclMHzPerFrame, "%9s", NA);
        sprintf(totExclMHzPerFrame, "%9s", NA);
        sprintf(totInclFPS, "%10s", NA);
    }

    char selfInclP[32], selfExclP[32], selfInclFPS[32], selfExclFPS[32];
    if (accum.realtime_used_avg() > 0) {
        double fpsHead = 1 / (1e-9f * accum.realtime_used_avg());
        double inclP = 100. * accum.realtime_used_avg() / accum.realtime_used_avg(); // 100%
        double chldP = 100. * accum.children_realtime_used_avg() / accum.realtime_used_avg();
        double exclP = inclP - chldP;
        double inclFPS = inclP / 100. * fpsHead;
        double exclFPS = exclP / 100. * fpsHead;
        sprintf(selfInclP, "%5.1f", inclP); // 100%
        sprintf(selfExclP, "%5.1f", exclP);
        sprintf(selfInclFPS, "%10.1f", inclFPS);
        sprintf(selfExclFPS, "%10.1f", exclFPS);
    } else {
        sprintf(selfInclP, "%5s", NA);
        sprintf(selfExclP, "%5s", NA);
        sprintf(selfInclFPS, "%10s", NA);
        sprintf(selfExclFPS, "%10s", NA);
    }
    char numCalls[32];
    if(frameTop.count() > 0) {
        double n = ((double)accum.count()) / frameTop.count();
        sprintf(numCalls, "%7.2f", n);
    } else {
        sprintf(numCalls, "%7s", NA);
    }
    char cpuP[64];
    if (accum.realtime_used_sum() > 0) {
        double inclP = 100.f* accum.cpu_used_sum()/ accum.realtime_used_sum();
        sprintf(cpuP, "%6.1f", inclP);
    } else {
        sprintf(cpuP, "%6.1f", 0.);
    }

    char s[2048];
#if 0
    fprintf(s, "%-*s %s (%s) %s (%s) %s %s %s\n", 
        nameLenMax, name, 
        totInclMHzPerFrame, totInclP, totExclMHzPerFrame, totExclP,
        selfInclFPS, numCalls, cpuP
        );
#else
    sprintf(s, "%-*s %s %s %s %s %s %s\n",
        nameLenMax, name,
        totInclP, totExclP, totInclFPS,
        selfInclFPS, numCalls, cpuP
    );
    os << s;
#endif
}

static
void print_tree(
    std::ostream& os,
    const EventAcc& frameTop,
    const std::list<EventAcc>::const_iterator& begin,
    const std::list<EventAcc>::const_iterator& end,
    int stackLevelMax,
    int nameWidth)
{
    if (begin == end) {
        return;
    }
    const EventAcc& parent = *begin;
    print_event(os, frameTop, parent, nameWidth);

    if (parent.stack_level() >= stackLevelMax) {
        return;
    }

    // Due to merge procedure, we may have multiple items shared same stack position.
    //   foo               foo        Some stack positions may disappear.
    //     \bar-1            \bar-1   We can only loop to num_children_max() to
    //   foo     => merge =>  bar-2   find all childrens in a list.
    //     \bar-2
    std::vector<std::list<EventAcc>::const_iterator> children;
    {
        if(parent.stack_level() == 9 && 0 == strcmp(parent.name(), "CompressCtu_InterRecur")) {
   //         __debugbreak();
        }
        
        for(unsigned stack_pos = 0; stack_pos < parent.num_children_max(); stack_pos++) {
            auto child_begin = begin;
            while (true) {
                const auto& it = std::find_if(child_begin, end,
                    [&parent, stack_pos](const EventAcc& eventAcc) {
                        return eventAcc.parent_path() == parent.self_path()
                            && eventAcc.stack_pos() == stack_pos;
                    }
                );
                if (it == end) {
                    break;
                } else {
                    children.push_back(it);
                    child_begin = it;
                }
                child_begin++;
            }
        }
    }

    for (unsigned idx = 0; idx < children.size(); idx++) {
        const auto& child_begin = children[idx];
        const auto& child_end = idx + 1 < children.size() ? children[idx+1] : end;
        print_tree(os, frameTop, child_begin, child_end, stackLevelMax, nameWidth);
    }
}

static
void print_threads(
    std::ostream& os,
    int reportType,
    const std::vector< std::list<EventAcc> >& threadAccums,
    int stackLevelMax,
    int nameWidth
)
{
    std::string header;
    {
        std::string numThreads = std::to_string(threadAccums.size());
        switch (reportType) {
            case REPORT_THREAD_ROOT:
                header = std::string("Threads summary");
                break;
            case REPORT_STACK_TOP:
                header = std::string("Stack top");
                break;
            case REPORT_SUMMARY_NO_REC:
                header = "Summary report (no recursion)";
                break;
            case REPORT_SUMMARY:
                header = "Summary report";
                break;
            case REPORT_DETAILED:
                header = "Detailed report";
                break;
        }
        header += std::string(" [ ") + std::to_string(threadAccums.size()) + " thread(s) ]";
    }
    print_header(os, header, nameWidth);

    const EventAcc& frameTop = threadAccums[0].front();
    for (size_t threadId = 0; threadId < threadAccums.size(); threadId++) {
        const auto& accums = threadAccums[threadId];

        RootAcc *rootAcc = RootAcc::Create(accums, (unsigned)threadId);
        if (rootAcc) {
            print_event(os, frameTop, *rootAcc, nameWidth);
        }
        if (reportType == REPORT_THREAD_ROOT) {
            continue;
        }

        if (reportType == REPORT_STACK_TOP) {
            stackLevelMax = 0;
        }
        // Top-level may have no root. Here we gather all items with stack_level == 0
        std::vector<std::list<EventAcc>::const_iterator> roots;
        for (auto it = accums.begin(); it != accums.end(); it++) {
            const EventAcc& eventAcc = *it;
            if (eventAcc.stack_level() == 0) {
                roots.push_back(it);
            }           
        }
        assert(roots.size() > 0);

        for (size_t i = 0; i < roots.size(); i++) {
            const auto& begin = roots[i];
            const auto& end = i + 1 < roots.size() ? roots[i + 1] : accums.end();
            print_tree(os, frameTop, begin, end, stackLevelMax, nameWidth);
        }
    }
    os << std::endl;
}

std::string Reporter::Report(int reportFlags, int stackLevelMax)
{
    std::vector< std::list<Event> > threadEvents;
    for (auto& rawEventsItem : _rawThreadMap) {
        auto& rawEvents = rawEventsItem.second;

        auto events = Event::Create(std::move(rawEvents));
        threadEvents.push_back(std::move(events));
    }
    {
        int mainThreadId = -1;
        for(size_t threadId = 0; threadId < threadEvents.size(); threadId++) {
            const auto& head = threadEvents[threadId].front();
            if (head.frame_flag()) {
                mainThreadId = (unsigned)threadId;
                break;
            }
        }
        if (mainThreadId == -1) {
            return "";
        }
        if (mainThreadId != 0) { // set to mt_id = 0
            auto other = std::move(threadEvents[0]);
            auto main = std::move(threadEvents[mainThreadId]);
            threadEvents[0] = main;
            threadEvents[mainThreadId] = other;
        }
    }

    std::vector< std::list<EventAcc> > threadAccums;
    for (auto& events : threadEvents) {
        auto accums = EventAcc::Create(std::move(events));
        threadAccums.push_back(std::move(accums));
    }

    std::vector< std::list<EventAcc> > threadSummary;
    for (const auto& accums : threadAccums) {
        auto summary = EventAcc::CreateSummary(accums);
        threadSummary.push_back(std::move(summary));
    }

    std::vector< std::list<EventAcc> > threadSummaryNoRec;
    for (const auto& accums : threadSummary) {
        auto summaryNoRec = EventAcc::CreateSummaryNoRec(accums);
        threadSummaryNoRec.push_back(std::move(summaryNoRec));
    }
    
    unsigned nameWidth = 0;
    {
        int stackLevelMax_ = 0;
        for (const auto& accums : threadAccums) {
            int stackLevel = 0;
            for (const auto& accum : accums) {
                stackLevel = std::max(stackLevel, accum.stack_level());
                nameWidth = std::max(nameWidth, PrettyName::name_size(accum));
            }
            stackLevelMax_ = std::max(stackLevelMax_, stackLevel);
        }
        stackLevelMax_ += 1;
        if (stackLevelMax < 0) {
            stackLevelMax = stackLevelMax_;
        } else {
            stackLevelMax = std::min(stackLevelMax, stackLevelMax_);
        }
    }

#define DEBUG_REPORT 1
#if DEBUG_REPORT
    std::ostream& ss = std::cout;
#else
    std::stringstream ss;
#endif
    if (reportFlags & REPORT_THREAD_ROOT) {
        print_threads(ss, REPORT_THREAD_ROOT, threadSummary, stackLevelMax, nameWidth);
    }
    if (reportFlags & REPORT_STACK_TOP) {
        print_threads(ss, REPORT_STACK_TOP, threadSummary, stackLevelMax, nameWidth);
    }
    if (reportFlags & REPORT_SUMMARY_NO_REC) {
        print_threads(ss, REPORT_SUMMARY_NO_REC, threadSummaryNoRec, stackLevelMax, nameWidth);
    }
    if (reportFlags & REPORT_SUMMARY) {
        print_threads(ss, REPORT_SUMMARY, threadSummary, stackLevelMax, nameWidth);
    }
    if (reportFlags & REPORT_DETAILED) {
        print_threads(ss, REPORT_DETAILED, threadAccums, stackLevelMax, nameWidth);
    }
#if DEBUG_REPORT
    return "We're maintaining. Keep calm and don't panic.";
#else
    return ss.str();
#endif
}

}
