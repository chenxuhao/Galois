/*
 * memScalGraph.h
 *
 *  Created on: Mar 4, 2013
 *      Author: nyadav
 */

#ifndef MEMSCALGRAPH_H_
#define MEMSCALGRAPH_H_

#include "Galois/Bag.h"
#include "Galois/Graph/Util.h"
#include "Galois/Runtime/Context.h"
#include "Galois/Runtime/MethodFlags.h"

#include "Galois/gdeque.h"

#include <boost/functional.hpp>
#include <boost/iterator/transform_iterator.hpp>
#include <boost/iterator/filter_iterator.hpp>

#include <algorithm>
#include <map>
#include <set>
#include <vector>

namespace Galois {
//! Parallel graph data structures.
namespace Graph {

template<typename NodeTy, typename EdgeTy, bool Directional>
class MemScalGraph : private boost::noncopyable {
  template<typename T>
  struct first_eq_and_valid {
    T N2;
    first_eq_and_valid(T& n) :N2(n) {}
    template <typename T2>
    bool operator()(const T2& ii) const {
      return ii.first() == N2 && ii.first() && ii.first()->active;
    }
  };
  struct first_not_valid {
    template <typename T2>
    bool operator()(const T2& ii) const { return !ii.first() || !ii.first()->active; }
  };

  struct gNode: public Galois::Runtime::Lockable {
    //! The storage type for an edge
    typedef GraphImpl::EdgeItem<gNode, EdgeTy, Directional> EITy;

    //! The storage type for edges

    typedef Galois::gdeque<EITy,32> EdgesTy;
    typedef typename EdgesTy::iterator iterator;

    EdgesTy edges;
    NodeTy data;
    bool active;

    template<typename... Args>
    gNode(Args&&... args): data(std::forward<Args>(args)...), active(false) { }

    iterator begin() { return edges.begin(); }
    iterator end()   { return edges.end();  }

    void erase(iterator ii) {
      *ii = edges.back();
      edges.pop_back();
    }

    void erase(gNode* N) {
      iterator ii = find(N);
      if (ii != end())
        edges.erase(ii);
    }

    iterator find(gNode* N) {
      return std::find_if(begin(), end(), first_eq_and_valid<gNode*>(N));
    }


    template<typename... Args>
    iterator createEdge(gNode* N, EdgeTy* v, Args&&... args) {
      edges.push_front(EITy(N, v, std::forward<Args>(args)...));
      return edges.begin();
    }

    template<typename... Args>
    iterator createEdgeWithReuse(gNode* N, EdgeTy* v, Args&&... args) {
      //First check for holes
      iterator ii = std::find_if(begin(), end(), first_not_valid());
      if (ii != end()) {
	*ii = EITy(N, v, std::forward<Args>(args)...);
	return ii;
      }
       edges.push_front(EITy(N, v, std::forward<Args>(args)...));
       return edges.begin();
    }
  };

  //The graph manages the lifetimes of the data in the nodes and edges
  typedef Galois::InsertBag<gNode> NodeListTy;
  NodeListTy nodes;

  GraphImpl::EdgeFactory<EdgeTy> edges;

  //Helpers for iterator classes
  struct is_node : public std::unary_function<gNode&, bool>{
    bool operator() (const gNode& g) const { return g.active; }
  };
  struct is_edge : public std::unary_function<typename gNode::EITy&, bool> {
    bool operator()(typename gNode::EITy& e) const { return e.first()->active; }
  };
  struct makeGraphNode: public std::unary_function<gNode&, gNode*> {
    gNode* operator()(gNode& data) const { return &data; }
  };

public:
  //! Graph node handle
  typedef gNode* GraphNode;
  //! Edge data type
  typedef EdgeTy edge_type;
  //! Node data type
  typedef NodeTy node_type;
  //! Edge iterator
  typedef typename boost::filter_iterator<is_edge, typename gNode::iterator> edge_iterator;
  //! Reference to edge data
  typedef typename gNode::EITy::reference edge_data_reference;
  //! Node iterator
  typedef boost::transform_iterator<makeGraphNode,
          boost::filter_iterator<is_node,
                   typename NodeListTy::iterator> > iterator;

private:
  template<typename... Args>
  edge_iterator createEdgeWithReuse(GraphNode src, GraphNode dst, Galois::MethodFlag mflag, Args&&... args) {
    assert(src);
    assert(dst);
    Galois::Runtime::checkWrite(mflag, true);
    Galois::Runtime::acquire(src, mflag);
    typename gNode::iterator ii = src->find(dst);
    if (ii == src->end()) {
      if (Directional) {
	ii = src->createEdgeWithReuse(dst, 0, std::forward<Args>(args)...);
      } else {
	Galois::Runtime::acquire(dst, mflag);
	EdgeTy* e = edges.mkEdge(std::forward<Args>(args)...);
	ii = dst->createEdgeWithReuse(src, e, std::forward<Args>(args)...);
	ii = src->createEdgeWithReuse(dst, e, std::forward<Args>(args)...);
      }
    }
    return boost::make_filter_iterator(is_edge(), ii, src->end());
  }

