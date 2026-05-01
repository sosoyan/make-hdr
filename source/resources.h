//
//  resources.h
//  MakeHDR
//
//  Created by Vahan Sosoyan 2024.
//

#ifndef resources_h
#define resources_h

#include <vector>
#include <string>
#include <random>
#include <chrono>
#include <thread>
#include <cfloat>
#include <unordered_set>
#include <cstdint>

#include "spdlog/spdlog.h"

#include "armadillo"

#include "ofxsImageEffect.h"
#include "ofxsMultiThread.h"
#include "ofxsProcessing.H"

#define VERSION_MAJOR 1
#define VERSION_MINOR 2
#define VERSION_FIX 0

#define CMP_MAX 3
#define SRC_MAX 16


namespace fx
{
    const std::string label = "MakeHDR";
    const std::string version = std::to_string(VERSION_MAJOR) + "." + 
                                std::to_string(VERSION_MINOR) + "." +
                                std::to_string(VERSION_FIX);

    const std::string description = label + " v" + version + 
        " developed by Vahan Sosoyan";

    enum ch
    {
        r, g, b, a
    };

    struct point
    {
    public:
        point(int _x, int _y) : x(_x),
                                y(_y) 
        {
        }
    
        int x;
        int y;

        int64_t key() const { return (int64_t)x << 32 | (uint32_t)y; }
    };

    class timer
    {
    public:
        timer()
        {
            begin = std::chrono::steady_clock::now();
        }

        long long get()
        {
            const std::chrono::steady_clock::time_point end = std::chrono::steady_clock::now();
            return std::chrono::duration_cast<std::chrono::milliseconds>(end - begin).count();
        }

    private:
        std::chrono::steady_clock::time_point begin;
    };
}

#endif