#pragma once

#include "profpoint.h"
#include "event.h"

#include <list>
#include <vector>
#include <map>
#include <iostream>

namespace fpsprof {

class Node;

struct ThreadMap
{
    // ctors
    void AddRawThread(std::list<ProfPoint>&& marks);
    bool Deserialize(std::ifstream& ifs);

    void Serialize(std::ostream& os) const;

    unsigned reported_penalty_denom() { return _penalty_denom; }
    uint64_t reported_penalty_self_nsec() const { return _penalty_self_nsec; }
    uint64_t reported_penalty_children_nsec() const { return _penalty_children_nsec; }
    const std::map<int, Node* >& threads() const { return _threads; };

private:
    unsigned _penalty_denom = 0;
    uint64_t _penalty_self_nsec = 0;
    uint64_t _penalty_children_nsec = 0;
    std::map<int, Node* > _threads;

    std::map<int, std::list<Event> > _threadEventsMap;
};

}
