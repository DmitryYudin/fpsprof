/*
 * Copyright © 2020 Dmitry Yudin. All rights reserved.
 * Licensed under the Apache License, Version 2.0
 */

#pragma once

#include "profpoint.h"

#include <stdint.h>
#include <ostream>
#include <list>
#include <string>

namespace fpsprof {

struct ProfPoint;

// Some common stuff we always want to access + serialize/desirialize
class RawEvent {
public:
    explicit RawEvent(const ProfPoint& pp)
        : _name(pp.name())
        , _stack_level(pp.stack_level())
        , _frame_flag(pp.frame_flag())
        , _measure_process_time(pp.measure_process_time())
        , _start_nsec(pp.realtime_start())
        , _stop_nsec(pp.realtime_stop())
        , _cpu_used(pp.cputime_delta()) {
    }
    RawEvent()
        : _name(NULL) { // this is for deserialization only, since we to not want to use exceptions
    }

    const char* name() const { return _name; }
    int stack_level() const { return _stack_level; }
    bool frame_flag() const { return _frame_flag; }
    bool measure_process_time() const { return _measure_process_time; }
    uint64_t start_nsec() const { return _start_nsec; }
    uint64_t stop_nsec() const { return _stop_nsec; }
    uint64_t cpu_used() const { return _cpu_used; }

    friend std::ostream& operator<<(std::ostream& os, const RawEvent& event);
    char* desirialize(char* s); // ~= strtok, returns NULL on failure

protected:
    std::string make_hash() const {
#ifdef NDEBUG
        return std::to_string((long long)_name) + "."
#else
        return std::string(_name) + "."
#endif
            + std::to_string((int)_stack_level) + "."
            + std::to_string((int)_frame_flag) + "."
            + std::to_string((int)_measure_process_time);
    }

    const char* _name;
    int _stack_level;
    bool _frame_flag;
    bool _measure_process_time;

    uint64_t _start_nsec;
    uint64_t _stop_nsec;
    uint64_t _cpu_used;
};

class EventAcc;

// Keep track of children count and own stack position
class Event : public RawEvent {
public:
    static std::list<Event> Create(const std::list<RawEvent>& rawEvents);

    uint64_t children_realtime_used() const { return _children_realtime_used; }
    uint64_t children_cpu_used() const { return _children_cpu_used; }
    const std::string& parent_path() const { return _parent_path; }
    const std::string& self_path() const { return _self_path; }

    unsigned stack_pos() const { return _stack_pos; }
    unsigned num_children() const { return _num_children; }
    void add_child(const Event& event);

private:
    Event(const RawEvent& rawEvent, Event* parent /* = NULL */);

    uint64_t _children_realtime_used;
    uint64_t _children_cpu_used;
    std::string _parent_path;
    std::string _self_path;
    unsigned _stack_pos;
    unsigned _num_children;

private:
    Event(); //disallowed
};

class EventAcc : public Event {
public:
    static std::list<EventAcc> Create(const std::list<Event>& events);
    static std::list<EventAcc> CreateSummary(const std::list<EventAcc>& accums);

    explicit EventAcc(const Event& event); // casting

    void AddEvent(const Event& event);
    void AddEventAccum(const EventAcc& eventAcc);

    uint64_t realtime_used_sum() const { return _realtime_sum; }
    uint64_t cpu_used_sum() const { return _cpu_sum; }
    uint64_t children_realtime_used_sum() const { return _children_realtime_sum; }
    uint64_t children_cpu_used_sum() const { return _children_cpu_sum; }

    uint64_t realtime_used_avg() const { return _realtime_sum / _count; }
    uint64_t cpu_used_avg() const { return _cpu_sum / _count; }
    uint64_t children_realtime_used_avg() const { return _children_realtime_sum / _count; }
    uint64_t children_cpu_used_avg() const { return _children_cpu_sum / _count; }
    unsigned num_children_max() const { return _num_children_max; }
    unsigned count() const { return _count; }

private:
    unsigned num_children() const; // disallowed, use _max() for accumalated events

    unsigned _count;
    uint64_t _realtime_sum;
    uint64_t _cpu_sum;
    uint64_t _children_realtime_sum;
    uint64_t _children_cpu_sum;
    unsigned _num_children_max;

protected:
    EventAcc(); //disallowed
    // EventAcc(const EventAcc& eventAcc); defaut - ok
};

class RootAcc : public EventAcc {
public:
    static RootAcc* Create(const std::list<EventAcc>& accums, unsigned threadId);

private:
    RootAcc(const EventAcc& eventAcc, unsigned threadId);
    std::string _name_str;

protected:
    RootAcc(); //disallowed
};

}
