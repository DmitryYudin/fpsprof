/*
 * Copyright © 2021 Dmitry Yudin. All rights reserved.
 * Licensed under the Apache License, Version 2.0
 */

#pragma once

#include "printable.h"

#include <stdint.h>
#include <list>
#include <string>

namespace fpsprof {

class RawEvent;

class Node : public Printable {
public:
    static Node* CreateFull(std::list<RawEvent>&& rawEvents);
    static Node* CreateNoRecur(const Node& root);

    virtual std::string doPrint() const override;

    Node();
    Node(const RawEvent& rawEvent, Node& parent);
    Node(const Node&) = delete;
    Node(Node&&) = default;
    Node& operator= (Node&) = delete;
    Node& operator= (Node&&) = default;

    const char* name() const { return _name; }
    int stack_level() const { return _stack_level; }
    bool frame_flag() const { return _frame_flag; }
    bool measure_process_time() const { return _measure_process_time; }
    unsigned num_children() const { return (unsigned)_children.size(); }
    
    uint64_t realtime_used() const { return _realtime_used; }
    uint64_t cpu_used() const { return _cpu_used; }
    const std::string& parent_path() const { return _parent_path; }
    const std::string& self_path() const { return _self_path; }
    const Node *parent() const { return _parent; }
    unsigned count() const { return _count; }
    unsigned num_recursions() const { return _num_recursions; }
    const std::list<Node>& children() const { return _children; }

    unsigned name_len_max() const;
    unsigned stack_level_max() const;

    uint64_t children_realtime_used() const { uint64_t n = 0; for(auto& child : _children) { n += child.realtime_used(); } return n; }

protected:
    Node& add_child(const RawEvent& rawEvent);
    void merge_children(bool strict);
    void merge_self(Node&& node, bool strict);

    Node* deep_copy(Node* parent) const;
    static void rebase_children(const Node* newHead, Node& oldHead);

    bool collapse_recursion();
    void update_stack_level();

private:
    const char* _name;
    int _stack_level;
    bool _frame_flag;
    bool _measure_process_time;

    uint64_t _realtime_used;
    uint64_t _cpu_used;
    std::string _parent_path;
    std::string _self_path;
    //unsigned _stack_pos;

    Node *_parent;
    unsigned _count;
    unsigned _num_recursions;
    std::list<Node> _children;
};

}
