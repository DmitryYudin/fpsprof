/*
 * Copyright © 2021 Dmitry Yudin. All rights reserved.
 * Licensed under the Apache License, Version 2.0
 */

#pragma once

#include <stdint.h>
#include <list>

namespace fpsprof {

class Node;

class Stat {
public:
    static std::list<Stat*> CollectStatistics(const Node& node);

    Stat(const Node& node);

#if _MSC_VER // Microsofts STL library can't move list< Stat > using only rvalue reference, i.e. there is no 'operator= (std::list<T&&> &&)' available
    Stat(const Stat&) = default;
#else
    Stat(const Stat&) = delete;
#endif
    Stat(Stat&&) = default;
    Stat& operator= (Stat&) = delete;
    Stat& operator= (Stat&&) = default;

    const char* name() const { return _name; }
    int stack_level_min() const { return _stack_level_min; }
    bool measure_process_time() const { return _measure_process_time; }
    uint64_t realtime_used() const { return _realtime_used; }
    uint64_t cpu_used() const { return _cpu_used; }

    unsigned count() const { return _count; }
    unsigned num_recursions() const { return _num_recursions; }

    uint64_t children_realtime_used() const { return _children_realtime_used; }

    void add_node(const Node& node);

private:
    const char* _name;
    int _stack_level_min;
    bool _measure_process_time;
    uint64_t _realtime_used;
    uint64_t _cpu_used;

    unsigned _count;
    unsigned _num_recursions;

    uint64_t _children_realtime_used;
};

}
