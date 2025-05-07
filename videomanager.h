#pragma once
#include <string>
#include <vector>
#include <iostream>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>
#include <libavutil/avutil.h>
#include <libswscale/swscale.h>
}

class VideoManager {
private:
    const char* input_file;
    const char* output_file;
    AVFormatContext* format_ctx;
    AVFormatContext* output_ctx;
    int video_stream_idx;
    int audio_stream_idx;
    AVCodecContext* input_audio_codec_ctx;
    AVCodecContext* output_audio_format_ctx;
    AVPacket* packet;
    AVFrame* frame;
    
    struct AudioSegment {
        int64_t start_pts;     // Presentation timestamp (start)
        int64_t end_pts;       // Presentation timestamp (end)
        AVRational time_base;  // Time base for accurate timing
        bool keep;             // Flag to indicate if segment should be kept
    };
    AudioSegment current_segment;
    std::vector<AudioSegment> soundProfile;
    float volume_threshold_db;
    float dead_space_buffer;
    

public:
    VideoManager(const char* input_file, const char* output_file);
    ~VideoManager();
    void validateFileExists();
    void populateFormatContext();
    void getAudioStream();
    const AVCodec* getAudioCodec();
    void setAudioCodec();
    void copyAudioCodecParams();
    void openAudioCodec();
    void createOutputContext();
    void createOutputStreams();
    void openOutputFile();
    void writeFileHeader();
    AVFrame* frameAlloc();
    void buildSoundProfile();
    float calculateRMS(AVFrame* frame, AVCodecContext* audio_codec_ctx);
    void parseInputVideo();
};
