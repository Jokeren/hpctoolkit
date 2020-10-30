// -*-Mode: C++;-*-

// * BeginRiceCopyright *****************************************************
//
// $HeadURL$
// $Id$
//
// --------------------------------------------------------------------------
// Part of HPCToolkit (hpctoolkit.org)
//
// Information about sources of support for research and development of
// HPCToolkit is at 'hpctoolkit.org' and in 'README.Acknowledgments'.
// --------------------------------------------------------------------------
//
// Copyright ((c)) 2002-2020, Rice University
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
// * Redistributions of source code must retain the above copyright
//   notice, this list of conditions and the following disclaimer.
//
// * Redistributions in binary form must reproduce the above copyright
//   notice, this list of conditions and the following disclaimer in the
//   documentation and/or other materials provided with the distribution.
//
// * Neither the name of Rice University (RICE) nor the names of its
//   contributors may be used to endorse or promote products derived from
//   this software without specific prior written permission.
//
// This software is provided by RICE and contributors "as is" and any
// express or implied warranties, including, but not limited to, the
// implied warranties of merchantability and fitness for a particular
// purpose are disclaimed. In no event shall RICE or contributors be
// liable for any direct, indirect, incidental, special, exemplary, or
// consequential damages (including, but not limited to, procurement of
// substitute goods or services; loss of use, data, or profits; or
// business interruption) however caused and on any theory of liability,
// whether in contract, strict liability, or tort (including negligence
// or otherwise) arising in any way out of the use of this software, even
// if advised of the possibility of such damage.
//
// ******************************************************* EndRiceCopyright *

//***************************************************************************
//
// File:
//   $HeadURL$
//
// Purpose:
//   [The purpose of this file]
//
// Description:
//   [The set of functions, macros, etc. defined in the file]
//
//***************************************************************************

//************************* System Include Files ****************************

#include <iostream>
#include <fstream>

#include <string>
#include <climits>
#include <cstring>

#include <typeinfo>
#include <unordered_map>

#include <sys/stat.h>

//*************************** User Include Files ****************************

#include <include/uint.h>
#include <include/gcc-attr.h>
#include <include/gpu-metric-names.h>

#include "CallPath-DataFlow.hpp"

using std::string;

#include <lib/prof/CCT-Tree.hpp>
#include <lib/prof/CallPath-Profile.hpp>
#include <lib/prof/Metric-Mgr.hpp>
#include <lib/prof/Metric-ADesc.hpp>

#include <lib/profxml/XercesUtil.hpp>
#include <lib/profxml/PGMReader.hpp>

#include <lib/prof-lean/hpcrun-metric.h>

#include <lib/binutils/LM.hpp>
#include <lib/binutils/VMAInterval.hpp>

#include <lib/xml/xml.hpp>

#include <lib/support/diagnostics.h>
#include <lib/support/Logic.hpp>
#include <lib/support/IOUtil.hpp>
#include <lib/support/StrUtil.hpp>


#include <vector>
#include <queue>
#include <iostream>

#include <boost/graph/graphviz.hpp>
#include <boost/graph/detail/read_graphviz_new.hpp>
#include "redshow_graphviz.h"

