/*
 * Copyright © 2020 Dmitry Yudin. All rights reserved.
 * Licensed under the Apache License, Version 2.0
 */

#define NOMINMAX

#include "reporter.h"
#include "node.h"
#include "stat.h"
#include "printer.h"

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
    const std::string delim = std::string(nameLenMax + 44, '-');
    os << delim << std::endl;
    os << header << std::endl;
    os << delim << std::endl;

    char s[2048];
    sprintf(s, "%3s %-*s %6s %6s %10s %7s %6s\n", "st", nameLenMax, "name",
        "inc%", "exc%", "fps", "call/fr", "cpu%");
    os << s;
}

struct PrettyName // unsafe
{
    static const char* name(const char *name, unsigned stack_level, unsigned num_recursions) // unsafe
    {
        unsigned n = PrettyName::fill_size(stack_level);
        memset(_name, ' ', n);
        strcpy(_name + n, name);

        if (num_recursions) {
            sprintf(_name + strlen(_name), "[+%u]", num_recursions);
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

static void print_tree(
    std::ostream& os,
    const Node& node,
    uint64_t frameRealTimeUsed,
    unsigned frameCount,
    int nameWidth
) 
{
    os << Printer::printNode(node) << std::endl;
    for(auto& child: node.children()) {
        print_tree(os, child, frameRealTimeUsed, frameCount, nameWidth);
    }
}


static void print_threads(
    std::ostream& os,
    int reportType,
    const std::vector< Node* >& threads,
    int nameWidth
)
{
    std::string header;
    switch (reportType) {
        case REPORT_THREAD_ROOT:    header = std::string("Threads summary");    break;
        case REPORT_STACK_TOP:      header = std::string("Stack top");  break;
        case REPORT_SUMMARY_NO_REC: header = "Summary report (no recursion)";   break;
        case REPORT_SUMMARY:        header = "Summary report";  break;
        case REPORT_DETAILED:       header = "Detailed report"; break;
    }
    header += std::string(" [ ") + std::to_string(threads.size()) + " thread(s) ]";

    print_header(os, header, nameWidth);

    auto& frameThread = threads[0];
    if( threads[0]->children().size() == 0 ) {
        return;
    }
    const Node& frameTop = frameThread->children().front();
    Printer::setFrameCounters(frameTop.realtime_used(), frameTop.count());
    for(const auto node: threads) {
        uint64_t frameRealTimeUsed = frameTop.realtime_used();
        unsigned frameCount = frameTop.count();
        if(reportType == REPORT_THREAD_ROOT) {
            os << Printer::printNode(frameTop) << std::endl;
        } else {
            print_tree(os, node->children().front(), frameRealTimeUsed, frameCount, nameWidth);
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
    {
        unsigned stackLevelMax = 0, n = 0;
        for (const auto& node : threadsFull) {
            stackLevelMax = std::max(stackLevelMax, node->stack_level_max());
            n = std::max(n, node->name_len_max());
            nameWidth = std::max(nameWidth, PrettyName::name_size(node->name_len_max(), stackLevelMax));
        }
        Printer::setNameColumnWidth(n, stackLevelMax, 0);
    }

#define DEBUG_REPORT 1
#if DEBUG_REPORT
    std::ostream& ss = std::cout;
#else
    std::stringstream ss;
#endif

    if (reportFlags & REPORT_THREAD_ROOT) {
        print_threads(ss, REPORT_THREAD_ROOT, threadsFull, nameWidth);
    }
    if (reportFlags & REPORT_DETAILED) {
        print_threads(ss, REPORT_DETAILED, threadsFull, nameWidth);
    }
    if (reportFlags & REPORT_SUMMARY_NO_REC) {
        print_threads(ss, REPORT_SUMMARY_NO_REC, threadsNoRecur, nameWidth);
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
