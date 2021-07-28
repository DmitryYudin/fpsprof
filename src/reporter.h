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

const int REPORT_THREAD_ROOT = (1 << 0);
const int REPORT_STACK_TOP = (1 << 1);
const int REPORT_SUMMARY_NO_REC = (1 << 2);
const int REPORT_SUMMARY = (1 << 3);
const int REPORT_DETAILED = (1 << 4);

class Reporter {
public:
    void Serialize(std::ostream& os) const;
    bool Deserialize(const char* filename);

    void AddProfPoints(std::list<ProfPoint>&& marks);

    // one-shot (destroy data on return)
    std::string Report(int reportFlags);

private:
    std::map<int, std::list<RawEvent> > _rawThreadMap;
};

}
