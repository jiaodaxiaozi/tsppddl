#include <global.h>
#include <solver/bc/callbacks/subtour_elimination_cuts_solver.h>

#include <chrono>
#include <ctime>
#include <fstream>
#include <iostream>
#include <ratio>

SubtourEliminationCutsSolver::SubtourEliminationCutsSolver(const Graph& g, const ch::solution& sol, const IloEnv& env, const IloNumVarArray& x, double eps) : g{g}, sol{sol}, env{env}, x{x}, eps{eps}, n{g.g[graph_bundle].n} {
    pi = ch::sets_info(n,
            bvec(2*n+2, false),
            bvec(2*n+2, false),
            ivec(2*n+2, -1),
            bvec(2*n+2, false), 0,
            bvec(2*n+2, false), 0,
            bvec(2*n+2, true), 0, 0);
            
    sigma = ch::sets_info(n,
            bvec(2*n+2, false),
            bvec(2*n+2, false),
            ivec(2*n+2, -1),
            bvec(2*n+2, true), 0,
            bvec(2*n+2, false), 0,
            bvec(2*n+2, false), 0, 0);
    
    // Nodes 0 and 2n+1 are there only for padding, but don't have any meaning        
    pi.in_ts[0] = false; pi.in_tabu[0] = true; pi.in_ts[2*n+1] = false; pi.in_tabu[2*n+1] = true;
    sigma.in_fs[0] = false; sigma.in_tabu[0] = true; sigma.in_fs[2*n+1] = false; sigma.in_tabu[2*n+1] = true;
            
    tot_number_of_iterations = 25;
    tabu_duration = 10;
}

std::vector<IloRange> SubtourEliminationCutsSolver::separate_valid_cuts() {
    using namespace std::chrono;
    
    auto cuts = std::vector<IloRange>();
    
    auto start_time = high_resolution_clock::now();
    
    // As a very rough way to avoid separating the same cuts many times, we keep track of
    // 1. The first element of S and
    // 2. Of the sum of all the elements of S
    // We hash S -> (first, sum) and only add S's with unique hashes.
    struct memory { int first; int sum;
        memory () : first(0), sum(0) {}
        memory(int f, int s) : first(f), sum(s) {}
        bool operator==(const memory& other) { return (other.first == first && other.sum == sum); }
    };
    auto cuts_memory_pi = std::vector<memory>(), cuts_memory_sigma = std::vector<memory>();
        
    for(auto iter = 1; iter <= tot_number_of_iterations; iter++) {
        auto best_pi = pi, best_sigma = sigma;
        auto bn_pi = -1, bn_sigma = -1; // Best node \in (1...2n)
        
        for(auto i = 1; i <= 2*n; i++) {
            auto new_pi = pi, new_sigma = sigma;
                        
            add_or_remove_from_pi_sets(new_pi, i);
            recalculate_pi_sums(new_pi);
            add_or_remove_from_sigma_sets(new_sigma, i);
            recalculate_sigma_sums(new_sigma);
            
            if(i == pi.first_non_tabu() || (new_pi.lhs < best_pi.lhs && !pi.in_tabu[i] && !new_pi.empty_S())) {// Forbidding empty S
                best_pi = new_pi;
                bn_pi = i;
            }
            if(i == sigma.first_non_tabu() || (new_sigma.lhs < best_sigma.lhs && !sigma.in_tabu[i] && !new_sigma.empty_S())) {// Forbidding empty S
                best_sigma = new_sigma;
                bn_sigma = i;
            }
        }
        
        auto f = boost::find(best_pi.in_S, true);
        auto m_pi = memory(), m_sigma = memory();
        auto added_mem_pi = false, added_mem_sigma = false;
        if(f != boost::end(best_pi.in_S)) {
            m_pi = memory(
                f - boost::begin(best_pi.in_S),
                boost::accumulate(best_pi.in_S, 0, [] (int a, bool b) { return a + (b?1:0); })
            );
            added_mem_pi = (std::find(cuts_memory_pi.begin(), cuts_memory_pi.end(), m_pi) != cuts_memory_pi.end());
        }
        f = boost::find(best_sigma.in_S, true);
        if(f != boost::end(best_sigma.in_S)) {
            m_sigma = memory(
                f - boost::begin(best_sigma.in_S),
                boost::accumulate(best_sigma.in_S, 0, [] (int a, bool b) { return a + (b?1:0); })
            );
            added_mem_sigma = (std::find(cuts_memory_sigma.begin(), cuts_memory_sigma.end(), m_sigma) != cuts_memory_sigma.end());
        }
        
        if(bn_pi == -1) { throw std::runtime_error("Best pi node can't be -1"); }
        
        update_info(pi, best_pi, bn_pi, iter);
        
        // Bonus: we can recycle set pi to add the groetschel pi cut
        if(!added_mem_pi) {
            add_pi_cut_if_violated(cuts, pi);
            add_groetschel_pi_cut_if_violated(cuts, pi);
            cuts_memory_pi.push_back(m_pi);
        }
        
        if(bn_sigma == -1) { throw std::runtime_error("Best sigma node can't be -1"); }
        
        update_info(sigma, best_sigma, bn_sigma, iter);
        
        // Bonus: we can recycle set sigma to add the groetschel sigma cut
        if(!added_mem_sigma) {
            add_sigma_cut_if_violated(cuts, sigma);
            add_groetschel_sigma_cut_if_violated(cuts, sigma);
            cuts_memory_sigma.push_back(m_sigma);
        }
    }
        
    auto end_time = high_resolution_clock::now();
    auto time_span = duration_cast<duration<double>>(end_time - start_time);
    global::g_total_time_spent_separating_cuts += time_span.count();
    
    return cuts;
}

