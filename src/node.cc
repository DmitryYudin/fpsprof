/*
 * Copyright © 2021 Dmitry Yudin. All rights reserved.
 * Licensed under the Apache License, Version 2.0
 */

#define NOMINMAX

#include "node.h"
#include "event.h"

#include <assert.h>
#include <string.h>
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
#ifndef NDEBUG
    , _parent_path("")
    , _self_path("")
#endif
    , _parent(NULL)
    , _count(0)
    , _num_recursions(0)
    , _has_penalty(true)
    , _num_removed(0)
    , _count_norec(0)
{
}
Node::Node(const Event& event, Node& parent)
    : _name(event.name())
    , _stack_level(event.stack_level())
    , _frame_flag(event.frame_flag())
    , _measure_process_time(event.measure_process_time())
    , _realtime_used(event.stop_nsec() - event.start_nsec())
    , _cpu_used(event.cpu_used())
#ifndef NDEBUG
    , _parent_path(parent.self_path())
    , _self_path("/" + make_hash() + _parent_path)
#endif
    , _parent(&parent)
    , _count(1)
    , _num_recursions(0)
    , _has_penalty(true)
    , _num_removed(0)
    , _count_norec(1)
{
}

#ifndef NDEBUG
std::string Node::make_hash() const {
    std::string name;

    static std::map<const char*, unsigned> _ptr_hash;
    if (_ptr_hash.find(_name) == _ptr_hash.end()) {
        _ptr_hash[_name] = (unsigned)_ptr_hash.size();
    }
    unsigned idx = _ptr_hash[_name];
    name = std::to_string(idx);
    //name = std::string(_name);
    return name + "."
        + std::to_string((int)_stack_level) + "."
        + std::to_string((int)_frame_flag) + "."
        + std::to_string((int)_measure_process_time);
}
#endif

Node& Node::add_child(const Event& event)
{
    if (event.stack_level() != _stack_level + 1) {
        assert(!"not a direct child");
    }
    if (event.measure_process_time() && !_measure_process_time) {
        assert(!"stack level increase resulted in counter change "
            "from thread time to process time");
    }
    _children.emplace_back(event, *this);

    return _children.back();
}
void Node::merge_self(Node&& node, bool strict)
{
    assert(_name == node.name());
    assert(_stack_level == node.stack_level());
    assert(_frame_flag == node.frame_flag());
    assert(_has_penalty == node.has_penalty());
    
    if(strict) {
        assert(1 == node.count());
        assert(0 == node.num_recursions());
    }
    _realtime_used += node.realtime_used();
    _cpu_used += node.cpu_used();
    _count += node.count();
    _num_recursions = std::max(_num_recursions, node.num_recursions());

    if(strict) {
#ifndef NDEBUG
        assert(_parent_path == node.parent_path());
        assert(_self_path == node.self_path());
#endif
    }
    rebase_children(this, node);
    _children.splice(_children.end(), std::move(node._children));

    _num_removed += node._num_removed;
    _count_norec += node._count_norec;
}

void Node::merge_children(bool strict)
{
    //assert(_has_penalty == true);

    auto it = _children.begin();
    while(it != _children.end()) {
        const char* name = it->name();
        auto cand = it; cand++;
        while( cand != _children.end() ) {
            if(name != cand->name()) {
                cand++;
            } else {
                it->merge_self(std::move(*cand), strict);
                cand = _children.erase(cand);
            }
        }
        it++;
    } 
    for(auto& child: _children) {
        child.merge_children(strict);
    }
}

void Node::rebase_children(const Node* newHead, Node& oldHead)
{
    for(auto& child: oldHead._children) {
        child._parent = const_cast<Node*>(newHead);
    }
}
Node* Node::deep_copy(Node* parent) const
{
    Node *node = new Node();

    node->_name = _name;
    node->_stack_level = _stack_level;
    node->_frame_flag = _frame_flag;
    node->_measure_process_time = _measure_process_time;

    node->_realtime_used = _realtime_used;
    node->_cpu_used = _cpu_used;
#ifndef NDEBUG
    node->_parent_path = _parent_path;
    node->_self_path = _self_path;
#endif
    node->_parent = parent;
    node->_count = _count;
    node->_num_recursions = _num_recursions;
    node->_has_penalty = _has_penalty;
    node->_num_removed = _num_removed;
    node->_count_norec = _count_norec;
    /*
    std::list<Node> newChildren;
    for(auto& child: _children) {
        newChildren.splice(newChildren.end(), std::move(*child.deep_copy(node)));
    }
    for(auto& child: _children) {
        rebase_children(&child, child);
    }
    */
    for(auto& child: _children) {
        {
            auto *newChild = child.deep_copy(node);
            node->_children.push_back(std::move(*newChild)); // change 'newChild' displacenment
        }
        {
            auto& newChild = node->_children.back();
            rebase_children(&newChild, newChild);
        }
    }
    return node;
}

