/*
 * Copyright � 2020 Dmitry Yudin. All rights reserved.
 * Licensed under the Apache License, Version 2.0
 */

#pragma once

#include "profpoint.h"
#include "events.h"

#include <string>
#include <list>
#include <map>

namespace fpsprof {

const int REPORT_THREAD_ROOT = (1 << 0);
const int REPORT_STACK_TOP = (1 << 1);
const int REPORT_SUMMARY_NO_REC = (1 << 2);
const int REPORT_SUMMARY = (1 << 3);
const int REPORT_DETAILED = (1 << 4);

class Reporter {
public:
    std::string Serialize() const;
    bool Deserialize(const char* data);

    void AddProfPoints(const std::list<ProfPoint>& marks);

    std::string Report(int reportFlags, int stackLevelMax = -1) const;

private:
    std::map<int, std::list<RawEvent> > _rawThreadMap;
};

}