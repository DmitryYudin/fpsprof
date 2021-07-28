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
{
}
Node::Node(const RawEvent& rawEvent, const Node& parent)
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

void Node::update_counters()
{
    if(_parent) {
        _count *= _parent->count();
    }
    for(auto& child: _children) {
        child.update_counters();
    }
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
  //root.update_counters();

    root._frame_flag = std::any_of(root.children().begin(), root.children().end(), [](const Node& child) { return child.frame_flag(); });

    if(root._frame_flag && root.children().size() > 1) {
        throw std::runtime_error("frame thread must have only one entry point");
    }
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
