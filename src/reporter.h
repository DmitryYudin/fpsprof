/*
 * Copyright © 2020 Dmitry Yudin. All rights reserved.
 * Licensed under the Apache License, Version 2.0
 */

#pragma once

#include "profpoint.h"
#include "thread.h"

#include <string>
#include <list>
#include <vector>
#include <map>

namespace fpsprof {

class Reporter {
public:
    void AddRawThread(std::list<ProfPoint>&& marks);
    bool Deserialize(const char* filename);

    void Serialize(std::ostream& os) const;

    // one-shot (destroy data on return)
    std::string Report();

private:
    std::string report();
    ThreadMap _threadMap;
};

}
