#pragma once
#include <string>
#include <vector>
#include <iostream>
#include <cstdint>
#include <algorithm>
#include <ranges>
#include <queue>

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
    AVCodecContext* video_codec_ctx;
    AVCodecContext* audio_ctx;
    int audio_stream_idx;
    int video_stream_idx;

    float rms_volume;
    int rms_nb_samples;
    int rms_channels;

    AVPacket* packet;
    AVFrame* frame;
    double packets_per_sec;
    int64_t output_buffer_capacity;
    unsigned char writeOutBufferState;
    int reached_end;
    
    struct VideoSegment {
        int64_t start_pts;     
        std::queue<AVPacket*> queue;
        bool keep;             // Flag to indicate if segment should be kept

        VideoSegment()
            : start_pts(-1), queue(), keep(false) {}
        VideoSegment(int64_t start, std::queue<AVPacket*>* queue_ptr, bool keep_flag) 
            : start_pts(start), queue(), keep(keep_flag){}
    };
    VideoSegment current_segment;
    std::queue<std::queue<VideoSegment*>> outputQueue;

    float volume;
    float volume_threshold_db;
    int64_t dead_space_buffer;
    int64_t previous_end_pts;
    int64_t PTS_offset;
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
    void setVideoContext();
    void setAudiocontext();
    void setPacketsPerSec();
    double getPacketsPerSecond();
    void buildVideo();
    bool profilePacketAudio(const AVPacket* original_packet);
    float calculateRMS(AVFrame* frame, AVCodecContext* audio_codec_ctx);
    void writeHalfQueue(std::queue<VideoSegment*>* outputBuffer);
    void writeEntireQueue(std::queue<VideoSegment*>* outputBuffer);
    void emptyOutputQueue();
    void invokeQueueSM();
    void writeToOutputQueue(std::queue<VideoSegment*> outputBuffer);
    void emptyOutputBuffer(std::queue<VideoSegment*>* outputBuffer);
    void writeOutputBuffer(std::queue<VideoSegment*>* outputBuffer, VideoSegment* current_segment);
    void writeOutLoop();
};
