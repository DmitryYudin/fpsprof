#include "thread.h"

#include <inttypes.h>

#include <ostream>
#include <iomanip>
#include <fstream>

namespace fpsprof {

#define FMT_PREFIX "F:"
#define PROP_PREFIX "P:"
#define NAME_PREFIX "N:"
#define THREAD_PREFIX "T:"
#define EVENT_PREFIX "E:"

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

    unsigned fmt = 0;
    bool time_resolution_nsec = false;
    bool measure_process_time = false;
    int thread_id = 0;
    int64_t thread_time = 0;
    std::map<unsigned, std::string> ids;

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
        
        if (0 == strncmp(s, FMT_PREFIX, strlen(FMT_PREFIX))) {
            READ_LONG(s, fmt, goto error_exit)
            if(fmt != 0 && fmt != 1) {
                goto error_exit;
            }
        } else if (0 == strncmp(s, PROP_PREFIX, strlen(PROP_PREFIX))) {
            READ_LONG(s, _penalty_denom, goto error_exit)
            READ_LONGLONG(s, _penalty_self_nsec, goto error_exit)
            READ_LONGLONG(s, _penalty_children_nsec, goto error_exit)
            READ_LONG(s, time_resolution_nsec, goto error_exit)
            READ_LONG(s, measure_process_time, goto error_exit)    
        } else if (0 == strncmp(s, NAME_PREFIX, strlen(NAME_PREFIX))) {
            assert(fmt == 1);

            unsigned id;
            READ_LONG(s, id, goto error_exit)
            READ_NEXT_TOKEN(s, goto error_exit);
            ids[id] = s;
        } else if (0 == strncmp(s, THREAD_PREFIX, strlen(THREAD_PREFIX))) {
            READ_LONG(s, thread_id, goto error_exit)
            READ_LONGLONG(s, thread_time, goto error_exit)
        } else if (0 == strncmp(s, EVENT_PREFIX, strlen(EVENT_PREFIX))) {
            Event event;
            READ_LONG(s, event._frame_flag, goto error_exit)
            READ_LONG(s, event._stack_level, goto error_exit)
            if( fmt == 0) {
                std::string name_str;
                READ_NEXT_TOKEN(s, goto error_exit) name_str = s;
                event._name = hash_event_name(name_str);
            } else if (fmt == 1) {
                unsigned id;
                READ_LONG(s, id, goto error_exit)
                auto it = ids.find(id);
                if(it == ids.end()) {
                    goto error_exit;
                }
                event._name = hash_event_name(it->second);
            }
            int64_t delta_time;
            uint64_t duration_time;
            READ_LONGLONG(s, delta_time, goto error_exit)   
            READ_LONGLONG(s, duration_time, goto error_exit)
            int64_t start_time = thread_time + delta_time;
            int64_t stop_time = start_time + duration_time;

            event._start_nsec = start_time*(time_resolution_nsec ? 100 : 1);
            event._stop_nsec = stop_time*(time_resolution_nsec ? 100 : 1);
            event._measure_process_time = measure_process_time;
            event._cpu_used = 0; //READ_LONGLONG(s, event._cpu_used, goto error_exit)
            operator[](thread_id).push_back(event);

            thread_time += start_time;
        } else {
            goto error_exit;
        }
    }

    assert(_penalty_denom);

    return true;

error_exit:
    fprintf(stderr, "parse fail at line %d\n", count);

    return false;
}

#if _MSC_VER
#define TIME_RESOLUTION_NSEC 1 // 
#else
#define TIME_RESOLUTION_NSEC 0 // Linux, 100nsec resolution
#endif

void ThreadMap::Serialize(std::ostream& os) const
{
    assert(_penalty_denom);

    unsigned fmt = 1;
    bool measure_process_time = false;

    os  << FMT_PREFIX << " "
        << fmt
        << std::endl;

    for (const auto& threadEvents : *this) {
        const auto& events = threadEvents.second;
        for (const auto& event : events) {
            measure_process_time = event.measure_process_time();
        }
    }
    os  << PROP_PREFIX << " "
        << _penalty_denom << " "
        << _penalty_self_nsec << " "
        << _penalty_children_nsec << " "
        << TIME_RESOLUTION_NSEC << " "
        << measure_process_time << " "
        << std::endl;

    std::map<const char*, unsigned> ids;
    if(fmt == 1) {
        for (const auto& threadEvents : *this) {
            const auto& events = threadEvents.second;
            for (const auto& event : events) {
                if(ids.find(event.name()) == ids.end()) {
                    unsigned id = (unsigned)ids.size();
                    ids[ event.name() ] = id;
                    os  << NAME_PREFIX << " "
                        << std::setw(3) << id << " "
                        << event.name()
                        << std::endl;
                }
            }
        }
    }

    for (const auto& threadEvents : *this) {
        int thread_id = threadEvents.first;
        const auto& events = threadEvents.second;
        if(events.empty()) {
            continue;
        }
        const auto& firstEvent = events.front();
        int64_t thread_time = firstEvent.start_nsec() / ( TIME_RESOLUTION_NSEC ? 100 : 1 );
        os  << THREAD_PREFIX << " "
            << std::setw(3) << thread_id << " "
            << thread_time << " "
            << std::endl;

        for (const auto& event : events) {
            char buf[1024];
            int64_t start_time = event.start_nsec() / ( TIME_RESOLUTION_NSEC ? 100 : 1 );
            int64_t stop_time = event.stop_nsec() / ( TIME_RESOLUTION_NSEC ? 100 : 1 );
            int64_t delta_time = start_time - thread_time;
            uint64_t duration_time = stop_time - start_time;
            if(fmt == 0) {
                sprintf(buf, EVENT_PREFIX " %d %u %s %" PRIu64" %" PRIu64"\n"//" %" PRIu64"\n"
                    , event.frame_flag()
                    , event.stack_level()
                    , event.name()
                    , delta_time
                    , duration_time
                    //, event.cpu_used()
                    );
            } else if (fmt == 1) {
                sprintf(buf, EVENT_PREFIX " %d %u %u %" PRIu64" %" PRIu64"\n" //" %" PRIu64"\n"
                    , event.frame_flag()
                    , event.stack_level()
                    , ids[ event.name() ]
                    , delta_time
                    , duration_time
                    //, event.cpu_used()
                    );            
            }
            os << buf;

            thread_time = start_time;
        }
    }
}

}
