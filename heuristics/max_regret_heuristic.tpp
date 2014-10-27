template<class IC, class RG>
Path MaxRegretHeuristic<IC, RG>::solve() {
    using namespace std::chrono;
    
    auto t_start = high_resolution_clock::now();
    auto rs = this->remaining_requests.size();
    
    while(rs > 0) {
        auto regrets = std::vector<double>(rs, 0);
        auto new_possible_paths = std::vector<Path>(rs, Path());
        auto ps = this->p.path.size();
            
        for(auto req_id = 0u; req_id < rs; req_id++) {
            // BestCost, SecondBestCost, BestLoad, SecondBestLoad
            auto bc = std::numeric_limits<int>::max(), sbc = std::numeric_limits<int>::max(), bl = 0, sbl = 0;
            auto managed_to_insert = false;
            Path new_path;
            
            for(auto x = 1u; x < ps; x++) {
                for(auto y = x; y < ps; y++) {
                    auto req = this->remaining_requests[req_id];
                    auto result = HeuristicHelper::insert<IC>(this->g, this->insertion_comparator, req, x, y, this->p, sbc, sbl);

                    if(result.first) {
                        managed_to_insert = true;
                        if(this->insertion_comparator(result.second.total_cost, result.second.total_load, bc, bl)) {
                            sbc = bc;
                            sbl = bl;
                            new_path = result.second;
                            bc = new_path.total_cost;
                            bl = new_path.total_load;
                        } else {
                            sbc = result.second.total_cost;
                            sbl = result.second.total_load;
                        }
                    }
                }
            }
            
            if(managed_to_insert) {
                regrets[req_id] = this->regret_calculator(bc, bl, sbc, sbl);
                new_possible_paths[req_id] = new_path;
            } else {
                regrets[req_id] = -1;
                new_possible_paths[req_id] = Path();
            }
        }
        
        auto best_regret = regrets[0];
        auto best_regret_id = 0;
        
        for(auto req_id = 1u; req_id < rs; req_id++) {
            if(regrets[req_id] > best_regret) {
                best_regret = regrets[req_id];
                best_regret_id = req_id;
            }
        }
        
        if(best_regret >= 0) {
            this->p = new_possible_paths[best_regret_id];
            this->remaining_requests.erase(this->remaining_requests.begin() + best_regret_id);
        } else {
            throw std::runtime_error("Can't insert any request! (max regret)");
        }
        
        rs = this->remaining_requests.size();
    }
    
    auto t_end = high_resolution_clock::now();
    auto time_span = duration_cast<duration<double>>(t_end - t_start);
    global::g_total_time_spent_by_heuristics += time_span.count();
    
    return this->p;
}