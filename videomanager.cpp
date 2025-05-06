#include <iostream>
#include "videomanager.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>
#include <libavutil/avutil.h>
#include <libswscale/swscale.h>
}

VideoManager::VideoManager(const char* input_file, const char* output_file) {
    this->input_file = input_file;
    this->output_file = output_file;
    this->format_ctx = nullptr;      
    this->output_ctx = nullptr;
    this->video_stream_idx = -1;
    this->audio_stream_idx = -1;
    this->options = nullptr;
    this->input_audio_codec_ctx = nullptr;
}


VideoManager::~VideoManager() {

}


int VideoManager::validateFileExists() {
    if (avformat_open_input(&this->format_ctx, this->input_file, nullptr, nullptr) < 0) {
        std::cerr << "Could not open input file" << std::endl;
        return 1;
    }
}


int VideoManager::populateFormatContext() {
    // Open the input file and populate the format context with file information
    if (avformat_open_input(&this->format_ctx, this->input_file, nullptr, nullptr) < 0) {
        std::cerr << "Could not open input file: " << this->input_file << std::endl;
        return 0;  // Return error code if file couldn't be opened
    }

    // Read stream information from the file (detect streams, codecs, etc.)
    if (avformat_find_stream_info(format_ctx, nullptr) < 0) {
        std::cerr << "Could not find stream information" << std::endl;
        return 0;  // Return error code if stream info couldn't be found
    }

    return 1;
}


int VideoManager::getAudioStream() {
    for (unsigned int i = 0; i < this->format_ctx->nb_streams; i++) {
        if (this->format_ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
            this->audio_stream_idx = i;
            break;
        }
    }

    if (this->audio_stream_idx == -1) {
        std::cerr << "Could not find audio stream" << std::endl;
        return 0;
    }

    return 1;
}


const AVCodec* VideoManager::getAudioCodec() {
    const AVCodec* audio_codec = avcodec_find_decoder(this->format_ctx->streams[this->audio_stream_idx]->codecpar->codec_id);
    if (!audio_codec) {
        std::cerr << "Could not find audio codec" << std::endl;
        return nullptr;
    }

    return audio_codec;
}


int VideoManager::setAudioCodec() {
    // Allocate codec context
    this->input_audio_codec_ctx = avcodec_alloc_context3(this->getAudioCodec());
    if (!this->input_audio_codec_ctx) {
        std::cerr << "Could not allocate audio codec context" << std::endl;
        return 0;
    }

    return 1;
}


int VideoManager::copyAudioCodecParams() {
    // Copy codec parameters based on input format
    if (avcodec_parameters_to_context(this->input_audio_codec_ctx, this->format_ctx->streams[this->audio_stream_idx]->codecpar) < 0) {
        std::cerr << "Could not copy codec parameters" << std::endl;
        return 0;
    }

    return 1;
}


int VideoManager::openAudioCodec() {
    if (avcodec_open2(this->input_audio_codec_ctx, this->getAudioCodec(), nullptr) < 0) {
        std::cerr << "Could not open audio codec" << std::endl;
        return 0;
    }

    return 1;
}


int VideoManager::createOutputContext() {
    if (avformat_alloc_output_context2(&this->output_ctx, nullptr, nullptr, this->output_file) < 0) {
        std::cerr << "Could not create output context" << std::endl;
        return 0;
    }

    return 1;
}



