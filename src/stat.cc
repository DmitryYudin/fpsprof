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

static void check_stack_level(const std::list<Stat*>& stats, const std::list<Node>& children)
{
    for(auto& stat: stats) {
        int n = 0;
        for(auto& child : children) {
            n += child.name() == stat->name() ? 1 : 0;
            if(n > 1) {
                throw std::runtime_error("recursion detected on statistics collection stage");
            }
        }
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
    //check_stack_level(stats, node.children());

    auto it = std::find_if(stats.begin(), stats.end(), [&](const Stat* stat) {
        return stat->name() == node.name();
    });
    if(it != stats.end()) {
        (*it)->add_node(node);
    } else {
        stats.push_back(new Stat(node));
    }
    /*
    for(auto& child: node.children()) {
        auto it = std::find_if(stats.begin(), stats.end(), [&](const Stat* stat) {
            return stat->name() == child.name();
        });
        if(it != stats.end()) {
            (*it)->add_node(node);
        } else {
            stats.push_back(new Stat(child));
        }
    }
    */
    for(auto& child: node.children()) {
        collect_statistics(stats, child);
    }
}

std::list<Stat*> Stat::CollectStatistics(const Node& node)
{
    std::list<Stat*> stats;
    stats.push_back(new Stat(node));
    
    collect_statistics(stats, node);

    return stats;
}

}
