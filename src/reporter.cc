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

    const auto frameThread = threadsFull[0];
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

#define DEBUG_REPORT 1
#if DEBUG_REPORT
    std::ostream& ss = std::cout;
#else
    std::stringstream ss;
#endif

    Printer::printTrees(ss, "Threads summary", threadsFull, true);
    Printer::printTrees(ss, "Detailed report", threadsFull);
    Printer::printTrees(ss, "Summary report (no recursion)", threadsNoRecur);
    Printer::printStats(ss, "Function statistics", funcStats);

#if DEBUG_REPORT
    return "We're maintaining. Keep calm and don't panic.";
#else
    return ss.str();
#endif
}

}