namespace Analysis {

namespace CallPath {

typedef std::map<int, redshow_graphviz_node> NodeMap;

typedef std::map<int, std::map<int, std::map<std::string, redshow_graphviz_edge>>> EdgeMap;

static void readGraph(const std::string &file_name, NodeMap &node_map, EdgeMap &edge_map) {
  std::ifstream file(file_name);
  std::stringstream dotfile;

  dotfile << file.rdbuf();
  file.close();

  boost::read_graphviz_detail::parser_result result;
  boost::read_graphviz_detail::parse_graphviz_from_string(dotfile.str(), result, true);

  // read vertice
  for (auto &ninfo : result.nodes) {
    redshow_graphviz_node node;
    
    node.node_id = std::stoi(ninfo.first);

    if (ninfo.second.find("node_type") != ninfo.second.end()) {
      node.type = ninfo.second.at("node_type");
    }

    if (ninfo.second.find("duplicate") != ninfo.second.end()) {
      node.duplicate = ninfo.second.at("duplicate");
    }

    node_map.emplace(node.node_id, node);
  }

  for (auto &einfo : result.edges) {
    int source_id = std::stoi(einfo.source.name);
    int target_id = std::stoi(einfo.target.name);

    redshow_graphviz_edge edge;

    if (einfo.props.find("edge_type") != einfo.props.end()) {
      edge.type = einfo.props.at("edge_type");
    }

    if (einfo.props.find("redundancy") != einfo.props.end()) {
      edge.redundancy = std::stod(einfo.props.at("redundancy"));
    }

    if (einfo.props.find("overwrite") != einfo.props.end()) {
      edge.overwrite = std::stod(einfo.props.at("overwrite"));
    }

    edge_map[source_id][target_id].emplace(edge.type, edge);
  }
}


std::vector<std::string>
static getInlineStack(Prof::Struct::ACodeNode *stmt) {
  std::vector<std::string> st;
  Prof::Struct::Alien *alien = stmt->ancestorAlien();
  if (alien) {
    auto func_name = alien->name();
    auto *stmt = alien->parent();
    if (stmt && stmt->type() == Prof::Struct::ANode::TyAlien) {
      alien = dynamic_cast<Prof::Struct::Alien *>(stmt);
      auto file_name = alien->fileName();
      auto line = std::to_string(alien->begLine());
      auto name = file_name + ":" + line + "\t" + func_name;
      st.push_back(name);

      while (true) {
        stmt = alien->parent();
        if (stmt) {
          alien = stmt->ancestorAlien();
          if (alien) {
            func_name = alien->name();
            stmt = alien->parent();
            if (stmt && stmt->type() == Prof::Struct::ANode::TyAlien) {
              alien = dynamic_cast<Prof::Struct::Alien *>(stmt);
              file_name = alien->fileName();
              line = std::to_string(alien->begLine());
              name = file_name + ":" + line + "\t" + func_name;
              st.push_back(name);
            } else {
              break;
            }
          } else { 
            break;
          }
        } else {
          break;
        }
      }
    }
  } 

  return st;
}


static void matchCCTNode(Prof::CallPath::CCTIdToCCTNodeMap &cctNodeMap, NodeMap &node_map) { 
  // match nodes
  for (auto &iter : node_map) {
    auto &node = iter.second;
    Prof::CCT::ANode *cct = NULL;

    if (cctNodeMap.find(node.node_id) != cctNodeMap.end()) {
      cct = cctNodeMap.at(node.node_id);
    } else if (cctNodeMap.find(-node.node_id) != cctNodeMap.end()) {
      cct = cctNodeMap.at(-node.node_id);
    }

    if (cct) {
      std::stack<Prof::CCT::ProcFrm *> st;
      Prof::CCT::ProcFrm *proc_frm = NULL;
      
      if (cct->type() != Prof::CCT::ANode::TyProcFrm) {
        proc_frm = cct->ancestorProcFrm(); 
      } else {
        proc_frm = dynamic_cast<Prof::CCT::ProcFrm *>(cct);
      }

      while (proc_frm) {
        st.push(proc_frm);
        auto *stmt = proc_frm->parent();
        if (stmt) {
          proc_frm = stmt->ancestorProcFrm();
        } else {
          break;
        }
      };

      while (st.empty() == false) {
        proc_frm = st.top();
        st.pop();
        if (proc_frm->structure()) {
          if (proc_frm->ancestorCall()) {
            auto func_name = proc_frm->structure()->name();
            auto *call = proc_frm->ancestorCall();
            auto *call_strct = call->structure();
            auto line = std::to_string(call_strct->begLine());
            std::string file_name = "Unknown";
            if (call_strct->ancestorAlien()) {
              auto alien_st = getInlineStack(call_strct);
              for (auto &name : alien_st) {
                // Get inline call stack
                node.context.append(name);
                node.context.append("\n");
              }

              auto fname = call_strct->ancestorAlien()->fileName();
              if (fname.find("<unknown file>") == std::string::npos) {
                file_name = fname;
              }
              auto name = file_name + ":" + line + "\t" + func_name;
              node.context.append(name);
              node.context.append("\n");
            } else if (call_strct->ancestorFile()) {
              auto fname = call_strct->ancestorFile()->name();
              if (fname.find("<unknown file>") == std::string::npos) {
                file_name = fname;
              }
              auto name = file_name + ":" + line + "\t" + func_name;
              node.context.append(name);
              node.context.append("\n");
            }
          }
        }
      }
    }
  }
}

static void writeGraph(const std::string &file_name, const NodeMap &node_map, const EdgeMap &edge_map) {
  typedef redshow_graphviz_node VertexProperty;
  typedef redshow_graphviz_edge EdgeProperty;
  typedef boost::adjacency_list<boost::vecS, boost::vecS, boost::directedS, VertexProperty,
                                EdgeProperty> Graph;
  typedef boost::graph_traits<Graph>::vertex_descriptor vertex_descriptor;
  Graph g;
  std::map<int, vertex_descriptor> vertice;
  for (auto &node_iter : node_map) {
    auto &node = node_iter.second;
    auto v = boost::add_vertex(node, g);
    vertice[node.node_id] = v;
  }

  for (auto &edge_iter : edge_map) {
    auto from = vertice.at(edge_iter.first);
    for (auto &node_iter : edge_iter.second) {
      auto to = vertice.at(node_iter.first);
      for (auto &type_iter : node_iter.second) {
        boost::add_edge(from, to, type_iter.second, g);
      }
    }
  }

  boost::dynamic_properties dp;
  dp.property("node_id", boost::get(&VertexProperty::node_id, g));
  dp.property("context", boost::get(&VertexProperty::context, g));
  dp.property("node_type", boost::get(&VertexProperty::type, g));
  dp.property("duplicate", boost::get(&VertexProperty::duplicate, g));
  dp.property("edge_type", boost::get(&EdgeProperty::type, g));
  dp.property("overwrite", boost::get(&EdgeProperty::overwrite, g));
  dp.property("redundancy", boost::get(&EdgeProperty::redundancy, g));

  std::ofstream out(file_name + ".context");
  boost::write_graphviz_dp(out, g, dp);
}


void analyzeDataFlowMain(Prof::CallPath::Profile &prof, const std::vector<std::string> &data_flow_files) {
  auto &cctNodeMap = prof.cctNodeMap();

  for (auto &file : data_flow_files) {
    NodeMap node_map;
    EdgeMap edge_map;

    readGraph(file, node_map, edge_map);
    matchCCTNode(cctNodeMap, node_map);
    writeGraph(file, node_map, edge_map); 
  }
}

} // namespace CallPath

} // namespace Analysis