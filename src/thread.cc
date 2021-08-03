#include "thread.h"

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
                READ_LONG(s, _penalty_denom, return false)
                READ_LONGLONG(s, _penalty_self_nsec, return false)
                READ_LONGLONG(s, _penalty_children_nsec, return false)
            }
            continue;
        }

        int thread_id;
        READ_LONG(s, thread_id, return false)

        Event event;
        if (!event.desirialize(s)) {
            fprintf(stderr, "parse fail at line %d\n", count);
            return false;
        }
        
        operator[](thread_id).push_back(event);
    }

    assert(_penalty_denom);

    return true;
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
        }
    }
}

}
