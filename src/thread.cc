#include "thread.h"

#include <inttypes.h>

#include <ostream>
#include <iomanip>
#include <fstream>

namespace fpsprof {

#define STREAM_PREFIX "prof:event:"
#define PENALTY_PREFIX "prof:penalty:"

extern void GetPenalty(unsigned& penalty_denom, uint64_t& penalty_self_nsec, uint64_t& penalty_children_nsec);

typedef std::map<int, std::list<Event> > map;

void ThreadMap::AddRawThread(std::list<ProfPoint>&& marks)
{
    if(_penalty_denom == 0) {
        GetPenalty(_penalty_denom, _penalty_self_nsec, _penalty_children_nsec);
    }

    std::list<Event> events;
    while (!marks.empty()) {
        const auto& pp = marks.front();
        assert(pp.complete());
        events.push_back((Event)pp);
        marks.pop_front();
    }
    if(events.empty()) {
        return;
    }

    int thread_id = (int)this->size();
    operator[](thread_id) = std::move(events);
}

#define READ_NEXT_TOKEN(s, err_action) s = strtok(NULL, " "); if (!s) { err_action; };
#define READ_NUMERIC(s, val, err_action, strtoNum) \
    READ_NEXT_TOKEN(s, err_action) \
    { char* end; val = strtoNum(s, &end, 10); if (*end != '\0') { err_action; } }
#define READ_LONG(s, val, err_action) READ_NUMERIC(s, val, err_action, strtol)
#define READ_LONGLONG(s, val, err_action) READ_NUMERIC(s, val, err_action, strtoll)

static const char* hash_event_name(const std::string& name_str)
{
    static std::map<std::string, const char*> cache;
    auto it = cache.find(name_str);
    const char* name;
    if (it != cache.end()) {
        name = it->second;
    } else {
        if (cache.size() == 0) { // sanitary
            atexit([]() { for (auto& x : cache) { delete[] x.second; }; cache.clear(); });
        }
        char* val = new char[name_str.size() + 1];
        strcpy(val, name_str.c_str());
        cache[name_str] = val;
        name = val;
    }
    return name;
}

bool ThreadMap::Deserialize(std::ifstream& ifs)
{
    assert(_penalty_denom == 0);

    char line[2048];
    unsigned count = 0;
    while (!ifs.getline(line, sizeof(line)).eof()) {
        if (ifs.fail()) {
            return false;
        }
        count++;

        char* s = strtok(line, " ");
        if (!s) {
            continue;
        }
        if (0 != strncmp(s, STREAM_PREFIX, strlen(STREAM_PREFIX))) {
            if (0 == strncmp(s, PENALTY_PREFIX, strlen(PENALTY_PREFIX))) {
                READ_LONG(s, _penalty_denom, goto error_exit)
                READ_LONGLONG(s, _penalty_self_nsec, goto error_exit)
                READ_LONGLONG(s, _penalty_children_nsec, goto error_exit)
            }
            continue;
        }

        int thread_id;
        READ_LONG(s, thread_id, goto error_exit)

        Event event;
        READ_LONG(s, event._frame_flag, goto error_exit)
        READ_LONG(s, event._measure_process_time, goto error_exit)
        READ_LONG(s, event._stack_level, goto error_exit)
        std::string name_str;
        READ_NEXT_TOKEN(s, goto error_exit) name_str = s;
        READ_LONGLONG(s, event._start_nsec, goto error_exit)
        READ_LONGLONG(s, event._stop_nsec, goto error_exit)
        READ_LONGLONG(s, event._cpu_used, goto error_exit)
        event._name = hash_event_name(name_str);
        
        operator[](thread_id).push_back(event);
    }

    assert(_penalty_denom);

    return true;

error_exit:
    fprintf(stderr, "parse fail at line %d\n", count);

    return false;
}

void ThreadMap::Serialize(std::ostream& os) const
{
    assert(_penalty_denom);

    os << PENALTY_PREFIX << " "
        << _penalty_denom << " "
        << _penalty_self_nsec << " "
        << _penalty_children_nsec << " "
        << std::endl;

    for (const auto& threadEvents : *this) {
        int thread_id = threadEvents.first;
        const auto& events = threadEvents.second;
        for (const auto& event : events) {
#if 0
            os  << STREAM_PREFIX << " "
                << std::setw(2) << thread_id << " "

                << std::setw(1) << event.frame_flag() << " "
                << std::setw(1) << event.measure_process_time() << " "
                << std::setw(2) << event.stack_level() << " "
                << std::setw(20) << std::left << event.name() << std::right << " "
                << std::setw(14) << event.start_nsec() << " "
                << std::setw(14) << event.stop_nsec() << " "
                << std::setw(12) << event.cpu_used() << " "
                << std::setw(0)

                << std::endl;
#else
            char buf[1024];
            sprintf(buf, STREAM_PREFIX" %d %d %d %u %s %" PRIu64" %" PRIu64" %" PRIu64"\n", 
                thread_id,
                event.frame_flag(),
                event.measure_process_time(),
                event.stack_level(),
                event.name(),
                event.start_nsec(),
                event.stop_nsec(),
                event.cpu_used()
                );
#endif
            os << buf;
        }
    }
}

}
