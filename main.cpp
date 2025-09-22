#include <iostream> 
#include <chrono>
#include "videomanager.h"

extern "C" {  
#include <libavcodec/avcodec.h> 
#include <libavformat/avformat.h> 
#include <libavutil/imgutils.h> 
#include <libavutil/avutil.h>  
#include <libswscale/swscale.h> 
}

int main(int argc, char* argv[]) {
    std::string inputName = "input.mp4";
    std::string outputName = "output.mp4";
    float db_volume_threshold = -18.0f;
    float dead_space_buffer = 0.5f;

    if (argc == 5) {
        inputName = argv[1];
        outputName = argv[2];

        try {
            db_volume_threshold = std::stof(argv[3]);
        }
        catch (const std::invalid_argument& e) {
            std::cout << "Error: Invalid float format" << std::endl;
            return 1;
        }
        catch (const std::out_of_range& e) {
            std::cout << "Error: Float value out of range" << std::endl;
            return 1;
        }
        db_volume_threshold = (db_volume_threshold >= 0) ? -db_volume_threshold : db_volume_threshold;

        try {
            dead_space_buffer = std::stof(argv[4]);
        }
        catch (const std::invalid_argument& e) {
            std::cout << "Error: Invalid float format" << std::endl;
            return 1;
        }
        catch (const std::out_of_range& e) {
            std::cout << "Error: Float value out of range" << std::endl;
            return 1;
        }
        dead_space_buffer = (dead_space_buffer <= 0) ? 0.5f : dead_space_buffer;
    }

    auto start = std::chrono::high_resolution_clock::now();

    VideoManager vm(inputName.c_str(), 
        outputName.c_str(), 
        db_volume_threshold, 
        dead_space_buffer);

    vm.buildVideo();

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    std::cout << "Execution time: " << duration.count() << " microseconds" << std::endl;
    std::cout << "Execution time: " << duration.count() / 1000.0 << " milliseconds" << std::endl;
    return 0;
}

/*
    Technologies:
        FFMPEG

    Capabilities: 
        -Removes silence from video
        -Handles irregular packets (dropped frames)
        -Allows for silent buffers inbetween

    Statistics:
        ~2.5 hour long video => 1 hour
        ~13 seconds

    Restrictions:
        Output is can only match input.
        Packet handling ONLY
*/