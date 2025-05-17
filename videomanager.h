#pragma once
#include <string>
#include <vector>
#include <iostream>
#include <cstdint>
#include <algorithm>
#include <ranges>

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
    AVFormatContext* input_ctx;
    AVFormatContext* output_ctx;
    AVCodecContext* video_ctx;
    AVCodecContext* audio_ctx;
    int audio_stream_idx;
    int video_stream_idx;

    float rms_volume;
    int rms_nb_samples;
    int rms_channels;

    AVPacket* packet;
    AVFrame* frame;
    double packets_per_sec;
    int64_t bufferSize;
    
    struct VideoSegment {
        int64_t start_pts;     // Presentation timestamp (start)
        int64_t end_pts;       // Presentation timestamp (end)
        AVPacket* packet;
        bool keep;             // Flag to indicate if segment should be kept

        VideoSegment()
            : start_pts(-1), end_pts(-1), keep(false) {}
        VideoSegment(int64_t start, int64_t end, bool keep_flag) 
            : start_pts(start), end_pts(end), keep(keep_flag){}
    };
    VideoSegment current_segment;
    std::vector<VideoSegment*> outputQueue;

    float volume;
    float volume_threshold_db;
    int64_t dead_space_buffer;
    int64_t previous_end_pts;
    bool is_audible;
    

public:
    VideoManager(const char* input_file, const char* output_file);
    ~VideoManager();
    void openInput();
    void setInputContext();
    void setAudioStreamIndex(int index);
    void setVideoStreamIndex(int index);
    const AVCodec* getAudioCodec();
    void setAudioCodec();
    void copyAudioCodecParams();
    void openAudioCodec();
    void createOutputContext();
    void createOutputStreams();
    void openOutputFile();
    void writeFileHeader();
    AVFrame* frameAlloc();
    void setVideoContext();
    void setAudiocontext();
    void setPacketsPerSec();
    std::vector<VideoSegment*> createOutputBuffer();
    void setBufferPrelude(bool setter, std::vector<VideoSegment*> *outputBuffer);
    void readInPacket(std::vector<VideoSegment*> *outputBuffer, VideoSegment* segment);
    void writeOutPacket(std::vector<VideoSegment*> *outputBuffer);
    void shiftBufferLeft(std::vector<VideoSegment*>* outputBuffer);
    void buildVideo();
    float calculateRMS(AVFrame* frame, AVCodecContext* audio_codec_ctx);
};
