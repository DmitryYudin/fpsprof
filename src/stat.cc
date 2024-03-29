/*
 * Copyright � 2021 Dmitry Yudin. All rights reserved.
 * Licensed under the Apache License, Version 2.0
 */

#define NOMINMAX

#include "stat.h"
#include "node.h"

#include <assert.h>
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

Stat::Stat(const Node& node, const std::string& path)
    : _name(node.name())
    , _stack_level_min(node.stack_level())
    , _measure_process_time(node.measure_process_time())
    , _realtime_used(node.realtime_used())
    , _cpu_used(node.cpu_used())
    , _count(node.count())
    , _num_recursions(node.num_recursions())
    , _children_realtime_used(node.children_realtime_used())
    , _child_free(node.children().empty())
{
    _paths.push_back(path);
    check_recursion(node);
}

void Stat::add_node(const Node& node, const std::string& path)
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
    _child_free &= node.children().empty();
    _paths.push_back(path);
}

static void collect_statistics(std::list<Stat*>& stats, const Node& node, const std::string& path = "")
{
    bool found = false;
    for(auto stat: stats) {
        if(stat->name() == node.name()) {
            stat->add_node(node, path);
            found = true;
            break;
        }
    }
    if(!found) {
        stats.push_back(new Stat(node, path));
    }

    for(auto& child: node.children()) {
        std::string pathNext;
        if(node.stack_level() >= 0) {
            pathNext = std::string(node.name()) + (path.empty() ? "" : "::") + path;
        }
        collect_statistics(stats, child, pathNext);
    }
}

std::list<Stat*> Stat::CollectStatistics(const Node& node)
{
    std::list<Stat*> stats;
    
    collect_statistics(stats, node);

    for(auto stat: stats) {
        stat->_paths.unique();
        stat->_paths.sort( [](const std::string& a, const std::string& b) {
            return a < b;
        });
    }

    stats.sort( [](const Stat* a, const Stat* b) {
        int64_t a_incl = a->realtime_used();
        int64_t b_incl = b->realtime_used();
        int64_t a_self = a_incl - a->children_realtime_used();
        int64_t b_self = b_incl - b->children_realtime_used();
        return  a_self != b_self ? a_self > b_self : a_incl < b_incl; 
    });
    // if there is a 'root' node we place it at the end of list
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