void SubtourEliminationCutsSolver::update_info(ch::sets_info& set, const ch::sets_info& best, int bn, int iter) {
    auto removed = set.in_S[bn];
    
    set = best;
    if(removed) {
        set.in_tabu[bn] = true;
        set.tabu_start[bn] = iter;
    }
    
    // Update tabu list
    for(auto i = 1; i <= 2*n; i++) {
        if(set.tabu_start[i] == iter - tabu_duration) {
            set.in_tabu[i] = false;
            set.tabu_start[i] = -1;
        }
    }
}

void SubtourEliminationCutsSolver::add_groetschel_sigma_cut_if_violated(std::vector<IloRange>& cuts, const ch::sets_info& sigma) {
    if(sigma.empty_S()) { return; }
    if(boost::count(sigma.in_S, true) <= 1) { return; }
    
    auto my_S = std::vector<int>();
    for(auto i = 1; i <= 2*n; i++) { if(sigma.in_S[i]) { my_S.push_back(i); } }
    
    auto outflow_S = std::vector<double>(my_S.size(), 0);
    for(auto i = 0u; i < my_S.size(); i++) {
        for(auto j = 1; j <= 2*n; j++) {
            if(sol.x[my_S[i]][j] > 0) { outflow_S[i] += sol.x[my_S[i]][j]; }
        }
    }
    
    auto max_outflow_id = std::distance(outflow_S.begin(), std::max_element(outflow_S.begin(), outflow_S.end()));
    if(max_outflow_id != 1) {
        std::swap(*my_S.begin(), *(my_S.begin() + max_outflow_id));
    }
    
    std::random_shuffle(my_S.begin() + 1, my_S.end());
    
    auto lhs = calculate_groetschel_lhs_sigma(my_S, sigma);
        
    if(lhs > my_S.size() - 1 + eps) { actually_add_groetschel_sigma_cut(cuts, my_S, sigma); }
}

void SubtourEliminationCutsSolver::add_groetschel_pi_cut_if_violated(std::vector<IloRange>& cuts, const ch::sets_info& pi) {
    if(pi.empty_S()) { return; }
    if(boost::count(pi.in_S, true) <= 1) { return; }
    
    auto my_S = std::vector<int>();
    for(auto i = 1; i <= 2*n; i++) { if(pi.in_S[i]) { my_S.push_back(i); } }
    
    auto inflow_S = std::vector<double>(my_S.size(), 0);
    for(auto i = 0u; i < my_S.size(); i++) {
        for(auto j = 1; j <= 2*n; j++) {
            if(sol.x[j][my_S[i]] > 0) { inflow_S[i] += sol.x[j][my_S[i]]; }
        }
    }
    
    auto max_inflow_id = std::distance(inflow_S.begin(), std::max_element(inflow_S.begin(), inflow_S.end()));
    if(max_inflow_id != 1) {
        std::swap(*my_S.begin(), *(my_S.begin() + max_inflow_id));
    }
    
    std::random_shuffle(my_S.begin() + 1, my_S.end());
    
    auto lhs = calculate_groetschel_lhs_pi(my_S, pi);
    
    if(lhs > my_S.size() - 1 + eps) { actually_add_groetschel_pi_cut(cuts, my_S, pi); }
}

