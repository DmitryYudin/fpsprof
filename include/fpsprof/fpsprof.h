/*
 * Copyright © 2020 Dmitry Yudin. All rights reserved.
 * Licensed under the Apache License, Version 2.0
 */

#pragma once

#include <stdio.h>

#define FPSPROF_SERIALIZE_STREAM(stream)    FPSPROF_serialize_stream(stream);
#define FPSPROF_SERIALIZE_FILE(filename)    FPSPROF_serialize_file(filename);
#define FPSPROF_REPORT_STREAM(stream)       FPSPROF_report_stream(stream);
#define FPSPROF_REPORT_FILE(filename)       FPSPROF_report_file(filename);

#define FPSPROF_START_FRAME(handle, name)   void* handle = FPSPROF_start_frame(name);
#define FPSPROF_START(handle, name)         void* handle = FPSPROF_start(name);
#define FPSPROF_STOP(handle)                FPSPROF_stop(handle);

#define _FPSPROF_JOIN_(x, y) x ## y             // just to overcome C preprocessor
#define _FPSPROF_JOIN(x, y) _FPSPROF_JOIN_(x,y) // issue
#define FPSPROF_SCOPED_FRAME(name)          pfsprof::scoped_frame_t _FPSPROF_JOIN(p,__LINE__)(name);    
#define FPSPROF_SCOPED(name)                pfsprof::scoped_t _FPSPROF_JOIN(p,__LINE__)(name);

#ifdef __cplusplus
extern "C" {
#endif

// All writers set to NULL by default
void FPSPROF_serialize_stream(FILE* fp);
void FPSPROF_serialize_file(const char* filename);
void FPSPROF_report_stream(FILE* fp);
void FPSPROF_report_file(const char* filename);

// The value of 'name' must a string literal
void* FPSPROF_start_frame(const char* name);
void* FPSPROF_start(const char* name);
void FPSPROF_stop(void* handle);

#ifdef __cplusplus
}
#endif

// C++ bindings
#ifdef __cplusplus
namespace pfsprof {
    struct scoped_frame_t {
        explicit scoped_frame_t(const char* name) : _handle(FPSPROF_start_frame(name)) {}
        ~scoped_frame_t() { FPSPROF_stop(_handle); }
    private:
        void* _handle;
    };
    struct scoped_t {
        explicit scoped_t(const char* name) : _handle(FPSPROF_start(name)) {}
        ~scoped_t() { FPSPROF_stop(_handle); }
    private:
        void* _handle;
    };
}
#endif