void Node::update_stack_level()
{
    _stack_level = _parent ? _parent->_stack_level + 1 : -1;
    for(auto& child: _children) {
        child.update_stack_level();
    }
}

bool Node::collapse_recursion()
{
    for(auto it = _children.begin(); it != _children.end(); ) {
        // child will modify us and add more items at the end of a '_children' list
        if( it->collapse_recursion() ) {
            it = _children.erase(it);
        } else {
            it++;
        }
    }
#if 0
    while(parent) {
        if(parent->name() == _name) {
            parent->_count += _count;
            parent->_num_recursions += _num_recursions + 1;
            rebase_children(parent, *this);
            parent->_children.splice(parent->_children.end(), std::move(_children));
            return true;
        } else {
            parent = parent->_parent;
        }
    }
    return false;
#else
    Node *parent = _parent;
    Node *parent_recur = NULL;
    while(parent) {
        if(parent->name() == _name) {
            parent_recur = parent;
            break;
        } else {
            parent = parent->_parent;
        }
    }
    if(!parent_recur) {
        return false;
    }

    uint64_t self_realtime_used = realtime_used() - children_realtime_used();
    uint64_t self_cpu_used = cpu_used() - children_cpu_used(); // TODO: might be negative (???)
    parent = _parent;
    rebase_children(parent, *this);
    parent->_children.splice(parent->_children.end(), std::move(_children));
    parent->_num_removed += _num_removed + _count_norec;

    while (parent != parent_recur) {
        parent->_realtime_used -= self_realtime_used;
        parent->_cpu_used -= self_cpu_used;
        parent = parent->_parent;
    }
    parent_recur->_count += _count;
    parent_recur->_num_recursions += _num_recursions + 1;

    return true;
#endif
}

Node* Node::CreateFull(std::list<Event>&& events)
{
    Node *root = new Node();
    std::stack<Node*> stack;
    stack.push(root);
    while (!events.empty()) {
        auto& event = events.front();
        while (stack.top()->stack_level() >= event.stack_level()) {
            stack.pop();
            if(stack.empty()) {
                throw std::runtime_error("broken event list");
            }
        }
        stack.push(&stack.top()->Node::add_child(event));
        events.pop_front();
    }

    root->merge_children(true);
    for(const auto& child: root->children()) {
        root->_frame_flag |= child.frame_flag();
        root->_realtime_used += child.realtime_used();
        root->_cpu_used += child.cpu_used();
        root->_count += child.count();
    }
    if(root->frame_flag() && root->children().size() > 1) {
        throw std::runtime_error("frame thread must have only one entry point");
    }

    return root;
}

Node* Node::CreateNoRecur(const Node& node)
{
    Node *norec = node.deep_copy(NULL);

    norec->collapse_recursion();
    norec->update_stack_level();
    norec->merge_children(false);
    return norec;
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

unsigned Node::mitigate_counter_penalty(unsigned penalty_denom, uint64_t penalty_self_nsec, uint64_t penalty_children_nsec)
{
    unsigned numChildrenFull = 0;
    for(auto& child: _children) {
        numChildrenFull += child.mitigate_counter_penalty(penalty_denom, penalty_self_nsec, penalty_children_nsec);
    }
    uint64_t decrement_realtime_used = penalty_children_nsec*(numChildrenFull + _num_removed)/penalty_denom + penalty_self_nsec*(_count)/penalty_denom;

    if(_parent == NULL) { // root
        _realtime_used = children_realtime_used();
    } else {
        _realtime_used = _realtime_used < decrement_realtime_used ? 0 : _realtime_used - decrement_realtime_used;

        uint64_t children_realtime_used_ = children_realtime_used();
        if(_realtime_used < children_realtime_used_) {
            _realtime_used = children_realtime_used_;
        }
    }
    _has_penalty = false;

    return numChildrenFull + _count_norec + _num_removed;
}
void Node::MitigateCounterPenalty(Node& root, unsigned penalty_denom, uint64_t penalty_self_nsec, uint64_t penalty_children_nsec)
{
    root.mitigate_counter_penalty(penalty_denom, penalty_self_nsec, penalty_children_nsec);
}
}
