/*
 * Copyright © 2020 Dmitry Yudin. All rights reserved.
 * Licensed under the Apache License, Version 2.0
 */

#define NOMINMAX
#include "reporter.h"
#include "node.h"
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
    const std::string delim = std::string(nameLenMax + 55, '-');
    os << delim << std::endl;
    os << header << std::endl;
    os << delim << std::endl;

    char s[2048];
#if 0
    sprintf(s, "%3s %-*s %9s (%6s) %9s (%6s) %10s %7s %6s\n", "st", nameLenMax, "name", 
        "incMHz/Fr", "inc%", "excMHz/Fr", "exc%", "call/sec", "call/fr", "cpu%");
#else
    sprintf(s, "%3s %-*s %6s %6s %10s %10s %7s %6s\n", "st", nameLenMax, "name",
        "inc%", "exc%", "fps", "call/sec", "call/fr", "cpu%");
#endif
    os << s;
}

struct PrettyName // unsafe
{
    static const char* name(const Node& node) // unsafe
    {
        unsigned n = PrettyName::fill_size(node.stack_level());
        memset(_name, ' ', n);
        strcpy(_name + n, node.name());

        //if (node.num_recursions()) {
        //    sprintf(_name + strlen(_name), "[x%u]", node.num_recursions());
        //}
        return _name;
    }
    static unsigned name_size(unsigned name_len, unsigned stack_level) {
        unsigned recur_size = 0;
        //if (node.num_recursions()) {
        //    recur_size = 1 + (unsigned)LOG10(node.num_recursions()); // rare case
        //    recur_size += 3; // "[x%u] "
        //}
        unsigned n = PrettyName::fill_size(stack_level);
        return n + name_len + recur_size;
    }

private:
    static unsigned fill_size(unsigned stack_level) {
        unsigned stack_fill_width = std::min(128u, 2 * (1 + stack_level));
        return stack_fill_width;
    }
    static char _name[1024];
};
char PrettyName::_name[];

