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
    AVCodecContext* input_audio_codec_ctx;
    AVCodecContext* output_audio_format_ctx;

public:
    VideoManager(const char* input_file, const char* output_file);
    ~VideoManager();
    int validateFileExists();
    int populateFormatContext();
    int getAudioStream();
    const AVCodec* getAudioCodec();
    int setAudioCodec();
    int copyAudioCodecParams();
    int openAudioCodec();
    int createOutputContext();
};

#endif