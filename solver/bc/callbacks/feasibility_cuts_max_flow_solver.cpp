#include <global.h>
#include <solver/bc/callbacks/feasibility_cuts_max_flow_solver.h>

#include <boost/graph/boykov_kolmogorov_max_flow.hpp>
#include <boost/property_map/property_map.hpp>

#include <chrono>
#include <ctime>
#include <ratio>

std::vector<IloRange> FeasibilityCutsMaxFlowSolver::separate_feasibility_cuts(const Graph& g, const Graph& gr, const ch::solution& sol, const IloNumVarArray& x, double eps) {
    using namespace std::chrono;
        
    auto n = g.g[graph_bundle].n;
    vi_t vi, vi_end;
    ei_t ei, ei_end;

    auto cuts = std::vector<IloRange>();
    auto xvals = sol.x;

    auto capacity = std::vector<double>(num_edges(gr.g), 0);
    auto reverse_edge = std::vector<edge_t>(num_edges(gr.g));

    for(std::tie(ei, ei_end) = edges(gr.g); ei != ei_end; ++ei) {
        auto i = gr.g[source(*ei, gr.g)].id;
        auto j = gr.g[target(*ei, gr.g)].id;
        auto e = gr.g[*ei].id;

        capacity[e] = xvals[i][j];
        reverse_edge[e] = edge(target(*ei, gr.g), source(*ei, gr.g), gr.g).first;
    }
    
    auto already_checked_cycle = std::vector<int>();

    for(auto i = 1u; i <= (unsigned int)n; i++) {
        auto residual_capacity_prec = std::vector<double>(num_edges(gr.g), 0);
        auto residual_capacity_cycles = std::vector<double>(num_edges(gr.g), 0);
        auto colour_prec = std::vector<int>(num_vertices(gr.g), 0);
        auto colour_cycles = std::vector<int>(num_vertices(gr.g), 0);
        auto source_v_prec = vertex_t(), sink_v_prec = vertex_t(), source_v_cycles = vertex_t(), sink_v_cycles = vertex_t();
        auto skip_cycle = (std::find(already_checked_cycle.begin(), already_checked_cycle.end(), i) != already_checked_cycle.end());

        for(std::tie(vi, vi_end) = vertices(gr.g); vi != vi_end; ++vi) {
            if(gr.g[*vi].id == i) {
                source_v_prec = *vi;
            }
            if(gr.g[*vi].id == n+i) {
                source_v_cycles = *vi;
                sink_v_prec = *vi;
            }
            if(gr.g[*vi].id == (unsigned int)2*n+1) {
                sink_v_cycles = *vi;
            }
        }

        auto t_start = high_resolution_clock::now();
        auto flow_prec = 999.9, flow_cycles = 999.9;

        flow_prec = boykov_kolmogorov_max_flow(gr.g,
            make_iterator_property_map(capacity.begin(), get(&Arc::id, gr.g)),
            make_iterator_property_map(residual_capacity_prec.begin(), get(&Arc::id, gr.g)),
            make_iterator_property_map(reverse_edge.begin(), get(&Arc::id, gr.g)),
            make_iterator_property_map(colour_prec.begin(), get(&Node::id, gr.g)),
            get(&Node::id, gr.g),
            source_v_prec,
            sink_v_prec
        );

        if(!skip_cycle) {
            flow_cycles = boykov_kolmogorov_max_flow(gr.g,
                make_iterator_property_map(capacity.begin(), get(&Arc::id, gr.g)),
                make_iterator_property_map(residual_capacity_cycles.begin(), get(&Arc::id, gr.g)),
                make_iterator_property_map(reverse_edge.begin(), get(&Arc::id, gr.g)),
                make_iterator_property_map(colour_cycles.begin(), get(&Node::id, gr.g)),
                get(&Node::id, gr.g),
                source_v_cycles,
                sink_v_cycles
            ); 
        }
            
        auto t_end = high_resolution_clock::now();
        auto time_span = duration_cast<duration<double>>(t_end - t_start);
        global::g_total_time_spent_separating_cuts += time_span.count();
    
        if(flow_prec < 1 - eps) {
            auto source_nodes = std::vector<int>(), sink_nodes = std::vector<int>();
            
            for(auto j = 0u; j < colour_prec.size(); j++) {
                if(colour_prec[j] == colour_prec[i]) {
                    source_nodes.push_back(j);
                } else {
                    sink_nodes.push_back(j);
                }
            }
        
            IloRange cut;
            IloExpr lhs;
            IloNum rhs = 1.0;
            auto expr_init = false;

            auto col_index = 0;
            for(auto ii = 0; ii <= 2 * n + 1; ii++) {
                for(auto jj = 0; jj <= 2 * n + 1; jj++) {
                    if(g.cost[ii][jj] >= 0) {
                        if( std::find(source_nodes.begin(), source_nodes.end(), ii) != source_nodes.end() &&
                            std::find(sink_nodes.begin(), sink_nodes.end(), jj) != sink_nodes.end()) {
                            if(!expr_init) {
                                lhs = x[col_index];
                                expr_init = true;
                            } else {
                                lhs += x[col_index];
                            }
                        }
                        col_index++;
                    }
                }
            }
                    
            cut = (lhs >= rhs);
            cuts.push_back(cut);
        }
        
        if(!skip_cycle && flow_cycles < 1 - eps) {
            auto source_nodes = std::vector<int>(), sink_nodes = std::vector<int>();
                        
            for(auto j = 0u; j < colour_cycles.size(); j++) {
                if(colour_cycles[j] == colour_cycles[n+i]) {
                    source_nodes.push_back(j);
                    if(j >= (size_t)(n+1)) {
                        already_checked_cycle.push_back(j-n);
                    }
                } else {
                    sink_nodes.push_back(j);
                }
            }

            IloRange cut;
            IloExpr lhs;
            IloNum rhs = 1.0;
            auto expr_init = false;
    
            auto col_index = 0;
            for(auto ii = 0; ii <= 2 * n + 1; ii++) {
                for(auto jj = 0; jj <= 2 * n + 1; jj++) {
                    if(g.cost[ii][jj] >= 0) {
                        if( std::find(source_nodes.begin(), source_nodes.end(), ii) != source_nodes.end() &&
                            std::find(sink_nodes.begin(), sink_nodes.end(), jj) != sink_nodes.end()) {                                
                            if(!expr_init) {
                                lhs = x[col_index];
                                expr_init = true;
                            } else {
                                lhs += x[col_index];
                            }
                        }
                        col_index++;
                    }
                }
            }
                
            cut = (lhs >= rhs);
            cuts.push_back(cut);
        }
    }
    
    return cuts;
}