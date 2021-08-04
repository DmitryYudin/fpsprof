/*
 * Copyright © 2021 Dmitry Yudin. All rights reserved.
 * Licensed under the Apache License, Version 2.0
 */

#pragma once

#include <stdint.h>
#include <string>
#include <vector>
#include <list>
#include <iostream>

namespace fpsprof {

class Node;
class Stat;

class Printer {
public:
    static void setNameColumnWidth(unsigned nameLen, unsigned stack_level, unsigned num_recursions);
    static void setFrameCounters(uint64_t realtime_used, unsigned count);
    static void printTrees(std::ostream& os, const char *name, const std::vector< Node* >& threads, bool heads_only = false);
    static void printStats(std::ostream& os, const char *name, std::vector< std::list< Stat* > >& threads);


protected:
    static void printTreeHdr(std::ostream& os, const std::string& name);
    static void printStatHdr(std::ostream& os, const std::string& name);
    static void printNode(std::ostream& os, const Node& node);
    static void printTree(std::ostream& os, const Node& node);
    static void printStat(std::ostream& os, const Stat& stat, unsigned idx);

private:
    static std::string formatData(const char *name, int stack_level, unsigned num_recursions,
        int64_t realtime_used,
        int64_t children_realtime_used,
        unsigned count,
        int64_t cpu_used
    );
    static std::string formatName(const char *name, unsigned stack_level, unsigned num_recursions);

    static void printHdr(std::ostream& os, const std::string& name, const char *firstColumnName);

    static unsigned _nameColumnWidth;
    static uint64_t _frameRealTimeUsed;
    static unsigned _frameCount;
};

}
