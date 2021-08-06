/*
 * Copyright © 2020 Dmitry Yudin. All rights reserved.
 * Licensed under the Apache License, Version 2.0
 */

#pragma once

#include <list>
#include <assert.h>

#include "timers.h"

namespace fpsprof {
// write once forward_list
// + Fast memory allocation
// + Preallocation for a number of items
// - No emplace() with item_t::ctor, only c-style malloc
//   Distructive export to std::list<item> (i.e. item::copy_ctor() )
//      ^ can export to std::list<item*>, but additional code require to handle the ownership
template <class item_t>
class fastwrite_storage_t {
public:
    fastwrite_storage_t() {
        _pages.push_back(_current_page);
    }
    ~fastwrite_storage_t() {
        for (auto& page : _pages) {
            page_free(page);
        }
    }
    void clear() {
        _reading = false;
        _next_idx = 0;
        for (auto& page : _pages) {
            page_free(page);
        }
        _next_page = _pages.end();

        _current_page = page_alloc();
        _pages.push_back(_current_page);
        _alloc_overhead_wc = 0;
    }

    timer::wallclock_t get_overhead_wc() const { return _alloc_overhead_wc; }

    item_t* alloc_item() {
        assert(!_reading);
        item_t* item = &_current_page[_next_idx & page_mask];
        _next_idx++;
        if (0 == (_next_idx & page_mask)) {
            if (_next_page != _pages.end()) {
                _current_page = *_next_page++;
            } else {
                uint64_t wc = timer::wallclock::timestamp();
                _current_page = page_alloc(); // caller is responsible to manage reserve()
                _pages.push_back(_current_page);
                _alloc_overhead_wc += timer::wallclock::timestamp() - wc;
            }
        }
        return item;
    }

    unsigned size() const {
        return _next_idx;
    }

    void reserve(unsigned num_items) {
        assert(!_reading);
        unsigned num_items_free = (unsigned)_pages.size() * (1 << page_bits) - _next_idx;
        if (num_items <= num_items_free) {
            return;
        }
        uint64_t wc = timer::wallclock::timestamp();
        unsigned num_pages_minus1 = (num_items - num_items_free) >> page_bits;
        do {
            item_t* page = page_alloc();
            _pages.push_back(page);
            if (_next_page == _pages.end()) {
                _next_page--; // set to first preallocated
            }
        } while (num_pages_minus1--);
        _alloc_overhead_wc += timer::wallclock::timestamp() - wc;
    }

    std::list<item_t> to_list() { // destructive
        std::list<item_t> out;
#ifndef NDEBUG
        //printf("alloc overhead = %.8f sec\n", timer::wallclock::diff(_alloc_overhead_wc, 0)*1e-9);
#endif
        if (_next_idx == 0 || _reading) { // read once
            return out;
        }

        _reading = true;

        typename std::list<item_t*>::const_iterator read_page = _pages.begin();
        item_t* page = *read_page;
        for (unsigned idx = 0; idx < _next_idx;) {
            const item_t* item = &page[idx & page_mask];
            out.push_back(*item);

            idx++;
            if (0 == (idx & page_mask)) {
                page_free(page);
                if (++read_page != _pages.end()) {
                    page = *read_page;
                }
            }
        }
        while (read_page != _pages.end()) {
            page_free(page);
            if (++read_page != _pages.end()) {
                page = *read_page;
            }
        }
        _pages.clear();
        _next_idx = (unsigned)-1;

        return out;
    }

private:
    static item_t* page_alloc() {
        return (item_t*)malloc(sizeof(item_t) * (1UL << page_bits));
    }
    static void page_free(item_t* page) {
        free(page);
    }
    const static unsigned page_bits = 14;
    const static unsigned page_mask = (1 << page_bits) - 1;

    bool _reading = false;
    unsigned _next_idx = 0;
    std::list<item_t*> _pages;
    typename std::list<item_t*>::iterator _next_page = _pages.end();
    item_t* _current_page = page_alloc();
    timer::wallclock_t _alloc_overhead_wc = 0;
};
}
