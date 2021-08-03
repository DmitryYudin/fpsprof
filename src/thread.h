#pragma once

#include "profpoint.h"
#include "event.h"

#include <list>
#include <map>
#include <iostream>

namespace fpsprof {

struct ThreadMap: public std::map<int, std::list<Event> >
{
    // ctors
    void AddRawThread(std::list<ProfPoint>&& marks);
    bool Deserialize(std::ifstream& ifs);

    void Serialize(std::ostream& os) const;

    unsigned reported_penalty_denom() { return _penalty_denom; }
    uint64_t reported_penalty_self_nsec() const { return _penalty_self_nsec; }
    uint64_t reported_penalty_children_nsec() const { return _penalty_children_nsec; }

private:
    unsigned _penalty_denom = 0;
    uint64_t _penalty_self_nsec = 0;
    uint64_t _penalty_children_nsec = 0;
};

}
