/*
 * Copyright © 2021 Dmitry Yudin. All rights reserved.
 * Licensed under the Apache License, Version 2.0
 */

#define NOMINMAX

#include "node.h"
#include "events.h"

#include <assert.h>
#include <stack>
#include <vector>
#include <algorithm>
#include <stdexcept>

namespace fpsprof {

static const char rootNodeName[] = "<root>";
Node::Node()
    : _name(rootNodeName)
    , _stack_level(-1)
    , _frame_flag(false)
    , _measure_process_time(false)
    , _realtime_used(0)
    , _cpu_used(0)
    , _parent_path("")
    , _self_path("")
    , _stack_pos(0)
    , _parent(NULL)
    , _count(1)
    , _num_recursions(0)
{
}
Node::Node(const RawEvent& rawEvent, Node& parent)
    : _name(rawEvent.name())
    , _stack_level(rawEvent.stack_level())
    , _frame_flag(rawEvent.frame_flag())
    , _measure_process_time(rawEvent.measure_process_time())
    , _realtime_used(rawEvent.stop_nsec() - rawEvent.start_nsec())
    , _cpu_used(rawEvent.cpu_used())
    , _parent_path(parent.self_path())
    , _self_path("/" + rawEvent.make_hash() + _parent_path)
    , _stack_pos(parent.num_children())
    , _parent(&parent)
    , _count(1)
    , _num_recursions(0)
{
}

Node& Node::add_child(const RawEvent& rawEvent)
{
    if (rawEvent.stack_level() != _stack_level + 1) {
        assert(!"not a direct child");
    }
    if (rawEvent.measure_process_time() && !_measure_process_time) {
        assert(!"stack level increase resulted in counter change "
            "from thread time to process time");
    }
    _children.emplace_back(rawEvent, *this);
    return _children.back();
}
void Node::merge_self(Node&& node)
{
    assert(_name == node.name());
    assert(_stack_level == node.stack_level());
    assert(_frame_flag == node.frame_flag());
    assert(1 == node.count());
    assert(0 == node.num_recursions());

    _realtime_used += node.realtime_used();
    _cpu_used += node.cpu_used();
    _count += 1;

    assert(_parent_path == node.parent_path());
    assert(_self_path == node.self_path());

    _children.splice(_children.end(), std::move(node._children));
}

void Node::merge_children()
{
    auto it = _children.begin();
    while(it != _children.end()) {
        const char* name = it->name();
        auto cand = it; cand++;
        while( cand != _children.end() ) {
            if(name != cand->name()) {
                cand++;
            } else {
                it->merge_self(std::move(*cand));
                cand = _children.erase(cand);
            }
        }
        it++;
    } 
    for(auto& child: _children) {
        child.merge_children();
    }
}

Node Node::deep_copy(Node* parent)
{
    Node node = Node();

    node._name = _name;
    node._stack_level = _stack_level;
    node._frame_flag = _frame_flag;
    node._measure_process_time = _measure_process_time;

    node._realtime_used = _realtime_used;
    node._cpu_used = _cpu_used;
    node._parent_path = _parent_path;
    node._self_path = _self_path;
    node._stack_pos = _stack_pos;

    node._parent = parent;
    node._count = _count;
    node._num_recursions = _num_recursions;
    for(auto& child: _children) {
        node._children.push_back(child.deep_copy(&node));
    }

    return node;
}

bool Node::collapse_recursion()
{
    for(auto it = _children.begin(); it != _children.begin(); ) {
        if( it->collapse_recursion() ) { // child can modify us and add more items at the end of a '_children' list
            it = _children.erase(it);
        } else {
            it++;
        }
    }
    Node *parent = _parent;
    while(parent) {
        if(parent->name() == _name) {
            parent->_count += _count;
            parent->_num_recursions += 1;
            _children.splice(parent->_children.end(), std::move(_children));
            return true;
        } else {
            parent = parent->_parent;
        }
    }
    return false;
}

Node Node::CreateTree(std::list<RawEvent>&& rawEvents)
{
    Node root;
    std::stack<Node*> stack;
    stack.push(&root);
    while (!rawEvents.empty()) {
        auto& rawEvent = rawEvents.front();
        while (stack.top()->stack_level() >= rawEvent.stack_level()) {
            stack.pop();
            if(stack.empty()) {
                throw std::runtime_error("broken event list");
            }
        }
        stack.push(&stack.top()->Node::add_child(rawEvent));
        rawEvents.pop_front();
    }

    root.merge_children();
    root._frame_flag = std::any_of(root.children().begin(), root.children().end(), [](const Node& child) { return child.frame_flag(); });
    if(root._frame_flag && root.children().size() > 1) {
        throw std::runtime_error("frame thread must have only one entry point");
    }

    Node norec = root.deep_copy(NULL);

    norec.collapse_recursion();

    return root;
}


unsigned Node::name_len_max() const
{
    unsigned n = (unsigned)strlen(_name);
    for (const auto& child : _children) {
        n = std::max(n, child.name_len_max());
    }
    return n;
}
unsigned Node::stack_level_max() const
{
    unsigned n = std::max(0, _stack_level);
    for (const auto& child : _children) {
        n = std::max(n, child.stack_level_max());
    }
    return n;
}

}
