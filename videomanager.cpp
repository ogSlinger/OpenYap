#include "videomanager.h"

VideoManager::VideoManager(const char* input_file, const char* output_file) {
    this->input_file = input_file;
    this->output_file = output_file;

    this->format_ctx = nullptr;      
    this->output_ctx = nullptr;

    this->input_audio_codec_ctx = nullptr;
    this->output_audio_format_ctx = nullptr;

    this->video_stream_idx = -1;
    this->audio_stream_idx = -1;

    this->frame = nullptr;

    this->dead_space_buffer = 0;
    this->volume_threshold_db = -40.0f;

    this->current_segment.start_pts = AV_NOPTS_VALUE;
    this->current_segment.end_pts = AV_NOPTS_VALUE;
    this->current_segment.time_base = { 1, 1000 }; // Default millisecond time base
    this->current_segment.keep = false;

    av_init_packet(packet);
}


VideoManager::~VideoManager() {

}


void VideoManager::validateFileExists() {
    if (avformat_open_input(&this->format_ctx, this->input_file, nullptr, nullptr) < 0) {
        throw std::runtime_error("Could not open input file");
    }
}


void VideoManager::populateFormatContext() {
    // Open the input file and populate the format context with file information
    if (avformat_open_input(&this->format_ctx, this->input_file, nullptr, nullptr) < 0) {
        throw std::runtime_error("Could not open input file: ");
    }

    // Read stream information from the file (detect streams, codecs, etc.)
    if (avformat_find_stream_info(format_ctx, nullptr) < 0) {
        throw std::runtime_error("Could not find stream information");
    }
}


void VideoManager::getAudioStream() {
    this->audio_stream_idx = -1;
    for (unsigned int i = 0; i < this->format_ctx->nb_streams; i++) {
        if (this->format_ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
            this->audio_stream_idx = i;
            break;
        }
    }

    if (this->audio_stream_idx == -1) {
        throw std::runtime_error("Could not find audio stream");
    }
}


const AVCodec* VideoManager::getAudioCodec() {
    const AVCodec* audio_codec = avcodec_find_decoder(this->format_ctx->streams[this->audio_stream_idx]->codecpar->codec_id);
    if (!audio_codec) {
        throw std::runtime_error("Could not find audio codec");
        return nullptr;
    }
    return audio_codec;
}


void VideoManager::setAudioCodec() {
    // Allocate codec context
    this->input_audio_codec_ctx = avcodec_alloc_context3(this->getAudioCodec());
    if (!this->input_audio_codec_ctx) {
        throw std::runtime_error("Could not allocate audio codec context");
    }
}


void VideoManager::copyAudioCodecParams() {
    // Copy codec parameters based on input format
    if (avcodec_parameters_to_context(this->input_audio_codec_ctx, this->format_ctx->streams[this->audio_stream_idx]->codecpar) < 0) {
        throw std::runtime_error("Could not copy codec parameters");
    }
}


void VideoManager::openAudioCodec() {
    if (avcodec_open2(this->input_audio_codec_ctx, this->getAudioCodec(), nullptr) < 0) {
        throw std::runtime_error("Could not open audio codec");
    }
}


void VideoManager::createOutputContext() {
    if (avformat_alloc_output_context2(&this->output_ctx, nullptr, nullptr, this->output_file) < 0) {
        throw std::runtime_error("Could not create output context");
    }
}


void VideoManager::createOutputStreams() {
    // Create streams in output file (copy all streams from input)
    for (unsigned int i = 0; i < this->format_ctx->nb_streams; i++) {
        // Create a new stream in the output file
        AVStream* out_stream = avformat_new_stream(this->output_ctx, nullptr);
        if (!out_stream) {
            throw std::runtime_error("Failed to allocate output stream");
        }

        // Copy the stream parameters
        if (avcodec_parameters_copy(out_stream->codecpar, this->format_ctx->streams[i]->codecpar) < 0) {
            throw std::runtime_error("Failed to copy codec parameters");
        }
    }
}


void VideoManager::openOutputFile() {
    if (!(this->output_ctx->oformat->flags & AVFMT_NOFILE)) {
        if (avio_open(&this->output_ctx->pb, output_file, AVIO_FLAG_WRITE) < 0) {
            throw std::runtime_error("Could not open output file");
        }
    }
}

void VideoManager::writeFileHeader() {
    if (avformat_write_header(this->output_ctx, nullptr) < 0) {
        throw std::runtime_error("Error writing header");
    }
}


AVFrame* VideoManager::frameAlloc() {
    // Frame for decoding audio
    AVFrame* frame = av_frame_alloc();
    if (!frame) {
        throw std::runtime_error("Could not allocate frame");
    }

    return frame;
}


