#include "Galois/Runtime/DistSupport.h"
#include "Galois/Runtime/Context.h"
#include "Galois/Runtime/MethodFlags.h"

#include <iterator>
#include <deque>

namespace Galois {
namespace Graph {

enum class EdgeDirection {Un, Out, InOut};

template<typename NodeTy, typename EdgeTy, EdgeDirection EDir>
class ThirdGraph;

template<typename NHTy>
class GraphNodeBase {
  NHTy nextNode;
  bool active;

protected:
  GraphNodeBase() :active(false) {}

  NHTy& getNextNode() { return nextNode; }

  void serialize(Galois::Runtime::Distributed::SerializeBuffer& s) const {
    s.serialize(nextNode, active);
  }
  void deserialize(Galois::Runtime::Distributed::DeSerializeBuffer& s) {
    s.deserialize(nextNode, active);
  }

  void dump(std::ostream& os) {
    os << "next: ";
    nextNode.dump(os);
    os << " active: ";
    os << active;
  }

public:
  void setActive(bool b) {
    active = b;
  }
};


template<typename NodeDataTy>
class GraphNodeData {
  NodeDataTy data;
  
protected:

  void serialize(Galois::Runtime::Distributed::SerializeBuffer& s) const {
    s.serialize(data);
  }
  void deserialize(Galois::Runtime::Distributed::DeSerializeBuffer& s) {
    s.deserialize(data);
  }

  void dump(std::ostream& os) {
    os << "data: " << data;
  }

public:
  template<typename... Args>
  GraphNodeData(Args&&... args) :data(std::forward<Args...>(args...)) {}
  GraphNodeData() :data() {}

  NodeDataTy& getData() {
    return data;
  }
};

template<>
class GraphNodeData<void> {};

template<typename NHTy, typename EdgeDataTy, EdgeDirection EDir>
class GraphNodeEdges;

template<typename NHTy, typename EdgeDataTy>
class Edge {
  NHTy dst;
  EdgeDataTy val;
public:
  template<typename... Args>
  Edge(const NHTy& d, Args&&... args) :dst(d), val(std::forward<Args...>(args...)) {}

  Edge() {}

  NHTy getDst() { return dst; }
  EdgeDataTy& getValue() { return val; }

  typedef int tt_has_serialize;
  void serialize(Galois::Runtime::Distributed::SerializeBuffer& s) const {
    s.serialize(dst, val);
  }
  void deserialize(Galois::Runtime::Distributed::DeSerializeBuffer& s) {
    s.deserialize(dst, val);
  }

  void dump(std::ostream& os) {
    os << "<{Edge: dst: ";
    dst.dump(os);
    os << " val: ";
    os << val;
    os << "}>";
  }
};

template<typename NHTy>
class Edge<NHTy, void> {
  NHTy dst;
public:
  Edge(const NHTy& d) :dst(d) {}
  Edge() {}

  NHTy getDst() { return dst; }

  typedef int tt_has_serialize;
  void serialize(Galois::Runtime::Distributed::SerializeBuffer& s) const {
    s.serialize(dst);
  }
  void deserialize(Galois::Runtime::Distributed::DeSerializeBuffer& s) {
    s.deserialize(dst);
  }

  void dump(std::ostream& os) {
    os << "<{Edge: dst: ";
    dst.dump();
    os << "}>";
  }
};

template<typename NHTy, typename EdgeDataTy>
class GraphNodeEdges<NHTy, EdgeDataTy, EdgeDirection::Out> {
  typedef Edge<NHTy, EdgeDataTy> EdgeTy;
  typedef std::deque<EdgeTy> EdgeListTy;

  EdgeListTy edges;

protected:
  void serialize(Galois::Runtime::Distributed::SerializeBuffer& s) const {
    s.serialize(edges);
  }
  void deserialize(Galois::Runtime::Distributed::DeSerializeBuffer& s) {
    s.deserialize(edges);
  }
  void dump(std::ostream& os) {
    os << "numedges: " << edges.size();
    for (decltype(edges.size()) x = 0; x < edges.size(); ++x) {
      os << " ";
      edges[x].dump(os);
    }
  }
 public:
  typedef typename EdgeListTy::iterator iterator;

  template<typename... Args>
  iterator createEdge(const NHTy& dst, Args&&... args) {
    return edges.emplace(edges.end(), dst, std::forward<Args...>(args...));
  }

  iterator createEdge(const NHTy& dst) {
    return edges.emplace(edges.end(), dst);
  }

  iterator begin() {
    return edges.begin();
  }

