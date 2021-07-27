#pragma once

#include <list>
#include <string>

namespace fpsprof {

class RawEvent;

class Node {
public:
    static Node CreateTree(std::list<RawEvent>&& rawEvents);

    Node();
    Node(const RawEvent& rawEvent, const Node& parent);

    const char* name() const { return _name; }
    int stack_level() const { return _stack_level; }
    bool measure_process_time() const { return _measure_process_time; }
    unsigned num_children() const { return (unsigned)_children.size(); }
    
    uint64_t realtime_used() const { return _realtime_used; }
    uint64_t cpu_used() const { return _cpu_used; }
    const std::string& parent_path() const { return _parent_path; }
    const std::string& self_path() const { return _self_path; }

protected:
    Node& add_child(const RawEvent& rawEvent);
    void merge_stack_level();
    void merge(Node&& node);

private:
    const char* _name;
    int _stack_level;
    bool _frame_flag;
    bool _measure_process_time;

    uint64_t _realtime_used;
    uint64_t _cpu_used;
    std::string _parent_path;
    std::string _self_path;
    unsigned _stack_pos;

    const Node *_parent;
    unsigned _count;
    std::list<Node> _children;
};


}
