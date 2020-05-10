/*
 * Copyright © 2020 Dmitry Yudin. All rights reserved.
 * Licensed under the Apache License, Version 2.0
 */

#define NOMINMAX

#include "events.h"
#include "profpoint.h"

#include <string.h>
#include <ostream>
#include <iomanip>
#include <map>
#include <stack>
#include <vector>
#include <algorithm>
#include <assert.h>

namespace fpsprof {

std::ostream& operator<<(std::ostream& os, const RawEvent& event)
{
    os << std::setw(1) << event.frame_flag() << " "
        << std::setw(1) << event.measure_process_time() << " "
        << std::setw(2) << event.stack_level() << " "
        << std::setw(20) << std::left << event.name() << std::right << " "
        << std::setw(14) << event.start_nsec() << " "
        << std::setw(14) << event.stop_nsec() << " "
        << std::setw(12) << event.cpu_used() << " "
        << std::setw(0)
        ;
    return os;
}

#define READ_NEXT_TOKEN(s, err_action) s = strtok(NULL, " "); if (!s) { err_action; };
#define READ_NUMERIC(s, val, err_action, strtoNum) \
    READ_NEXT_TOKEN(s, err_action) \
    { char* end; val = strtoNum(s, &end, 10); if (*end != '\0') { err_action; } }
#define READ_LONG(s, val, err_action) READ_NUMERIC(s, val, err_action, strtol)
#define READ_LONGLONG(s, val, err_action) READ_NUMERIC(s, val, err_action, strtoll)

char* RawEvent::desirialize(char *s)
{
    std::string name_str;
    READ_LONG(s, _frame_flag, return NULL)
    READ_LONG(s, _measure_process_time, return NULL)
    READ_LONG(s, _stack_level, return NULL)
    READ_NEXT_TOKEN(s, return NULL) name_str = s;
    READ_LONGLONG(s, _start_nsec, return NULL)
    READ_LONGLONG(s, _stop_nsec, return NULL)
    READ_LONGLONG(s, _cpu_used, return NULL)

    static std::map<std::string, const char*> cache;
    auto it = cache.find(name_str);
    if (it != cache.end()) {
        _name = it->second;
    } else {
        if (cache.size() == 0) { // sanitary
            atexit([]() { for (auto& x : cache) { delete[] x.second; }; cache.clear(); });
        }
        char* val = new char[name_str.size() + 1];
        strcpy(val, name_str.c_str());
        cache[name_str] = val;
        _name = val;
    }
    return s;
}

Event::Event(const RawEvent& rawEvent, Event* parent)
    : RawEvent(rawEvent)
    , _children_realtime_used(0)
    , _children_cpu_used(0)
    , _parent_path("")
    , _self_path(Event::make_hash())
    , _stack_pos(0)
    , _num_children(0)
{
    // fix bounds
    if (parent) {
        _start_nsec = std::max(_start_nsec, parent->start_nsec());
        _stop_nsec = std::min(_stop_nsec, parent->stop_nsec());

        _parent_path = parent->self_path();
        _stack_pos = parent->num_children();

        parent->add_child(*this);

        // update
        _self_path = _self_path + "/" + _parent_path;
    }
}

void Event::add_child(const Event& child)
{
    if (child.stack_level() != _stack_level + 1) {
        assert(!"not a direct child");
    }
    if (child.measure_process_time() && !_measure_process_time) {
        assert(!"stack level increase resulted in counter change "
            "from thread time to process time");
    }

    _children_realtime_used += child.stop_nsec() - child.start_nsec();
    _children_cpu_used += child.cpu_used();
    _num_children++;
}

std::list<Event> Event::Create(const std::list<RawEvent>& rawEvents)
{
    std::list<Event> events;
    std::stack<Event*> stack;
    for (const auto& rawEvent : rawEvents) {
        while (!stack.empty() && stack.top()->stack_level() >= rawEvent.stack_level()) {
            stack.pop();
        }
        Event* parent = NULL;
        if (!stack.empty()) {
            parent = stack.top();
        }
        Event event(rawEvent, parent);

        events.push_back(event);
        stack.push(&events.back());
    }
    return events;
}

EventAcc::EventAcc(const Event& event)
    : Event(event)
    , _count(1)
    , _realtime_sum(event.stop_nsec() - event.start_nsec())
    , _cpu_sum(event.cpu_used())
    , _children_realtime_sum(event.children_realtime_used())
    , _children_cpu_sum(event.children_cpu_used())
    , _num_children_max(event.num_children())
{
}

void EventAcc::AddEvent(const Event& event)
{
    assert(_name == event.name());
    assert(_stack_level == event.stack_level());
    assert(_frame_flag == event.frame_flag());
    assert(_measure_process_time == event.measure_process_time());

    _realtime_sum += event.stop_nsec() - event.start_nsec();
    _cpu_sum += event.cpu_used();
    _children_realtime_sum += event.children_realtime_used();
    _children_cpu_sum += event.children_cpu_used();

    _num_children_max = std::max(_num_children_max, event.num_children());
    _count += 1;
}

void EventAcc::AddEventAccum(const EventAcc& eventAcc)
{
    if (_stack_level == -1) { // special case
        assert(0 == eventAcc.stack_level());
    } else {
        assert(_name == eventAcc.name());
        assert(_stack_level == eventAcc.stack_level());
    }
    assert(_frame_flag == eventAcc.frame_flag());
    assert(_measure_process_time == eventAcc.measure_process_time());

    _realtime_sum += eventAcc.realtime_used_sum();
    _cpu_sum += eventAcc.cpu_used_sum();
    _children_realtime_sum += eventAcc.children_realtime_used_sum();
    _children_cpu_sum += eventAcc.children_cpu_used_sum();
    _num_children_max = std::max(_num_children_max, eventAcc.num_children_max());
    _count += eventAcc.count();
}

std::list<EventAcc> EventAcc::Create(const std::list<Event>& events)
{
    std::list<EventAcc> accums;
    if (events.size() == 0) {
        return accums;
    }
    assert(events.front().stack_level() == 0);

    for (const auto& event: events) {
        auto it_accum = std::find_if(accums.begin(), accums.end(),
            [&event](const EventAcc& eventAcc) {
            return eventAcc.parent_path() == event.parent_path() \
                && eventAcc.name() == event.name() \
                && eventAcc.stack_pos() == event.stack_pos();
            }
        );
        if (it_accum == accums.end()) {
            accums.push_back((EventAcc)event);
        } else {
            it_accum->AddEvent(event);
        }
    }
    return accums;
}

std::list<EventAcc> EventAcc::CreateSummary(const std::list<EventAcc>& accums)
{
    std::list<EventAcc> accumSummary;
    for (const auto& accum : accums) {
        auto it_accum = std::find_if(accumSummary.begin(), accumSummary.end(),
            [&accum](const EventAcc& eventAcc) {
                return eventAcc.parent_path() == accum.parent_path() \
                    && eventAcc.name() == accum.name();
            }
        );
        if (it_accum == accumSummary.end()) {
            accumSummary.push_back(accum);
        } else {
            it_accum->AddEventAccum(accum);
        }
    }
    return accumSummary;
}

RootAcc::RootAcc(const EventAcc& eventAcc, unsigned threadId)
    : EventAcc(eventAcc)
    , _name_str(std::string("<") + (threadId == 0 ? "main" : std::to_string(threadId)) + ">")
{
    assert(eventAcc.stack_level() == 0);
    _name = _name_str.c_str(); // change
    _stack_level = -1; // drop
}

RootAcc* RootAcc::Create(const std::list<EventAcc>& accums, unsigned threadId)
{
    if (accums.empty()) {
        return NULL;
    }
    RootAcc* rootAcc = NULL;
    for (const auto& eventAccum : accums) {
        if (eventAccum.stack_level() != 0) {
            continue;
        }
        if (rootAcc == NULL) {
            rootAcc = new RootAcc(eventAccum, threadId);
        } else {
            rootAcc->AddEventAccum(eventAccum);
        }
    }
    return rootAcc;
}

}
