#pragma once
#include <chrono>
#include <string>
#include <iostream>

class SpanProfiler {
private:
    std::string name;
    std::chrono::high_resolution_clock::time_point start_time;
    bool active;

public:
    SpanProfiler(const std::string& span_name) 
        : name(span_name), active(true) {
        start_time = std::chrono::high_resolution_clock::now();
    }
    
    ~SpanProfiler() {
        if (active) {
            end();
        }
    }
    
    void end() {
        if (active) {
            auto end_time = std::chrono::high_resolution_clock::now();
            auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time);
            std::cout << "[PROFILE] " << name << ": " << duration.count() << " μs" << std::endl;
            active = false;
        }
    }
};

#define PROFILE_SPAN(name) SpanProfiler _prof(name)
