/*
 * Copyright © 2021 Dmitry Yudin. All rights reserved.
 * Licensed under the Apache License, Version 2.0
 */

#define NOMINMAX

#include "stat.h"
#include "node.h"

#include "assert.h"
#include <stdexcept>
#include <algorithm>

namespace fpsprof {

static void check_recursion(const Node& node)
{
    auto *name = node.name();
    auto *parent = node.parent();
    while(parent) {
        if(parent->name() == name) {
            throw std::runtime_error("recursion detected on statistics collection stage");
        }
        parent = parent->parent();
    }
}

Stat::Stat(const Node& node)
    : _name(node.name())
    , _stack_level_min(node.stack_level())
    , _measure_process_time(node.measure_process_time())
    , _realtime_used(node.realtime_used())
    , _cpu_used(node.cpu_used())
    , _count(node.count())
    , _num_recursions(node.num_recursions())
    , _children_realtime_used(node.children_realtime_used())
{
    check_recursion(node);
}

void Stat::add_node(const Node& node)
{
    check_recursion(node);

    assert(_name == node.name());
    assert(_measure_process_time == node.measure_process_time());

    _stack_level_min = std::min(_stack_level_min, node.stack_level());
    _realtime_used += node.realtime_used();
    _cpu_used += node.cpu_used();
    _count += node.count();
    _num_recursions += node.num_recursions();
    _children_realtime_used += node.children_realtime_used();
}

static void collect_statistics(std::list<Stat*>& stats, const Node& node)
{
    bool found = false;
    for(auto stat: stats) {
        if(stat->name() == node.name()) {
            stat->add_node(node);
            found = true;
            break;
        }
    }
    if(!found) {
        stats.push_back(new Stat(node));
    }

    for(auto& child: node.children()) {
        collect_statistics(stats, child);
    }
}

std::list<Stat*> Stat::CollectStatistics(const Node& node)
{
    std::list<Stat*> stats;
    
    collect_statistics(stats, node);

    stats.sort( [](const Stat* a, const Stat* b) { 
        return a->realtime_used() - a->children_realtime_used() > b->realtime_used() - b->children_realtime_used(); 
    });
    auto it = std::find_if(stats.begin(), stats.end(), [](const Stat* stat) { 
            return stat->stack_level_min() == -1;
    });
    if(it != stats.end()) {
        Stat *root = *it;
        stats.erase(it);
        stats.push_back(root);
    }
    
    return stats;
}

}
