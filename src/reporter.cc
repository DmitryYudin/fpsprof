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

namespace fpsprof {

void Reporter::AddRawThread(std::list<ProfPoint>&& marks)
{
    _threadMap.AddRawThread(std::move(marks));
}

bool Reporter::Deserialize(const char* filename)
{
    fprintf(stderr, "Reading '%s'\n", filename);

    std::ifstream ifs;
    ifs.open(filename, std::ifstream::in | std::ifstream::binary);
    if (!ifs.is_open()) {
        return false;
    }
    
    return _threadMap.Deserialize(ifs);
}

void Reporter::Serialize(std::ostream& os) const
{
    _threadMap.Serialize(os);
}

void generate_reports(
    ThreadMap& threadMap,
    std::vector< Node* >& threadsFull,
    std::vector< Node* >& threadsNoRecur,
    std::vector< std::list< Stat* > >& funcStatsFull,
    std::vector< std::list< Stat* > >& funcStatsNoRecur
)
{
    unsigned penalty_denom = threadMap.reported_penalty_denom();
    uint64_t penalty_self_nsec = threadMap.reported_penalty_self_nsec();
    uint64_t penalty_children_nsec = threadMap.reported_penalty_children_nsec();
    
    for (auto& threadEvents : threadMap) {
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

std::string Reporter::report()
{
    std::vector< Node* > threadsFull, threadsNoRecur;
    std::vector< std::list<Stat*> > funcStatsFull, funcStatsNoRecur;
    generate_reports(_threadMap, threadsFull, threadsNoRecur, funcStatsFull, funcStatsNoRecur);

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
}

std::string Reporter::Report()
{
    fprintf(stderr, "Generate reports\n");
    try {
        return this->report();
    } catch (std::exception& e) {
        fprintf(stderr, "exception: %s\n", e.what());
        return "";
    }
}
}
