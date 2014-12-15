#ifndef HEURISTIC_WITH_ORDERED_REQUESTS_H
#define HEURISTIC_WITH_ORDERED_REQUESTS_H

#include <global.h>
#include <heuristics/heuristic.h>
#include <heuristics/heuristic_helper.h>

#include <chrono>
#include <ctime>
#include <limits>
#include <ratio>
#include <stdexcept>
#include <utility>

template<class RC, class IC>
class heuristic_with_ordered_requests : public heuristic {
    IC insertion_comparator;
    bool insert(int req);

public:
    heuristic_with_ordered_requests(const tsp_graph& g, const RC& rc, const IC& ic);
    path solve();
};

#include <heuristics/heuristic_with_ordered_requests.tpp>

#endif