  template<typename... Args>
  edge_iterator createEdge(GraphNode src, GraphNode dst, Galois::MethodFlag mflag, Args&&... args) {
    assert(src);
    assert(dst);
    Galois::Runtime::checkWrite(mflag, true);
    Galois::Runtime::acquire(src, mflag);
    typename gNode::iterator ii = src->end();
    if (ii == src->end()) {
      if (Directional) {
	ii = src->createEdge(dst, 0, std::forward<Args>(args)...);
      } else {
	Galois::Runtime::acquire(dst, mflag);
	EdgeTy* e = edges.mkEdge(std::forward<Args>(args)...);
	ii = dst->createEdge(src, e, std::forward<Args>(args)...);
	ii = src->createEdge(dst, e, std::forward<Args>(args)...);
      }
    }
    return boost::make_filter_iterator(is_edge(), ii, src->end());
  }

public:
  /**
   * Creates a new node holding the indicated data. Usually you should call
   * {@link addNode()} afterwards.
   */
  template<typename... Args>
  GraphNode createNode(Args&&... args) {
    gNode* N = &(nodes.emplace(std::forward<Args>(args)...));
    N->active = false;
    return GraphNode(N);
  }

  /**
   * Adds a node to the graph.
   */
  void addNode(const GraphNode& n, Galois::MethodFlag mflag = MethodFlag::ALL) {
    Galois::Runtime::checkWrite(mflag, true);
    Galois::Runtime::acquire(n, mflag);
    n->active = true;
  }

  //! Gets the node data for a node.
  NodeTy& getData(const GraphNode& n, Galois::MethodFlag mflag = MethodFlag::ALL) const {
    assert(n);
    Galois::Runtime::checkWrite(mflag, false);
    Galois::Runtime::acquire(n, mflag);
    return n->data;
  }

  //! Checks if a node is in the graph
  bool containsNode(const GraphNode& n, Galois::MethodFlag mflag = MethodFlag::ALL) const {
    assert(n);
    Galois::Runtime::acquire(n, mflag);
    return n->active;
  }

  /**
   * Removes a node from the graph along with all its outgoing/incoming edges
   * for undirected graphs or outgoing edges for directed graphs.
   */
  //FIXME: handle edge memory
  void removeNode(GraphNode n, Galois::MethodFlag mflag = MethodFlag::ALL) {
    assert(n);
    Galois::Runtime::checkWrite(mflag, true);
    Galois::Runtime::acquire(n, mflag);
    gNode* N = n;
    if (N->active) {
      N->active = false;
      if (!Directional && edges.mustDel())
	for (edge_iterator ii = edge_begin(n, MethodFlag::NONE), ee = edge_end(n, MethodFlag::NONE); ii != ee; ++ii)
	  edges.delEdge(ii->second());
      N->edges.clear();
    }
  }


  /**
   * Adds an edge to graph, replacing existing value if edge already exists.
   *
   * Ignore the edge data, let the caller use the returned iterator to set the
   * value if desired.  This frees us from dealing with the void edge data
   * problem in this API
   */
  edge_iterator addEdge(GraphNode src, GraphNode dst, Galois::MethodFlag mflag = MethodFlag::ALL) {
    return createEdgeWithReuse(src, dst, mflag);
  }

  //! Adds and initializes an edge to graph but does not check for duplicate edges
  template<typename... Args>
  edge_iterator addMultiEdge(GraphNode src, GraphNode dst, Galois::MethodFlag mflag, Args&&... args) {
    return createEdge(src, dst, mflag, std::forward<Args>(args)...);
  }