  iterator end() {
    return edges.end();
  }
};

template<typename NHTy, typename EdgeDataTy>
class GraphNodeEdges<NHTy, EdgeDataTy, EdgeDirection::InOut> {
  //FIXME
};

template<typename NHTy, typename EdgeDataTy>
class GraphNodeEdges<NHTy, EdgeDataTy, EdgeDirection::Un> {
  //FIXME
};


#define SHORTHAND Galois::Runtime::Distributed::gptr<GraphNode<NodeDataTy, EdgeDataTy, EDir> >

template<typename NodeDataTy, typename EdgeDataTy, EdgeDirection EDir>
class GraphNode
  : public Galois::Runtime::Lockable,
    public GraphNodeBase<SHORTHAND >,
    public GraphNodeData<NodeDataTy>,
    public GraphNodeEdges<SHORTHAND, EdgeDataTy, EDir>
{
  friend class ThirdGraph<NodeDataTy, EdgeDataTy, EDir>;

  using GraphNodeBase<SHORTHAND >::getNextNode;

public:
  typedef SHORTHAND Handle;

  template<typename... Args>
  GraphNode(Args&&... args) :GraphNodeData<NodeDataTy>(std::forward<Args...>(args...)) {}

  GraphNode() {}

  //serialize
  typedef int tt_has_serialize;
  void serialize(Galois::Runtime::Distributed::SerializeBuffer& s) const {
    GraphNodeBase<SHORTHAND >::serialize(s);
    GraphNodeData<NodeDataTy>::serialize(s);
    GraphNodeEdges<SHORTHAND, EdgeDataTy, EDir>::serialize(s);
  }
  void deserialize(Galois::Runtime::Distributed::DeSerializeBuffer& s) {
    GraphNodeBase<SHORTHAND >::deserialize(s);
    GraphNodeData<NodeDataTy>::deserialize(s);
    GraphNodeEdges<SHORTHAND, EdgeDataTy, EDir>::deserialize(s);
  }
  void dump(std::ostream& os) {
    os << "<{GN: ";
    GraphNodeBase<SHORTHAND >::dump(os);
    os << " ";
    GraphNodeData<NodeDataTy>::dump(os);
    os << " ";
    GraphNodeEdges<SHORTHAND, EdgeDataTy, EDir>::dump(os);
    os << "}>";
  }
};

#undef SHORTHAND

/**
 * A Graph
 *
 * @param NodeTy type of node data (may be void)
 * @param EdgeTy type of edge data (may be void)
 * @param IsDir  bool indicated if graph is directed
 *
*/
template<typename NodeTy, typename EdgeTy, EdgeDirection EDir>
class ThirdGraph { //: public Galois::Runtime::Distributed::DistBase<ThirdGraph> {
  typedef GraphNode<NodeTy, EdgeTy, EDir> gNode;

  struct SubGraphState : public Galois::Runtime::Lockable {
    typename gNode::Handle head;
    Galois::Runtime::Distributed::gptr<SubGraphState> next;
    Galois::Runtime::Distributed::gptr<SubGraphState> master;
    typedef int tt_has_serialize;
    typedef int tt_dir_blocking;
    void serialize(Galois::Runtime::Distributed::SerializeBuffer& s) const {
      s.serialize(head, next, master);
    }
    void deserialize(Galois::Runtime::Distributed::DeSerializeBuffer& s) {
      s.deserialize(head, next, master);
    }
    SubGraphState() :head(), next(), master(this) {}
  };

  SubGraphState localState;

public:
  typedef typename gNode::Handle NodeHandle;

  template<typename... Args>
  NodeHandle createNode(Args&&... args) {
    NodeHandle N(new gNode(std::forward<Args...>(args...)));
    N->getNextNode() = localState.head;
    localState.head = N;
    return N;
  }

  NodeHandle createNode() {
    NodeHandle N(new gNode());
    N->getNextNode() = localState.head;
    localState.head = N;
    return N;
  }
  
  class iterator : public std::iterator<std::forward_iterator_tag, NodeHandle> {
    NodeHandle n;
    Galois::Runtime::Distributed::gptr<SubGraphState> s;
    void next() {
      n = n->getNextNode();
      while (s->next && !n) {
	s = s->next;
	n = s->head;
      }
    }
  public:
    explicit iterator(Galois::Runtime::Distributed::gptr<SubGraphState> ms) :n(), s(ms) {
      n = s->head;
      while (s->next && !n) {
	s = s->next;
	n = s->head;
      }
    }
    iterator() :n(), s() {}
    iterator(const iterator& mit) : n(mit.n), s(mit.s) {}

    NodeHandle& operator*() { return n; }
    iterator& operator++() { next(); return *this; }
    iterator operator++(int) { iterator tmp(*this); operator++(); return tmp; }
    bool operator==(const iterator& rhs) { return n == rhs.n; }
    bool operator!=(const iterator& rhs) { return n != rhs.n; }
  };

  iterator begin() { return iterator(localState.master); }
  iterator end() { return iterator(); }

  class local_iterator : public std::iterator<std::forward_iterator_tag, NodeHandle> {
    NodeHandle n;
    void next() {
      n = n->getNextNode();
    }
  public:
    explicit local_iterator(NodeHandle N) :n(N) {}
    local_iterator() :n() {}
    local_iterator(const local_iterator& mit) : n(mit.n) {}

    NodeHandle& operator*() { return n; }
    local_iterator& operator++() { next(); return *this; }
    local_iterator operator++(int) { local_iterator tmp(*this); operator++(); return tmp; }
    bool operator==(const local_iterator& rhs) { return n == rhs.n; }
    bool operator!=(const local_iterator& rhs) { return n != rhs.n; }
  };

  local_iterator local_begin() { return iterator(localState.head); }
  local_iterator local_end() { return iterator(); }

  ThirdGraph() {}
  typedef int tt_has_serialize;
  // mark the graph as persistent so that it is distributed
  typedef int tt_is_persistent;
  void serialize(Galois::Runtime::Distributed::SerializeBuffer& s) const {
    //This is what is called on the source of a replicating source
    s.serialize(localState.master);
  }
  void deserialize(Galois::Runtime::Distributed::DeSerializeBuffer& s) {
    //This constructs the local node of the distributed graph
    s.deserialize(localState.master);
    localState.next = localState.master->next;
    localState.master->next = &localState;
  }
  
};


} //namespace Graph
} //namespace Galois
