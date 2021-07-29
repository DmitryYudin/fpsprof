/*
 * Copyright © 2020 Dmitry Yudin. All rights reserved.
 * Licensed under the Apache License, Version 2.0
 */

#define NOMINMAX
#include "reporter.h"
#include "node.h"
#include "stat.h"

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

        if (node.num_recursions()) {
            sprintf(_name + strlen(_name), "[+%u]", node.num_recursions());
        }
        return _name;
    }
    static unsigned name_size(unsigned name_len, unsigned stack_level) {
        unsigned recur_size = 0;
        //if (node.num_recursions()) {
        //    recur_size = 1 + (unsigned)LOG10(node.num_recursions()); // rare case
        //    recur_size += 3; // "[+%u] "
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
void print_func(
    std::ostream& os,
    const Stat& frameTop,
    const std::list< Stat* >& stats,
    int nameLenMax
)
{

}

static
void print_event(
    std::ostream& os,
    const Node& node,
    uint64_t realtime_used,
    unsigned count,
    int nameLenMax
)
{
    const char* name = PrettyName::name(node);
    const char* NA = "-";

    double cpuFreqMHz = 2800; // 2.8 GHz
    char totInclP[32], totExclP[32], totInclFPS[32], totInclMHzPerFrame[32], totExclMHzPerFrame[32];
    if (realtime_used > 0) {
        double fpsHead = 1.f / (1e-9f * (realtime_used / count) );
        double inclP = 100.f * node.realtime_used() / realtime_used;
        double chldP = 100.f * node.children_realtime_used() / realtime_used;
        double exclP = inclP - chldP;
        double inclMHzPerFrame = inclP / 100.f * cpuFreqMHz / fpsHead;
        double exclMHzPerFrame = exclP / 100.f * cpuFreqMHz / fpsHead;
        double inclFPS = 1.f / (1e-9f * node.realtime_used() / count);
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
    if(count > 0) {
        double n = ((double)node.count()) / count;
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
    const Node& node,
    uint64_t realtime_used,
    unsigned count,
    int nameWidth)
{
    print_event(os, node, realtime_used, count, nameWidth);

    for(auto& child: node.children()) {
        print_tree(os, child, realtime_used, count, nameWidth);
    }
}

static
void print_threads(
    std::ostream& os,
    int reportType,
    const std::vector< Node* >& threads,
    int stackLevelMax,
    int nameWidth
)
{
    std::string header;

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

    print_header(os, header, nameWidth);

    auto& frameThread = threads[0];
    if( threads[0]->children().size() == 0 ) {
        return;
    }
    const Node& frameTop = frameThread->children().front();
    for(auto& nodes: threads) {
        uint64_t realtime_used = frameTop.realtime_used();
        unsigned count = frameTop.count();
        if(reportType == REPORT_THREAD_ROOT) {
            print_event(os, frameTop, realtime_used, count, nameWidth);
            if(reportType == REPORT_THREAD_ROOT) {
                continue;
            }
            for(auto& child: nodes->children()) {
                print_event(os, child, realtime_used, count, nameWidth);
            }
        }
    }
    os << std::endl;
}

static
void print_funcStats(
    std::ostream& os,
    const std::vector< std::list< Stat* > >& funcStats,
    int nameWidth
)
{
    std::string header = "Summary report";
    header += std::string(" [ ") + std::to_string(funcStats.size()) + " thread(s) ]";

    const Stat& frameTop = *funcStats[0].front();
    for(auto& stats: funcStats) {
        print_func(os, frameTop, stats, nameWidth);
        for(auto& stat: stats) {
        }
    }
    /*
    for (size_t threadId = 0; threadId < threads.size(); threadId++) {
        for(auto& node : threads[threadId]->children()) {
            print_tree(os, frameTop, node, stackLevelMax, nameWidth);
        }
    }*/

    os << std::endl;
}

void generate_reports(
    std::map<int, std::list<RawEvent> >& rawThreadMap,
    std::vector< Node* >& threadsFull,
    std::vector< Node* >& threadsNoRecur,
    std::vector< std::list< Stat* > >& funcStats
)
{
    for (auto& rawEventsItem : rawThreadMap) {
        auto& rawEvents = rawEventsItem.second;

        auto rootFull = Node::CreateFull(std::move(rawEvents));
        auto rootNoRecur = Node::CreateNoRecur(*rootFull);
        auto rootStat = Stat::CollectStatistics(*rootNoRecur);
        threadsFull.push_back(rootFull);
        threadsNoRecur.push_back(rootNoRecur);
        funcStats.push_back(rootStat);
    }

    int mainThreadId = -1;
    for(size_t threadId = 0; threadId < threadsFull.size(); threadId++) {
        if (threadsFull[threadId]->frame_flag()) {
            mainThreadId = (unsigned)threadId;
            break;
        }
    }
    if (mainThreadId == -1) {
        throw std::runtime_error("no main thread found");
    }
    if (mainThreadId != 0) { // set to mt_id = 0
        std::swap(threadsFull[0], threadsFull[mainThreadId]);
    }
}

std::string Reporter::Report(int reportFlags)
{
    std::vector< Node* > threadsFull, threadsNoRecur;
    std::vector< std::list<Stat*> > funcStats;
    generate_reports(_rawThreadMap, threadsFull, threadsNoRecur, funcStats);

    unsigned nameWidth = 0;
    unsigned stackLevelMax = 0;
    {
        for (const auto& node : threadsFull) {
            stackLevelMax = std::max(stackLevelMax, node->stack_level_max());
            nameWidth = std::max(nameWidth, PrettyName::name_size(node->name_len_max(), stackLevelMax));
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
        print_threads(ss, REPORT_THREAD_ROOT, threadsFull, stackLevelMax, nameWidth);
    }
    if (reportFlags & REPORT_DETAILED) {
        print_threads(ss, REPORT_DETAILED, threadsFull, stackLevelMax, nameWidth);
    }
    if (reportFlags & REPORT_SUMMARY_NO_REC) {
        print_threads(ss, REPORT_SUMMARY_NO_REC, threadsNoRecur, stackLevelMax, nameWidth);
    }
    if (reportFlags & REPORT_SUMMARY) {
        print_funcStats(ss, funcStats, nameWidth);
    }
#if DEBUG_REPORT
    return "We're maintaining. Keep calm and don't panic.";
#else
    return ss.str();
#endif
}

}
