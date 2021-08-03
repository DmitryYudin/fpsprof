/*
 * Copyright © 2020 Dmitry Yudin. All rights reserved.
 * Licensed under the Apache License, Version 2.0
 */

#pragma once

#include "profpoint.h"
#include "events.h"

#include <string>
#include <list>
#include <vector>
#include <map>

namespace fpsprof {

class Reporter {
public:
    void Serialize(std::ostream& os) const;
    bool Deserialize(const char* filename);

    void AddProfPoints(std::list<ProfPoint>&& marks);

    // one-shot (destroy data on return)
    std::string Report();

private:
    unsigned _penalty_denom = 0;
    int64_t _penalty_self_nsec = 0, _penalty_children_nsec = 0;

    std::map<int, std::list<Event> > _threadEventsMap;
};

}