static
void print_event(
    std::ostream& os,
    const Node& frameTop,
    const Node& node,
    int nameLenMax
)
{
    const char* name = PrettyName::name(node);
    const char* NA = "-";

    double cpuFreqMHz = 2800; // 2.8 GHz
    char totInclP[32], totExclP[32], totInclFPS[32], totInclMHzPerFrame[32], totExclMHzPerFrame[32];
    if (frameTop.realtime_used() > 0) {
        double fpsHead = 1.f / (1e-9f * (frameTop.realtime_used() / frameTop.count()) );
        double inclP = 100.f * node.realtime_used() / frameTop.realtime_used();
        double chldP = 100.f * node.children_realtime_used() / frameTop.realtime_used();
        double exclP = inclP - chldP;
        double inclMHzPerFrame = inclP / 100.f * cpuFreqMHz / fpsHead;
        double exclMHzPerFrame = exclP / 100.f * cpuFreqMHz / fpsHead;
        double inclFPS = 1.f / (1e-9f * node.realtime_used() / frameTop.count());
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
    if (node.realtime_used() > 0) {
        double fpsHead = 1 / (1e-9f * (node.realtime_used() / node.count()) );
        double inclP = 100. * node.realtime_used() / node.realtime_used(); // 100%
        double chldP = 100. * node.children_realtime_used() / node.realtime_used();
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
        double n = ((double)node.count()) / frameTop.count();
        sprintf(numCalls, "%7.2f", n);
    } else {
        sprintf(numCalls, "%7s", NA);
    }
    char cpuP[64];
    if (node.realtime_used() > 0) {
        double inclP = 100.f* node.cpu_used()/ node.realtime_used();
        sprintf(cpuP, "%6.1f", inclP);
    } else {
        sprintf(cpuP, "%6.1f", 0.);
    }

    char s[2048];
#if 0
    fprintf(s, "%3d %-*s %s (%s) %s (%s) %s %s %s\n", 
        node.stack_level(), nameLenMax, name, 
        totInclMHzPerFrame, totInclP, totExclMHzPerFrame, totExclP,
        selfInclFPS, numCalls, cpuP
        );
#else
    sprintf(s, "%3d %-*s %s %s %s %s %s %s\n",
        node.stack_level(), nameLenMax, name,
        totInclP, totExclP, totInclFPS,
        selfInclFPS, numCalls, cpuP
    );
    os << s;
#endif
}

static
void print_tree(
    std::ostream& os,
    const Node& frameTop,
    const Node& node,
    int stackLevelMax,
    int nameWidth)
{
    print_event(os, frameTop, node, nameWidth);

    if (node.stack_level() >= stackLevelMax) {
        return;
    }

    for(auto& child: node.children()) {
        print_tree(os, frameTop, child, stackLevelMax, nameWidth);
    }
}

static
void print_threads(
    std::ostream& os,
    int reportType,
    const std::vector< Node >& threads,
    int stackLevelMax,
    int nameWidth
)
{
    std::string header;
    {
        std::string numThreads = std::to_string(threads.size());
        switch (reportType) {
            case REPORT_THREAD_ROOT:
                header = std::string("Threads summary");
                stackLevelMax = 0;
                break;
            case REPORT_STACK_TOP:
                header = std::string("Stack top");
                stackLevelMax = 0;
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
        header += std::string(" [ ") + std::to_string(threads.size()) + " thread(s) ]";
    }
    print_header(os, header, nameWidth);

    if( threads[0].children().size() == 0 ) {
        return;
    }
    const Node& frameTop = threads[0].children().front();
    for (size_t threadId = 0; threadId < threads.size(); threadId++) {
        for(auto& node : threads[threadId].children()) {
            print_tree(os, frameTop, node, stackLevelMax, nameWidth);
        }
    }

    os << std::endl;
}

std::string Reporter::Report(int reportFlags)
{
    std::vector< Node > threads;
    for (auto& rawEventsItem : _rawThreadMap) {
        auto& rawEvents = rawEventsItem.second;

        auto root = Node::CreateTree(std::move(rawEvents));
        threads.push_back(std::move(root));
    }
    {
        int mainThreadId = -1;
        for(size_t threadId = 0; threadId < threads.size(); threadId++) {
            if (threads[threadId].frame_flag()) {
                mainThreadId = (unsigned)threadId;
                break;
            }
        }
        if (mainThreadId == -1) {
            return "";
        }
        if (mainThreadId != 0) { // set to mt_id = 0
            auto other = std::move(threads[0]);
            auto main = std::move(threads[mainThreadId]);
            threads[0] = main;
            threads[mainThreadId] = other;
        }
    }

    unsigned nameWidth = 0;
    unsigned stackLevelMax = 0;
    {
        for (const auto& node : threads) {
            stackLevelMax = std::max(stackLevelMax, node.stack_level_max());
            nameWidth = std::max(nameWidth, PrettyName::name_size(node.name_len_max(), stackLevelMax));
        }
        stackLevelMax += 1;
    }

#define DEBUG_REPORT 1
#if DEBUG_REPORT
    std::ostream& ss = std::cout;
#else
    std::stringstream ss;
#endif

    if (reportFlags & REPORT_THREAD_ROOT) {
        print_threads(ss, REPORT_THREAD_ROOT, threads, stackLevelMax, nameWidth);
    }
    if (reportFlags & REPORT_DETAILED) {
        print_threads(ss, REPORT_DETAILED, threads, stackLevelMax, nameWidth);
    }

#if DEBUG_REPORT
    return "We're maintaining. Keep calm and don't panic.";
#else
    return ss.str();
#endif

    /*
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
    

    if (reportFlags & REPORT_SUMMARY_NO_REC) {
        print_threads(ss, REPORT_SUMMARY_NO_REC, threadSummaryNoRec, stackLevelMax, nameWidth);
    }
    if (reportFlags & REPORT_SUMMARY) {
        print_threads(ss, REPORT_SUMMARY, threadSummary, stackLevelMax, nameWidth);
    }
*/
    return "";
}

}