  //! Removes an edge from the graph
  void removeEdge(GraphNode src, edge_iterator dst, Galois::MethodFlag mflag = MethodFlag::ALL) {
    assert(src);
    Galois::Runtime::checkWrite(mflag, true);
    Galois::Runtime::acquire(src, mflag);
    if (Directional) {
      src->erase(dst.base());
    } else {
      Galois::Runtime::acquire(dst->first(), mflag);
      EdgeTy* e = dst->second();
      edges.delEdge(e);
      src->erase(dst.base());
      dst->first()->erase(src);
    }
  }

  //! Finds if an edge between src and dst exists
  edge_iterator findEdge(GraphNode src, GraphNode dst, Galois::MethodFlag mflag = MethodFlag::ALL) {
    assert(src);
    assert(dst);
    Galois::Runtime::acquire(src, mflag);
    return boost::make_filter_iterator(is_edge(), src->find(dst), src->end());
  }

  /**
   * Returns the edge data associated with the edge. It is an error to
   * get the edge data for a non-existent edge.  It is an error to get
   * edge data for inactive edges. By default, the mflag is Galois::NONE
   * because edge_begin() dominates this call and should perform the
   * appropriate locking.
   */
  edge_data_reference getEdgeData(edge_iterator ii, Galois::MethodFlag mflag = MethodFlag::NONE) const {
    assert(ii->first()->active);
    Galois::Runtime::checkWrite(mflag, false);
    Galois::Runtime::acquire(ii->first(), mflag);
    return *ii->second();
  }

  //! Returns the destination of an edge
  GraphNode getEdgeDst(edge_iterator ii) {
    assert(ii->first()->active);
    return GraphNode(ii->first());
  }

  //// General Things ////

  //! Returns an iterator to the neighbors of a node
  edge_iterator edge_begin(GraphNode N, Galois::MethodFlag mflag = MethodFlag::ALL) {
    assert(N);
    Galois::Runtime::acquire(N, mflag);

    if (Galois::Runtime::shouldLock(mflag)) {
      for (typename gNode::iterator ii = N->begin(), ee = N->end(); ii != ee; ++ii) {
	if (ii->first()->active)
	  Galois::Runtime::acquire(ii->first(), mflag);
      }
    }
    return boost::make_filter_iterator(is_edge(), N->begin(), N->end());
  }

  //! Returns the end of the neighbor iterator
  edge_iterator edge_end(GraphNode N, Galois::MethodFlag mflag = MethodFlag::ALL) {
    assert(N);
    // Not necessary; no valid use for an end pointer should ever require it
    //if (shouldLock(mflag))
    //  acquire(N);
    return boost::make_filter_iterator(is_edge(), N->end(), N->end());
  }

  /**
   * An object with begin() and end() methods to iterate over the outgoing
   * edges of N.
   */
  EdgesIterator<MemScalGraph> out_edges(GraphNode N, MethodFlag mflag = MethodFlag::ALL) {
    return EdgesIterator<MemScalGraph>(*this, N, mflag);
  }

  /**
   * Returns an iterator to all the nodes in the graph. Not thread-safe.
   */
  iterator begin() {
    return boost::make_transform_iterator(
           boost::make_filter_iterator(is_node(),
				       nodes.begin(), nodes.end()),
	   makeGraphNode());
  }

  //! Returns the end of the node iterator. Not thread-safe.
  iterator end() {
    return boost::make_transform_iterator(
           boost::make_filter_iterator(is_node(),
				       nodes.end(), nodes.end()),
	   makeGraphNode());
  }

  typedef iterator local_iterator;

  local_iterator local_begin() {
    return boost::make_transform_iterator(
           boost::make_filter_iterator(is_node(),
				       nodes.local_begin(), nodes.local_end()),
	   makeGraphNode());
  }

  local_iterator local_end() {
    return boost::make_transform_iterator(
           boost::make_filter_iterator(is_node(),
				       nodes.local_end(), nodes.local_end()),
	   makeGraphNode());
  }

  /**
   * Returns the number of nodes in the graph. Not thread-safe.
   */
  unsigned int size() {
    return std::distance(begin(), end());
  }

  //! Returns the size of edge data.
  size_t sizeOfEdgeData() const {
    return gNode::EITy::sizeOfSecond();
  }

  MemScalGraph() { }
};

}
}


#endif /* MEMSCALGRAPH_H_ */
