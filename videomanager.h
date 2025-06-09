//© 2025[Derek Spaulding].All rights reserved.
#pragma once
#include <string>
#include <vector>
#include <iostream>
#include <cstdint>
#include <algorithm>
#include <ranges>
#include <queue>
#include <cmath>

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
    AVCodecContext* audio_decoder_ctx;
    int audio_stream_idx;
    int video_stream_idx;
    float linear_volume_threshold;

    AVPacket* out_pkt_ptr;
    double packets_per_sec;
    unsigned char writeOutBufferState;
    int reached_end;
    
    struct VideoSegment {
        int64_t start_pts;  
        int64_t start_dts;  
        int64_t next_pts;
        int64_t next_dts;
        bool keep;
        bool ready_to_push;
        std::queue<AVPacket*> queue;

        VideoSegment()
            : start_pts(-1), start_dts(-1), next_pts(-1), next_dts(-1), queue(), keep(false), ready_to_push(false) {}
        VideoSegment(int64_t start_pts, int64_t start_dts, int64_t next_pts, int64_t next_dts, std::queue<AVPacket*>* queue_ptr, bool keep_flag, bool push_flag, int64_t audio_duration)
            : start_pts(start_pts), start_dts(start_dts), next_pts(next_pts), next_dts(next_dts), queue(), keep(keep_flag), ready_to_push(push_flag) {}
    };
    VideoSegment current_segment;
    std::queue<std::queue<VideoSegment*>*> outputQueue;

    float volume_threshold_db;
    float dead_space_buffer;
    int64_t dead_space_buffer_pts;
    int64_t buffer_running_duration;
    int64_t video_pts_offset;
    int64_t audio_pts_offset;
    int64_t video_dts_offset;
    int64_t audio_dts_offset;
    
public:
    VideoManager(const char* input_file, const char* output_file);
    ~VideoManager();
    void buildVideo();

private: 
    void openInput();
    void setInputContext();
    void setAudioStreamIndex(int index);
    void setVideoStreamIndex(int index);
    const AVCodec* getAudioCodec();
    const AVCodec* getVideoCodec();
    void setAudioCodec();
    void copyAudioCodecParams();
    void openAudioCodec();
    void createOutputContext();
    void createOutputStreams();
    void openOutputFile();
    void writeFileHeader();
    void writeFileTrailer();
    void setAudioDecoder();
    void secondsToPTS();
    void calculateLinearScaleThreshold();
    void calculateFrameAudio(VideoSegment* current_segment, AVPacket* packet);
    void writeFullQueue();
    void writeHalfQueue();
    void popHalfQueue(std::queue<VideoSegment*>* outputBuffer);
    void invokeQueueSM();
    void writeToOutputQueue(std::queue<VideoSegment*>* outputBuffer);
    std::queue<VideoSegment*>* copyOutputBuffer(std::queue<VideoSegment*>* old_outputBuffer);
    void emptyOutfileBuffer(std::queue<VideoSegment*>* outputBuffer);
    void writeOutputBuffer(std::queue<VideoSegment*>* outputBuffer, VideoSegment* current_segment);
    void writeOutLoop();

    template<typename T>
    bool processAudioSamples(AVFrame* frame, T* samples, int* channels,
        int* num_increment, int& peak_threshold_count, float* linear_threshold, int* sample_count) {
        for (int i = 0; i < *sample_count; i += *num_increment) {
            T sample_value = samples[i];
            T abs_value;
            if constexpr (std::is_floating_point_v<T>) {
                abs_value = fabs(sample_value);
            }
            else {
                abs_value = abs(sample_value);
            }

            peak_threshold_count += (abs_value > *linear_threshold) ? 1 : -1;

            if (peak_threshold_count == 5) {
                return true;  // Keep segment
            }
            else if (peak_threshold_count == -5) {
                return false; // Don't keep segment
            }
        }
        return false; // Continue processing
    }
};
//© 2025[Derek Spaulding].All rights reserved.
