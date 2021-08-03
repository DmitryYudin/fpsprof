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
#include <exception>

#define READ_NEXT_TOKEN(s, err_action) s = strtok(NULL, " "); if (!s) { err_action; };
#define READ_NUMERIC(s, val, err_action, strtoNum) \
    READ_NEXT_TOKEN(s, err_action) \
    { char* end; val = strtoNum(s, &end, 10); if (*end != '\0') { err_action; } }
#define READ_LONG(s, val, err_action) READ_NUMERIC(s, val, err_action, strtol)
#define READ_LONGLONG(s, val, err_action) READ_NUMERIC(s, val, err_action, strtoll)

#define STREAM_PREFIX "prof:event:"
#define PENALTY_PREFIX "prof:penalty:"

namespace fpsprof {

extern void GetPenalty(unsigned& penalty_denom, int64_t& penalty_self_nsec, int64_t& penalty_children_nsec);

void Reporter::Serialize(std::ostream& os) const
{
    unsigned penalty_denom;
    int64_t penalty_self_nsec, penalty_children_nsec;
    GetPenalty(penalty_denom, penalty_self_nsec, penalty_children_nsec);

    os << PENALTY_PREFIX << " "
        << penalty_denom << " "
        << penalty_self_nsec << " "
        << penalty_children_nsec << " "
        << std::endl;

    for (const auto& threadEvents : _threadEventsMap) {
        int thread_id = threadEvents.first;
        const auto& events = threadEvents.second;
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
    fprintf(stderr, "Reading '%s'\n", filename);

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
            if (0 == strncmp(s, PENALTY_PREFIX, strlen(PENALTY_PREFIX))) {
                READ_LONG(s, _penalty_denom, return false)
                READ_LONGLONG(s, _penalty_self_nsec, return false)
                READ_LONGLONG(s, _penalty_children_nsec, return false)
            }
            continue;
        }

        int thread_id;
        READ_LONG(s, thread_id, return false)

        Event event;
        if (!event.desirialize(s)) {
            fprintf(stderr, "parse fail at line %d\n", count);
            return false;
        }
        _threadEventsMap[thread_id].push_back(event);
    }

    return true;
}

void Reporter::AddProfPoints(std::list<ProfPoint>&& marks)
{
    if (marks.empty()) {
        return;
    }

    std::list<Event> events;
    while (!marks.empty()) {
        const auto& pp = marks.front();
        assert(pp.complete());
        events.push_back((Event)pp);
        marks.pop_front();
    }

    int thread_id = (int)_threadEventsMap.size();
    _threadEventsMap[thread_id] = std::move(events);
}

void generate_reports(
    std::map<int, std::list<Event> >& threadEventsMap,
    std::vector< Node* >& threadsFull,
    std::vector< Node* >& threadsNoRecur,
    std::vector< std::list< Stat* > >& funcStatsFull,
    std::vector< std::list< Stat* > >& funcStatsNoRecur,
    unsigned penalty_denom,
    uint64_t penalty_self_nsec,
    uint64_t penalty_children_nsec
    
)
{
    for (auto& threadEvents : threadEventsMap) {
        auto& events = threadEvents.second;

        auto rootFull = Node::CreateFull(std::move(events));
        auto rootNoRecur = Node::CreateNoRecur(*rootFull);

        Node::MitigateCounterPenalty(*rootFull, penalty_denom, penalty_self_nsec, penalty_children_nsec);
        auto rootNoRecur2 = Node::CreateNoRecur(*rootFull);

        Node::MitigateCounterPenalty(*rootNoRecur, penalty_denom, penalty_self_nsec, penalty_children_nsec);

        threadsFull.push_back(rootFull);
        threadsNoRecur.push_back(rootNoRecur);

        funcStatsFull.push_back(Stat::CollectStatistics(*rootNoRecur2));
        funcStatsNoRecur.push_back(Stat::CollectStatistics(*rootNoRecur));
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
        std::swap(threadsNoRecur[0], threadsNoRecur[mainThreadId]);
    }
}

std::string Reporter::Report()
{
    fprintf(stderr, "Generate reports\n");
try {
    std::vector< Node* > threadsFull, threadsNoRecur;
    std::vector< std::list<Stat*> > funcStatsFull, funcStatsNoRecur;
    generate_reports(_threadEventsMap, threadsFull, threadsNoRecur, funcStatsFull, funcStatsNoRecur,
        _penalty_denom, _penalty_self_nsec, _penalty_children_nsec);

    //const auto frameThread = threadsFull[0];
const auto frameThread = threadsNoRecur[0];
    if(frameThread->children().empty()) { // root only
        return "";
    }
    const auto& frameNode = frameThread->children().front();

    {
        unsigned stackLevelMax = 0, nameLengthMax = 0;
        for (const auto& node : threadsFull) {
            stackLevelMax = std::max(stackLevelMax, node->stack_level_max());
            nameLengthMax = std::max(nameLengthMax, node->name_len_max());
        }
        Printer::setNameColumnWidth(nameLengthMax, stackLevelMax, 0);
        Printer::setFrameCounters(frameNode.realtime_used(), frameNode.count());
    }

#define DEBUG_REPORT 0
#if DEBUG_REPORT
    std::ostream& ss = std::cout;
#else
    std::stringstream ss;
#endif
    fprintf(stderr, "Print\n");
    Printer::printTrees(ss, "Threads summary", threadsFull, true);
    Printer::printTrees(ss, "Detailed report", threadsFull);
    Printer::printTrees(ss, "Summary report (no recursion)", threadsNoRecur);
    Printer::printStats(ss, "Function statistics (Full)", funcStatsFull);
    Printer::printStats(ss, "Function statistics (no recursion)", funcStatsNoRecur);

#if DEBUG_REPORT
    return "We're maintaining. Keep calm and don't panic.";
#else
    return ss.str();
#endif
} catch (std::exception& e) {
    fprintf(stderr, "exception: %s\n", e.what());
    return "";
}
}

}
