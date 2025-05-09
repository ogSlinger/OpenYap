#pragma once
#include <string>
#include <vector>
#include <iostream>
#include <cstdint>

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

    float rms_volume;
    int rms_nb_samples;
    int rms_channels;

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

    float volume;
    float volume_threshold_db;
    int64_t dead_space_buffer;
    int64_t pts;
    bool is_audible;
    

public:
    VideoManager(const char* input_file, const char* output_file);
    ~VideoManager();
    void openInput();
    void populateFormatContext();
    void getAudioStreamIndex();
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