void VideoManager::buildSoundProfile() {
    try {
        this->validateFileExists();
        this->populateFormatContext();
        this->getAudioStream();
        this->getAudioCodec();
        this->setAudioCodec();
        this->getAudioStream();
        this->copyAudioCodecParams();
        this->openAudioCodec();
    }
    catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
    }

    // Initialize the current segment
    this->current_segment.start_pts = AV_NOPTS_VALUE;
    this->current_segment.keep = false;

    // Read packets and analyze audio
    AVPacket packet;
    AVFrame* frame = av_frame_alloc();

    while (av_read_frame(this->format_ctx, &packet) >= 0) {
        // Only process audio packets
        if (packet.stream_index == this->audio_stream_idx) {
            // Decode packet
            if (avcodec_send_packet(this->input_audio_codec_ctx, &packet) >= 0) {
                while (avcodec_receive_frame(this->input_audio_codec_ctx, frame) >= 0) {
                    // Calculate audio volume (RMS)
                    float volume = calculateRMS(frame, this->input_audio_codec_ctx);

                    // Get pts value for this frame
                    int64_t pts = frame->pts;
                    if (pts == AV_NOPTS_VALUE) {
                        pts = frame->best_effort_timestamp;
                    }

                    // Check if above threshold
                    bool is_audible = (volume >= this->volume_threshold_db);

                    // State transition logic
                    if (is_audible) {
                        // Start a new segment to keep
                        if (this->current_segment.start_pts != AV_NOPTS_VALUE) {
                            // Finish previous segment
                            this->current_segment.end_pts = pts;

                            //If the previous segment's endpoint + dead space buffer > the start of next buffer, just reassign previous segment endpoint
                            if (!this->soundProfile.empty() &&
                                (this->soundProfile.back().end_pts + this->dead_space_buffer) > this->current_segment.start_pts) {
                                this->soundProfile.back().end_pts = this->current_segment.end_pts;
                            }
                            else {
                                this->soundProfile.push_back(this->current_segment);
                            }
                        }

                        // Start new audible segment
                        this->current_segment.start_pts = pts;

                        // Add null check before accessing the time_base
                        if (this->format_ctx && this->audio_stream_idx >= 0 &&
                            this->audio_stream_idx < (int)this->format_ctx->nb_streams &&
                            this->format_ctx->streams[this->audio_stream_idx]) {
                            this->current_segment.time_base = this->format_ctx->streams[this->audio_stream_idx]->time_base;
                        }
                        else {
                            // Default time base if not available (this is a fallback)
                            this->current_segment.time_base = { 1, 1000 }; // millisecond time base
                            std::cerr << "Warning: Could not access stream time base" << std::endl;
                        }
                        this->current_segment.keep = true;
                    }
                    else if (!is_audible && this->current_segment.keep) {
                        // End of audible segment
                        this->current_segment.end_pts = pts;
                        this->soundProfile.push_back(this->current_segment);

                        // Start new silent segment
                        this->current_segment.start_pts = pts;
                        this->current_segment.keep = false;
                    }
                }
            }
        }

        av_packet_unref(&packet);
    }

    // Add the last segment if needed
    if (this->current_segment.start_pts != AV_NOPTS_VALUE) {
        this->current_segment.end_pts = INT64_MAX; // End of stream
        this->soundProfile.push_back(this->current_segment);
    }

    // Cleanup
    av_frame_free(&frame);
    avcodec_free_context(&this->input_audio_codec_ctx);
    avformat_close_input(&this->format_ctx);
}

float VideoManager::calculateRMS(AVFrame* frame, AVCodecContext* audio_codec_ctx) {
    float volume = 0.0f;
    int nb_samples = frame->nb_samples;
    int channels = audio_codec_ctx->ch_layout.nb_channels;

    // Sum the squares of all samples
    for (int i = 0; i < nb_samples; i++) {
        for (int ch = 0; ch < channels; ch++) {
            float sample = 0.0f;

            // Handle different sample formats
            if (audio_codec_ctx->sample_fmt == AV_SAMPLE_FMT_FLT) {
                sample = ((float*)frame->data[ch])[i];
            }
            else if (audio_codec_ctx->sample_fmt == AV_SAMPLE_FMT_S16) {
                sample = ((int16_t*)frame->data[ch])[i] / 32768.0f;
            }
            else if (audio_codec_ctx->sample_fmt == AV_SAMPLE_FMT_S32) {
                sample = ((int32_t*)frame->data[ch])[i] / 2147483648.0f;
            }

            volume += sample * sample;
        }
    }

    // Calculate RMS
    if (nb_samples * channels > 0) {
        volume = sqrt(volume / (nb_samples * channels));
    }

    return volume;
}


void VideoManager::parseInputVideo() {
    
}

