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
void Node::merge(Node&& node)
{
    assert(_name == node.name());
    assert(_stack_level == node.stack_level());
    _realtime_used += node.realtime_used();
    _cpu_used += node.cpu_used();
    _count += 1;
    assert(_parent_path == node.parent_path());
    assert(_self_path == node.self_path());
    _children.splice(_children.begin(), std::move(node._children));
}

void Node::merge_stack_level()
{
    auto &it = _children.begin();
    while(it != _children.end()) {
        auto next = it; next++;
        const char* name = it->name();
        auto it2 = std::find_if(next, _children.end(),
            [name](const Node& child) { return name == child.name(); }
        );
        if( it2 == _children.end()) {
            it++;
        } else {
            it->merge(std::move(*it2));
            _children.erase(it2);
        }
    } 

    std::for_each(_children.begin(), _children.end(), [](Node& child) { child.merge_stack_level(); });
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

    root.merge_stack_level();
    /*
    auto &it = root._children.begin();
    while(it != )
    for(auto &it = root._children.begin(); it != root._children.end(); it++) {

    }
    */

/*

        auto it = std::find_if(accums.begin(), accums.end(),
            [&event](const EventAcc& eventAcc) {
            return eventAcc.parent_path() == event.parent_path()
                && eventAcc.name() == event.name()
                && eventAcc.stack_pos() == event.stack_pos();
            }
        );
*/
    return root;
}

}