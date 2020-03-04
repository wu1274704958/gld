#include <frame_rate.h>

namespace gld{

    std::atomic<float> FrameRate::ms = 0.0f;

    FrameRate::Calculator::Calculator(std::atomic<float>& in) : res(in)
    {
        begin = std::chrono::system_clock::now();
    }
    FrameRate::Calculator::~Calculator()
    {
        auto dur = std::chrono::system_clock::now() - begin;
        res = static_cast<float>(std::chrono::duration_cast<std::chrono::milliseconds>(dur).count());
    }

    float FrameRate::get_ms()
    {
        return static_cast<float>(ms);
    }
    float FrameRate::fps()
    {
        return 1000.0f / static_cast<float>(ms);
    }

    FrameRate::Calculator FrameRate::calculator()
    {
        return Calculator(ms);
    }
}