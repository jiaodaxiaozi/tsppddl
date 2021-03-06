#ifndef GRAPH_INFO_H
#define GRAPH_INFO_H

#include <string>

struct graph_info {
    int n;
    double h;
    double k;
    int capacity;
    
    std::string instance_path;
    std::string instance_dir;
    std::string instance_name;
    std::string instance_base_name;
    
    graph_info() {}
    graph_info(int n, int capacity, std::string instance_path);
};

#endif