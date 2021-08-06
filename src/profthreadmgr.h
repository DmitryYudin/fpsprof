/*
 * Copyright © 2021 Dmitry Yudin. All rights reserved.
 * Licensed under the Apache License, Version 2.0
 */

#pragma once

#include "profpoint.h"

#include <stdio.h>
#include <list>
#include <string>

namespace fpsprof {

class Reporter;

class IProfThreadMgr {
public:
    // every thread dumps all collected events to attached manager on destroy
    virtual void onProfThreadExit(std::list<ProfPoint>&& marks) = 0;
};

class ProfThreadMgr : public IProfThreadMgr {
public:
    ProfThreadMgr();
    ~ProfThreadMgr();

    void onProfThreadExit(std::list<ProfPoint>&& marks) override;

    void get_penalty(unsigned& penalty_denom, uint64_t& penalty_self_nsec, uint64_t& penalty_children_nsec) {
        penalty_denom = _penalty_denom;
        penalty_self_nsec = _penalty_self_nsec;
        penalty_children_nsec = _penalty_children_nsec;
    }

    void set_serialize_stream(FILE* stream) { _serialize = stream; }
    void set_serialize_file(const char* filename) { _serialize_filename = filename ? filename : ""; }
    void set_report_stream(FILE* stream) { _report = stream; }
    void set_report_file(const char* filename) { _report_filename = filename ? filename : ""; }

private:
    FILE* _serialize = NULL;
    std::string _serialize_filename;
    FILE* _report = NULL;
    std::string _report_filename;

    Reporter *_reporter;

    unsigned _penalty_denom = 0;
    int64_t _penalty_self_nsec = 0;
    int64_t _penalty_children_nsec = 0;
};

}