void SubtourEliminationCutsSolver::actually_add_groetschel_sigma_cut(std::vector<IloRange>& cuts, const std::vector<int>& my_S, const ch::sets_info& sigma) {
    IloExpr lhs(env);
    IloNum rhs = my_S.size() - 1;
    
    auto col_index = 0;
    for(auto i = 0; i <= 2 * n + 1; i++) {
        for(auto j = 0; j <= 2 * n + 1; j++) {
            if(g.cost[i][j] >= 0) {
                auto pos_i = std::find(my_S.begin(), my_S.end(), i);
                auto pos_j = std::find(my_S.begin(), my_S.end(), j);
                                
                if(pos_i != my_S.end() && i != my_S.back() && pos_j == pos_i + 1) { // First sum
                    lhs += x[col_index];
                }
                
                if(i == my_S.back() && j == my_S[0]) { // x[i_h][i_1]
                    lhs += x[col_index];
                }
                
                if(pos_i != my_S.end() && pos_i > my_S.begin() && i != my_S.back() && j == my_S[0]) { // Second sum
                    lhs += 2 * x[col_index];
                }
                
                if(pos_i != my_S.end() && pos_i > my_S.begin() + 1 && i != my_S.back() && pos_j != my_S.end() && pos_j > my_S.begin() && pos_j < pos_i) { // Third sum
                    lhs += x[col_index];
                }
                
                if(sigma.is_in_ts(j) && j == my_S[0]) { // Fourth sum
                    lhs += x[col_index];
                }
                
                col_index++;
            }
        }
    }
    
    IloRange cut;
    cut = (lhs <= rhs);
        
    cuts.push_back(cut);
}

void SubtourEliminationCutsSolver::actually_add_groetschel_pi_cut(std::vector<IloRange>& cuts, const std::vector<int>& my_S, const ch::sets_info& pi) {
    IloExpr lhs(env);
    IloNum rhs = my_S.size() - 1;
    
    auto col_index = 0;
    for(auto i = 0; i <= 2 * n + 1; i++) {
        for(auto j = 0; j <= 2 * n + 1; j++) {
            if(g.cost[i][j] >= 0) {
                auto pos_i = std::find(my_S.begin(), my_S.end(), i);
                auto pos_j = std::find(my_S.begin(), my_S.end(), j);
                                
                if(pos_i != my_S.end() && i != my_S.back() && pos_j == pos_i + 1) {
                    lhs += x[col_index];
                }
                
                if(i == my_S.back() && j == my_S[0]) {
                    lhs += x[col_index];
                }
                
                if(i == my_S[0] && pos_j != my_S.end() && pos_j > my_S.begin() + 1) {
                    lhs += 2 * x[col_index];
                }
                
                if(pos_i != my_S.end() && pos_i > my_S.begin() + 2 && pos_j > my_S.begin() + 1 && pos_j < pos_i) {
                    lhs += x[col_index];
                }
                
                if(i == my_S[0] && pi.is_in_ss(j)) {
                    lhs += x[col_index];
                }
                
                col_index++;
            }
        }
    }
    
    IloRange cut;
    cut = (lhs <= rhs);
        
    cuts.push_back(cut);
}

double SubtourEliminationCutsSolver::calculate_groetschel_lhs_sigma(const std::vector<int>& my_S, const ch::sets_info& pi) {
    double lhs = 0;
    
    for(auto k = 0u; k < my_S.size() - 1; k++) {
        lhs += sol.x[my_S[k]][my_S[k+1]];
        if(k >= 1) {
            lhs += 2 * sol.x[my_S[k]][my_S[0]];
        }
        if(k >= 2) {
            for(auto l = 1u; l < k; l++) {
                lhs += sol.x[my_S[k]][my_S[l]];
            }
        }
    }
    for(auto i = 1; i <= 2*n; i++) {
        if(sigma.in_ts[i]) {
            lhs += sol.x[i][my_S[0]];
        }
    }
    lhs += sol.x[my_S.back()][my_S[0]];
    
    return lhs;
}

