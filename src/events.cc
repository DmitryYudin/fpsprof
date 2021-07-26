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
#include <set>
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

std::list<Event> Event::Create(std::list<RawEvent>&& rawEvents)
{
    // restore call order: parent is always followed by all it's children
    // root     
    //    0
    //      1
    //        2 ...
    //   0
    std::list<Event> events;
    std::stack<Event*> stack;
    while (!rawEvents.empty()) {
        auto& rawEvent = rawEvents.front();
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

        rawEvents.pop_front();
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
    , _num_recursions(0)
{
}

void EventAcc::AddEvent(const Event& event)
{
    assert(_name == event.name());
    assert(_stack_level == event.stack_level());
    assert(_frame_flag == event.frame_flag());
    assert(_measure_process_time == event.measure_process_time());
    assert(_num_recursions == 0);

    // accumulate independent event: same stack level, same parent
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
    assert(_num_recursions == 0);

    _realtime_sum += eventAcc.realtime_used_sum();
    _cpu_sum += eventAcc.cpu_used_sum();
    _children_realtime_sum += eventAcc.children_realtime_used_sum();
    _children_cpu_sum += eventAcc.children_cpu_used_sum();
    _num_children_max = std::max(_num_children_max, eventAcc.num_children_max());
    _count += eventAcc.count();
}

void EventAcc::AddMergeRecursion(const EventAcc& eventAcc)
{
    assert(self_path() == eventAcc.parent_path());
    assert(_num_children_uniqu == 1);
    assert(0 == eventAcc.num_children_uniqu());
    assert(_frame_flag == eventAcc.frame_flag());
    assert(_measure_process_time == eventAcc.measure_process_time());
    assert(_stack_level + 1 == eventAcc.stack_level());
    assert(_realtime_sum >= eventAcc.realtime_used_sum());
    assert(_num_recursions >= 0);
    assert(0 == eventAcc.children_realtime_used_sum());
    assert(0 == eventAcc.children_cpu_used_sum());

    _count += eventAcc.count();
    _children_realtime_sum = 0;
    _children_cpu_sum = 0;
    _num_children_uniqu = 0;
    _num_recursions = eventAcc.num_recursions() + 1;
}

void EventAcc::update_num_children_uniqu(std::list<EventAcc>& accums)
{
    for (auto it = accums.begin(); it != accums.end();) {
        auto& parent = *it++;
        std::set<std::string> children;
        std::for_each(it, accums.end(),
            [&parent, &children](EventAcc& accum) {
                if (parent.self_path() != accum.parent_path()) {
                    return;
                }
                children.insert(accum.self_path());
            }
        );
        parent._num_children_uniqu = (unsigned)children.size();
    }
}

std::list<EventAcc> EventAcc::Create(std::list<Event>&& events)
{
    std::list<EventAcc> accums;
    if (events.size() == 0) {
        return accums;
    }
    assert(events.front().stack_level() == 0);

    // For each new event find it an a list of already processed events
    while (!events.empty()) {
        auto& event = events.front();
        if(0 == strcmp(event.name(), "CompressCtu_InterRecur")) {
            __debugbreak();
        }

        auto it = std::find_if(accums.begin(), accums.end(),
            [&event](const EventAcc& eventAcc) {
            return eventAcc.parent_path() == event.parent_path()
                && eventAcc.name() == event.name()
                && eventAcc.stack_pos() == event.stack_pos();
            }
        );
        if (it == accums.end()) { 
            // not found, insert new entry at the end of chilidren list (just after parent node)
            // boo(stack_level=N, parent=foo)   //
            // boo(N)                           // collected from different frames
            // boo(N)                           //
            //      boo_child(N+1)
            //      ..
            //      boo_child(N+1)
            auto it = std::find_if(accums.rbegin(), accums.rend(),
                [&event](const EventAcc& eventAcc) {
                return eventAcc.self_path() == event.parent_path();
                }
            );
            EventAcc eventAcc = (EventAcc)event;
            if(it == accums.rend()) { // top most event, only
                accums.push_back(eventAcc);
            } else {
                
                auto it2 = it.base(); // next to 'it'
                while(it2 != accums.end() && it2->parent_path() == event.parent_path()) {
                    it2++;
                }
                accums.insert(it2, eventAcc);
                
                /*
                auto it2 = std::find_if(it.base(), accums.end(),
                    [&event](const EventAcc& eventAcc) {
                    return eventAcc.parent_path() != event.parent_path();
                    }
                );
                */
                accums.insert(it2, eventAcc);
            }
        } else { // found, accumulate it
            it->AddEvent(event);
        }
        events.pop_front();
    }

    update_num_children_uniqu(accums);

    return accums;
}

std::list<EventAcc> EventAcc::CreateSummary(const std::list<EventAcc>& accums)
{
    std::list<EventAcc> accumSummary;
    for (const auto& accum : accums) {
        auto it = std::find_if(accumSummary.begin(), accumSummary.end(),
            [&accum](const EventAcc& eventAcc) {
                return eventAcc.parent_path() == accum.parent_path()
                    && eventAcc.name() == accum.name();
            }
        );
        if (it == accumSummary.end()) {
            accumSummary.push_back(accum);
        } else {
            it->AddEventAccum(accum);
        }
    }

    update_num_children_uniqu(accumSummary);

    return accumSummary;
}

std::list<EventAcc> EventAcc::CreateSummaryNoRec(const std::list<EventAcc>& accums)
{
    // process max deepth first, than move level higher
    std::list<EventAcc> accumSummary;
    std::set<std::string> invalidated;
    for (auto it = accums.begin(); it != accums.end();) {
        auto& accum = *it++;
        if (accumSummary.empty()) {
            accumSummary.push_back(accum);
            continue;
        }
        switch (accum.num_children_uniqu()) {
            default: 
                accumSummary.push_back(accum);
                break;
            case 0:
                if (invalidated.end() == invalidated.find(accum.self_path())) {
                    accumSummary.push_back(accum);
                }
                break;
            case 1: {
                accumSummary.push_back(accum);

                auto& parent = accumSummary.back();
                auto it_child = std::find_if(it, accums.end(),
                    [&parent](const EventAcc& eventAccum) {
                        return parent.self_path() == eventAccum.parent_path();
                    }
                );
                assert(it_child != accums.end());
                const auto& child = *it_child;
                if (0 == child.num_children_uniqu() &&
                    parent.name() == child.name()) {
                    parent.AddMergeRecursion(child);
                    invalidated.insert(child.self_path());
                }
                break;
            }
        }
    }
    if (accumSummary.size() == accums.size()) {
        return accumSummary;
    } else {
        return CreateSummaryNoRec(accumSummary);
    }
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
