#pragma once

#include <atomic>
#include <chrono>

namespace gld{

struct FrameRate
{
    
protected:
    struct Calculator
    {
        Calculator(std::atomic<float>& in);
        ~Calculator();
        std::atomic<float>& res;
        std::chrono::system_clock::time_point begin;
    }; 
    static std::atomic<float> ms;
public:
    static Calculator calculator();
    static float get_ms();
    static float fps();
};

}