double SubtourEliminationCutsSolver::calculate_groetschel_lhs_pi(const std::vector<int>& my_S, const ch::sets_info& pi) {
    auto lhs = 0;
    
    for(auto k = 0u; k < my_S.size(); k++) {
        if(k < my_S.size() - 1) {
            lhs += sol.x[my_S[k]][my_S[k+1]];
        }
        if(k >= 2) {
            lhs += 2 * sol.x[my_S[0]][my_S[k]];
        }
        if(k >= 3) {
            for(auto l = 2u; l < k; l++) {
                lhs += sol.x[my_S[k]][my_S[l]];
            }
        }
    }
    for(auto i = 1; i <= 2*n; i++) {
        if(pi.in_ss[i]) {
            lhs += sol.x[my_S[0]][i];
        }
    }
    lhs += sol.x[my_S.back()][my_S[0]];
    
    return lhs;
}

void SubtourEliminationCutsSolver::add_pi_cut_if_violated(std::vector<IloRange>& cuts, const ch::sets_info& pi) {
    if(pi.lhs >= 2 - eps) { return; } // Cut not violated
    if(pi.empty_S()) { return; } // Empty S
    if(boost::count(pi.in_S, true) <= 1) { return; } // Trivial S gives a trivial inequality!
    
    IloExpr lhs(env);
    IloNum rhs = 2.0;
    
    auto col_index = 0;
    for(auto i = 0; i <= 2 * n + 1; i++) {
        for(auto j = 0; j <= 2 * n + 1; j++) {
            if(g.cost[i][j] >= 0) {
                if(pi.is_in_S(i) && !pi.is_in_S(j)) {
                    lhs += x[col_index];
                }
                if(!pi.is_in_S(i) && pi.is_in_S(j)) {
                    lhs += x[col_index];
                }
                if(pi.is_in_fs(i) && pi.is_in_ts(j)) {
                    lhs += -2 * x[col_index];
                }
                if(pi.is_in_S(i) && pi.is_in_ss(j)) {
                    lhs += -2 * x[col_index];
                }
                col_index++;
            }
        }
    }
    
    IloRange cut;
    cut = (lhs >= rhs);
    cuts.push_back(cut);
}

void SubtourEliminationCutsSolver::add_sigma_cut_if_violated(std::vector<IloRange>& cuts, const ch::sets_info& sigma) {
    if(sigma.lhs >= 2 - eps) { return; } // Cut not violated
    if(sigma.empty_S()) { return; } // Empty S
    if(boost::count(sigma.in_S, true) <= 1) { return; } // Trivial S gives a trivial inequality!
    
    IloExpr lhs(env);
    IloNum rhs = 2.0;
    
    auto col_index = 0;
    for(auto i = 0; i <= 2 * n + 1; i++) {
        for(auto j = 0; j <= 2 * n + 1; j++) {
            if(g.cost[i][j] >= 0) {
                if(sigma.is_in_S(i) && !sigma.is_in_S(j)) {
                    lhs += x[col_index];
                }
                if(!sigma.is_in_S(i) && sigma.is_in_S(j)) {
                    lhs += x[col_index];
                }
                if(sigma.is_in_fs(i) && sigma.is_in_ss(j)) {
                    lhs += -2 * x[col_index];
                }
                if(sigma.is_in_ts(i) && sigma.is_in_S(j)) {
                    lhs += -2 * x[col_index];
                }
                col_index++;
            }
        }
    }
    
    IloRange cut;
    cut = (lhs >= rhs);
    cuts.push_back(cut);
}

