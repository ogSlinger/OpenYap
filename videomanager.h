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
    float linear_volume_threshold;

    AVPacket* packet;
    AVPacket* out_pkt_ptr;
    AVFrame* frame;
    double packets_per_sec;
    unsigned char writeOutBufferState;
    int reached_end;
    
    struct VideoSegment {
        int64_t start_pts;  
        int64_t next_pts;
        bool keep;             // Flag to indicate if segment should be kept
        std::queue<AVPacket*> queue;

        VideoSegment()
            : start_pts(-1), next_pts(-1), queue(), keep(false) {}
        VideoSegment(int64_t start, int64_t end, std::queue<AVPacket*>* queue_ptr, bool keep_flag) 
            : start_pts(start), next_pts(end), queue(), keep(keep_flag){}
    };
    VideoSegment current_segment;
    std::queue<std::queue<VideoSegment*>*> outputQueue;

    float volume;
    float volume_threshold_db;
    int64_t dead_space_buffer;
    int64_t previous_next_pts;
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
    void writeFileTrailer();
    void setVideoContext();
    void setAudiocontext();
    void buildVideo();
    float calculateLinearScaleThreshold();
    void calculateFrameAudio(VideoSegment* current_segment, AVPacket* packet, int bytes_per_sample);
    void writeFullQueue();
    void writeHalfQueue();
    void popHalfQueue(std::queue<VideoSegment*>* outputBuffer);
    void invokeQueueSM();
    void writeToOutputQueue(std::queue<VideoSegment*>* outputBuffer);
    void emptyFPSBuffer(std::queue<VideoSegment*>* outputBuffer);
    void emptyOutfileBuffer(std::queue<VideoSegment*>* outputBuffer);
    void writeOutputBuffer(std::queue<VideoSegment*>* outputBuffer, VideoSegment* current_segment);
    void writeOutLoop();
};
