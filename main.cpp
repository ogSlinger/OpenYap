#include <iostream>  // Include standard C++ input-output library for console operations
#include "videomanager.h"
extern "C" {  // Use C linkage for FFmpeg libraries since they're written in C
#include <libavcodec/avcodec.h>  // Include FFmpeg codec library for encoding/decoding
#include <libavformat/avformat.h>  // Include FFmpeg format library for container formats (MP4, MKV, etc.)
#include <libavutil/imgutils.h>  // Include FFmpeg utility library for image-related functions
#include <libavutil/avutil.h>  // Include FFmpeg utility library for common functions
#include <libswscale/swscale.h>  // Include FFmpeg library for scaling and pixel format conversion
}
#include <chrono>

int main(int argc, char* argv[]) {
    auto start = std::chrono::high_resolution_clock::now();

    VideoManager vm("input.mp4", "output.mp4");
    vm.buildVideo();

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    std::cout << "Execution time: " << duration.count() << " microseconds" << std::endl;
    std::cout << "Execution time: " << duration.count() / 1000.0 << " milliseconds" << std::endl;
    return 0;  // Return success code
}