void SubtourEliminationCutsSolver::add_or_remove_from_pi_sets(ch::sets_info& pi, int i) {
    if(pi.in_S[i]) { // Remove
        if(i <= n) {
            if(pi.in_S[i+n]) {
                pi.in_fs[i] = false;
                pi.in_ss[i] = true;
            } else {
                pi.in_ts[i] = true;
            }
        } else {
            if(pi.in_S[i-n]) {
                pi.in_fs[i-n] = false;
                pi.in_ts[i] = true;
            } else {
                pi.in_ss[i-n] = false;
                pi.in_ts[i-n] = true;
                pi.in_ts[i] = true;
            }
        }
    } else if(!pi.in_tabu[i]) { // Add
        if(i <= n) {
            if(pi.in_S[i+n]) {
                pi.in_fs[i] = true;
                pi.in_ss[i] = false;
            } else {
                pi.in_ts[i] = false;
            }
        } else {
            if(pi.in_S[i-n]) {
                pi.in_fs[i-n] = true;
                pi.in_ts[i] = false;
            } else {
                pi.in_ss[i-n] = true;
                pi.in_ts[i-n] = false;
                pi.in_ts[i] = false;
            }
        }
    }
    pi.in_S[i] = !pi.in_S[i];
}

void SubtourEliminationCutsSolver::add_or_remove_from_sigma_sets(ch::sets_info& sigma, int i) {
    if(sigma.in_S[i]) { // Remove
        if(i <= n) {
            if(sigma.in_S[i+n]) {
                sigma.in_fs[i] = true;
                sigma.in_ss[i+n] = false;
            } else {
                sigma.in_fs[i] = true;
                sigma.in_fs[i+n] = true;
                sigma.in_ts[i+n] = false;
            }
        } else {
            if(sigma.in_S[i-n]) {
                sigma.in_ss[i] = false;
                sigma.in_ts[i] = true;
            } else {
                sigma.in_fs[i] = true;
            }
        }
    } else if(!sigma.in_tabu[i]) { // Add
        if(i <= n) {
            if(sigma.in_S[i+n]) {
                sigma.in_fs[i] = false;
                sigma.in_ss[i+n] = true;
            } else {
                sigma.in_fs[i] = false;
                sigma.in_fs[i+n] = false;
                sigma.in_ts[i+n] = true;
            }
        } else {
            if(sigma.in_S[i-n]) {
                sigma.in_ss[i] = true;
                sigma.in_ts[i] = false;
            } else {
                sigma.in_fs[i] = false;
            }
        }
    }
    sigma.in_S[i] = !sigma.in_S[i];
}

void SubtourEliminationCutsSolver::recalculate_pi_sums(ch::sets_info& pi) {
    pi.fs = 0; pi.ss = 0; pi.ts = 0; pi.lhs = 0;
        
    for(auto i = 0; i <= 2 * n + 1; i++) {
        for(auto j = 0; j <= 2 * n + 1; j++) {
            if(g.cost[i][j] >= 0) {
                if(pi.is_in_S(i) && !pi.is_in_S(j)) {
                    pi.fs += sol.x[i][j];
                }
                if(!pi.is_in_S(i) && pi.is_in_S(j)) {
                    pi.fs += sol.x[i][j];
                }
                if(pi.is_in_fs(i) && pi.is_in_ts(j)) {
                    pi.ss += sol.x[i][j];
                }
                if(pi.is_in_S(i) && pi.is_in_ss(j)) {
                    pi.ts += sol.x[i][j];
                }
            }
        }
    }
    
    pi.lhs = pi.fs - 2 * pi.ss - 2 * pi.ts;
}

void SubtourEliminationCutsSolver::recalculate_sigma_sums(ch::sets_info& sigma) {
    sigma.fs = 0; sigma.ss = 0; sigma.ts = 0; sigma.lhs = 0;
        
    for(auto i = 0; i <= 2 * n + 1; i++) {
        for(auto j = 0; j <= 2 * n + 1; j++) {
            if(g.cost[i][j] >= 0) {
                if(sigma.is_in_S(i) && !sigma.is_in_S(j)) {
                    sigma.fs += sol.x[i][j];
                }
                if(!sigma.is_in_S(i) && sigma.is_in_S(j)) {
                    sigma.fs += sol.x[i][j];
                }
                if(sigma.is_in_fs(i) && sigma.is_in_ss(j)) {
                    sigma.ss += sol.x[i][j];
                }
                if(sigma.is_in_ts(i) && sigma.is_in_S(j)) {
                    sigma.ts += sol.x[i][j];
                }
            }
        }
    }
    
    sigma.lhs = sigma.fs - 2 * sigma.ss - 2 * sigma.ts;
}