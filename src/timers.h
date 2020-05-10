/*
 * Copyright © 2020 Dmitry Yudin. All rights reserved.
 * Licensed under the Apache License, Version 2.0
 */

#pragma once

#include <stdint.h>
#if _WIN32
#ifndef  WIN32_LEAD_AND_MEAN
#define  WIN32_LEAD_AND_MEAN 1
#endif
#include <windows.h>
#else
#include <time.h>
#endif

namespace fpsprof {

namespace timer {

    typedef uint64_t wallclock_t; // comparable
    namespace wallclock {
        __inline wallclock_t timestamp(); // timestamp
        __inline int64_t diff(wallclock_t a, wallclock_t b); // nsec
    }
    namespace thread {
        __inline uint64_t now(); // nsec
    }
    namespace process {
        __inline uint64_t now(); // nsec
    }

    // internals ...
    namespace {
#if _WIN32
        __inline uint64_t FILETIME_TO_NSEC(FILETIME const kernel_time, FILETIME const user_time)
        {
            ULARGE_INTEGER kernel, user;
            kernel.HighPart = kernel_time.dwHighDateTime;
            kernel.LowPart = kernel_time.dwLowDateTime;
            user.HighPart = user_time.dwHighDateTime;
            user.LowPart = user_time.dwLowDateTime;
            return (kernel.QuadPart + user.QuadPart) * 100;
        }
#else
        __inline uint64_t TIMESPEC_TO_NSEC(const struct timespec& ts) {
            return ((uint64_t)1e9) * ts.tv_sec + ts.tv_nsec;
        }
#endif
    }

    namespace wallclock {
        __inline wallclock_t timestamp()
        {
#if _WIN32
            LARGE_INTEGER pc;
            if (!QueryPerformanceCounter(&pc)) {
                return 0;
            }
            return pc.QuadPart;
#else // https://android.googlesource.com/platform/system/core/+/master/libutils/Timers.cpp
            struct timespec ts;
            if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
                return 0;
            }
            return TIMESPEC_TO_NSEC(ts);
#endif
        }
        __inline int64_t diff(wallclock_t a, wallclock_t b)
        {
            wallclock_t diff = a - b;
#if _WIN32
            static double nsec_by_wcfreq = 0;
            if (nsec_by_wcfreq == 0) {
                LARGE_INTEGER qpf;
                if (!QueryPerformanceFrequency(&qpf)) {
                    return 0;
                }
                nsec_by_wcfreq = 1e9/qpf.QuadPart;
            }

            return (int64_t)(diff * nsec_by_wcfreq);
#else
            return diff;
#endif
        }
    }

    namespace thread {
        __inline uint64_t now()
        {
#if _WIN32
            HANDLE handle = GetCurrentThread();
            FILETIME kernel_time, user_time; // 100 nsec
            FILETIME creation_time, exit_time;
            if (!GetThreadTimes(handle, &creation_time, &exit_time,
                &kernel_time, &user_time)) {
                return 0;
            }
            return FILETIME_TO_NSEC(kernel_time, user_time);
#else
            struct timespec ts;
            if (0 != clock_gettime(CLOCK_THREAD_CPUTIME_ID, &ts)) {
                return 0;
            }
            return TIMESPEC_TO_NSEC(ts);
#endif
        }
    }

    namespace process {
        __inline uint64_t now()
        {
#if _WIN32
            HANDLE handle = GetCurrentProcess();
            FILETIME kernel_time, user_time; // 100 nsec
            FILETIME creation_time, exit_time;
            if (!GetProcessTimes(handle, &creation_time, &exit_time,
                &kernel_time, &user_time)) {
                return 0;
            }
            return FILETIME_TO_NSEC(kernel_time, user_time);
#else
            struct timespec ts;
            if (0 != clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &ts)) {
                return 0;
            }
            return TIMESPEC_TO_NSEC(ts);
#endif
        }
    }
}

}
