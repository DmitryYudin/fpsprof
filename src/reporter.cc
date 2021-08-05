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
    std::map< int, Node* >& threadsFull,
    std::map< int, Node* >& threadsNoRecur,
    std::map< int, std::list< Stat* > >& funcStatsFull,
    std::map< int, std::list< Stat* > >& funcStatsNoRecur
)
{
    const std::map<int, Node* >& threads = threadMap.threads();
    unsigned penalty_denom = threadMap.reported_penalty_denom();
    uint64_t penalty_self_nsec = threadMap.reported_penalty_self_nsec();
    uint64_t penalty_children_nsec = threadMap.reported_penalty_children_nsec();

    for (auto& thread : threads) {
        int thread_id = thread.first;
        const auto rootFull = thread.second;

        auto rootNoRecur = Node::CreateNoRecur(*rootFull); fflush(stderr);
        Node::MitigateCounterPenalty(*rootFull, penalty_denom, penalty_self_nsec, penalty_children_nsec);

        auto rootNoRecur2 = Node::CreateNoRecur(*rootFull);
        Node::MitigateCounterPenalty(*rootNoRecur, penalty_denom, penalty_self_nsec, penalty_children_nsec);

        threadsFull[thread_id] = rootFull;
        threadsNoRecur[thread_id] = rootNoRecur;
        funcStatsFull[thread_id] = Stat::CollectStatistics(*rootNoRecur2);
        funcStatsNoRecur[thread_id] = Stat::CollectStatistics(*rootNoRecur);
    }
}

std::string Reporter::report()
{
    if(_threadMap.threads().empty()) {
        return "";
    }
    fprintf(stderr, "Generating reports\n");

    std::map< int, Node* > threadsFull, threadsNoRecur;
    std::map< int, std::list<Stat*> > funcStatsFull, funcStatsNoRecur;
    generate_reports(_threadMap, threadsFull, threadsNoRecur, funcStatsFull, funcStatsNoRecur);

    //const auto frameThread = threadsFull[0];
    const auto frameThread = threadsNoRecur[0];
    if(frameThread->children().empty()) { // root only
        return "";
    }
    {
        const auto& frameNode = frameThread->children().front();
        Printer::setFrameCounters(frameNode.realtime_used(), frameNode.count());
    }
    {
        unsigned stackLevelMax = 0, nameLengthMax = 0;
        for (const auto& thread : threadsFull) {
            auto *node = thread.second;
            stackLevelMax = std::max(stackLevelMax, node->stack_level_max());
            nameLengthMax = std::max(nameLengthMax, node->name_len_max());
        }
        Printer::setNameColumnWidth(nameLengthMax, stackLevelMax, 0);
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
    try {
        return this->report();
    } catch (std::exception& e) {
        fprintf(stderr, "exception: %s\n", e.what());
        return "";
    }
}
}
