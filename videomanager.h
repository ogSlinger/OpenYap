#pragma once
#ifndef videomanager
#define videomanager
#include <string>

class VideoManager {
private:
    const char* input_file;
    const char* output_file;
    AVFormatContext* format_ctx;
    AVFormatContext* output_ctx;
    int video_stream_idx;
    int audio_stream_idx;
    AVDictionary* options;
    AVCodecContext* audio_codec_ctx;

public:
    VideoManager();
    ~VideoManager();
};

#endif