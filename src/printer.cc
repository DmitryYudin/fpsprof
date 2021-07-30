/*
 * Copyright � 2021 Dmitry Yudin. All rights reserved.
 * Licensed under the Apache License, Version 2.0
 */

#define NOMINMAX

#include "printer.h"
#include "node.h"
#include "stat.h"

#include <math.h>
#include <algorithm>
#include <string>
#include <iomanip>

#define LOG10(x) (log(x)/log(10))
#define TRIM_STACK_LEVEL(stack_level) std::max(0, int(stack_level))
#define FILL_LEN(stack_level) std::min(128, 2 * TRIM_STACK_LEVEL(stack_level))

namespace fpsprof {

unsigned Printer::_nameColumnWidth = 60;
uint64_t Printer::_frameRealTimeUsed = 0;
unsigned Printer::_frameCount = 0;

void Printer::setNameColumnWidth(unsigned nameLen, unsigned stack_level, unsigned num_recursions)
{
    _nameColumnWidth = FILL_LEN(stack_level) + nameLen  + (num_recursions ? 1 + (unsigned)LOG10(num_recursions) + 3 : 0);
}
void Printer::setFrameCounters(uint64_t realtime_used, unsigned count)
{
    _frameRealTimeUsed = realtime_used;
    _frameCount = count;
}

std::string Printer::formatName(const char *name, unsigned stack_level, unsigned num_recursions)
{
    std::string res(FILL_LEN(stack_level), ' ');
    res += name;
    if (num_recursions) {
        res += std::string("[+") + std::to_string(num_recursions) + "]";
    }
    if(_nameColumnWidth > res.length()) {
        res.append(_nameColumnWidth - res.length(), ' ');
    }
    return res;
}

std::string Printer::formatData(
    const char *name,
    int stack_level,
    unsigned num_recursions,
    uint64_t realtime_used,
    uint64_t children_realtime_used,
    unsigned count,
    uint64_t cpu_used
)
{
    const char* NA = "-";

    char totInclP[32], totExclP[32], totInclFPS[32];
    if (_frameRealTimeUsed > 0) {
        double inclP = 100.f * realtime_used / _frameRealTimeUsed;
        double chldP = 100.f * children_realtime_used / _frameRealTimeUsed;
        double exclP = inclP - chldP;
        double inclFPS = 1.f / (1e-9f * realtime_used / _frameCount);
        sprintf(totInclP, "%6.2f", inclP);
        sprintf(totExclP, "%6.2f", exclP);
        sprintf(totInclFPS, "%10.1f", inclFPS);
    } else {
        sprintf(totInclP, "%6s", NA);
        sprintf(totExclP, "%6s", NA);
        sprintf(totInclFPS, "%10s", NA);
    }

    char callsPerFrame[32];
    if(_frameCount > 0) {
        double n = ((double)count) / _frameCount;
        sprintf(callsPerFrame, "%7.2f", n);
    } else {
        sprintf(callsPerFrame, "%7s", NA);
    }
    char cpuP[32];
    if (realtime_used > 0) {
        double inclP = 100.f* cpu_used / realtime_used;
        sprintf(cpuP, "%6.1f", inclP);
    } else {
        sprintf(cpuP, "%6.1f", 0.);
    }

    std::string res = formatName(name, stack_level, num_recursions);
    res.append(" ").append(totInclP);
    res.append(" ").append(totExclP);
    res.append(" ").append(totInclFPS);
    res.append(" ").append(callsPerFrame);
    res.append(" ").append(cpuP);

    return res;
}

void Printer::printTreeHdr(std::ostream& os, const std::string& name)
{
    const std::string delim = std::string(Printer::_nameColumnWidth + 44, '-');
    os << delim << std::endl;
    os << name << std::endl;
    os << delim << std::endl;

    char s[2048];
    sprintf(s, "%3s %-*s %6s %6s %10s %7s %6s\n", "st", Printer::_nameColumnWidth, "name",
        "inc%", "exc%", "fps", "call/fr", "cpu%");
    os << s;
}
void Printer::printStatHdr(std::ostream& os, const std::string& name)
{
    const std::string delim = std::string(Printer::_nameColumnWidth + 44, '-');
    os << delim << std::endl;
    os << name << std::endl;
    os << delim << std::endl;

    char s[2048];
    sprintf(s, "%3s %-*s %6s %6s %10s %7s %6s\n", "idx", Printer::_nameColumnWidth, "name",
        "inc%", "exc%", "fps", "call/fr", "cpu%");
    os << s;
}
void Printer::printNode(std::ostream& os, const Node& node)
{
    os  << std::setw(3) << node.stack_level() << " " 
        << formatData(node.name(), node.stack_level(), node.num_recursions(), 
                node.realtime_used(), node.children_realtime_used(), node.count(), node.cpu_used())
        << std::endl;
}
void Printer::printStat(std::ostream& os, const Stat& stat, unsigned idx)
{
    os  << std::setw(3) << idx << " " 
        << formatData(stat.name(), 0, stat.num_recursions(), 
                stat.realtime_used(), stat.children_realtime_used(), stat.count(), stat.cpu_used())
        << std::endl;
}
void Printer::printTree(std::ostream& os, const Node& node)
{
    printNode(os, node);
    for(auto& child: node.children()) {
        printTree(os, child);
    }
}

void Printer::printTrees(std::ostream& os, const char *name, const std::vector< Node* >& threads, bool heads_only)
{
    const std::string header = std::string(name) + " [ " + std::to_string(threads.size()) + " thread(s) ]";

    printTreeHdr(os, header);
    for(const auto node: threads) {
        if(heads_only) {
            printNode(os, *node);
        } else {
            printTree(os, *node);
        }
    }
    os << std::endl;
}

void Printer::printStats(std::ostream& os, const char *name, std::vector< std::list< Stat* > >& threads)
{
    const std::string header = std::string(name) + " [ " + std::to_string(threads.size()) + " thread(s) ]";

    printStatHdr(os, header);
    for(const auto& stats: threads) {
        unsigned idx = 1;
        for(const auto stat: stats) {
            printStat(os, *stat, idx++);
        }
    }
    os << std::endl;
}
}