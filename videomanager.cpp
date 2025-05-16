#include "videomanager.h"

#define MAX_STREAMS 10

VideoManager::VideoManager(const char* input_file, const char* output_file) {
    this->input_file = input_file;
    this->output_file = output_file;
    this->audio_stream_idx = -1;
    this->video_stream_idx = -1;

    this->input_ctx = nullptr;      
    this->output_ctx = nullptr;
    this->video_ctx = nullptr;
    this->audio_ctx = nullptr;

    this->dead_space_buffer = 5;
    this->volume = 0;
    this->volume_threshold_db = -40.0f;

    this->current_segment.start_pts = AV_NOPTS_VALUE;
    this->current_segment.end_pts = AV_NOPTS_VALUE;
    this->current_segment.time_base = { 1, 1000 }; // Default millisecond time base
    this->current_segment.keep = false;

    this->rms_volume = 0.0f;
    this->rms_nb_samples = 0;
    this->rms_channels = 0;

    this->pts = 0;
    this->is_audible = false;

    this->packet = av_packet_alloc();
    this->frame = av_frame_alloc();

    if (!this->frame) {
        throw std::runtime_error("Could not allocate frame.");
    }
    if (!this->packet) {
        throw std::runtime_error("Could not allocate packet.");
    }
}


VideoManager::~VideoManager() {
    avformat_close_input(&this->input_ctx);
    if (this->packet) {
        av_packet_free(&this->packet);  // Frees both the packet and its contents
    }
    if (this->frame) {
        av_frame_free(&this->frame);  // Frees both the frame structure and its contents
    }
}


void VideoManager::openInput() {
    if (avformat_open_input(&this->input_ctx, this->input_file, nullptr, nullptr) < 0) {
        throw std::runtime_error("Could not open input file");
    }
}


void VideoManager::setInputContext() {
    // Open the input file and populate the format context with file information
    this->openInput();

    // Read stream information from the file (detect streams, codecs, etc.)
    if (avformat_find_stream_info(this->input_ctx, nullptr) < 0) {
        throw std::runtime_error("Could not find stream information");
    }
}


void VideoManager::setAudioStreamIndex(int index = -1) {
    if (index == -1) {
        this->audio_stream_idx = -1;
        for (unsigned int i = 0; i < this->input_ctx->nb_streams; i++) {
            if (this->input_ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
                this->audio_stream_idx = i;
                break;
            }
        }

        if (this->audio_stream_idx == -1) {
            throw std::runtime_error("Could not find audio stream");
        }
    }
    else {
        this->audio_stream_idx = index;
    }
}

void VideoManager::setVideoStreamIndex(int index = -1) {
    if (index == -1) {
        for (unsigned int i = 0; i < this->input_ctx->nb_streams; i++) {
            if (this->input_ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
                this->video_stream_idx = i;
            }
        }
    }
    else {
        this->video_stream_idx = index;
    }
}


const AVCodec* VideoManager::getAudioCodec() {
    const AVCodec* audio_codec = avcodec_find_decoder(this->input_ctx->streams[this->audio_stream_idx]->codecpar->codec_id);
    if (!audio_codec) {
        throw std::runtime_error("Could not find audio codec");
        return nullptr;
    }
    return audio_codec;
}


void VideoManager::setAudioCodec() {
    // Allocate codec context
    this->audio_ctx = avcodec_alloc_context3(this->getAudioCodec());
    if (!this->audio_ctx) {
        throw std::runtime_error("Could not allocate audio codec context");
    }
}


void VideoManager::copyAudioCodecParams() {
    // Copy codec parameters based on input format
    if (avcodec_parameters_to_context(this->audio_ctx, this->input_ctx->streams[this->audio_stream_idx]->codecpar) < 0) {
        throw std::runtime_error("Could not copy codec parameters");
    }
}


void VideoManager::openAudioCodec() {
    if (avcodec_open2(this->audio_ctx, this->getAudioCodec(), nullptr) < 0) {
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
    for (unsigned int i = 0; i < this->input_ctx->nb_streams; i++) {
        // Create a new stream in the output file
        AVStream* out_stream = avformat_new_stream(this->output_ctx, nullptr);
        if (!out_stream) {
            throw std::runtime_error("Failed to allocate output stream");
        }

        // Copy the stream parameters
        if (avcodec_parameters_copy(out_stream->codecpar, this->input_ctx->streams[i]->codecpar) < 0) {
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


void VideoManager::setVideoContext() {
    if (this->video_stream_idx >= 0) {
        AVStream* video_stream = this->input_ctx->streams[this->video_stream_idx];
        const AVCodec* decoder = avcodec_find_decoder(video_stream->codecpar->codec_id);
        this->video_ctx = avcodec_alloc_context3(decoder);
        avcodec_parameters_to_context(this->video_ctx, video_stream->codecpar);
        this->video_ctx->pkt_timebase = video_stream->time_base;
        avcodec_open2(this->video_ctx, decoder, NULL);
    }
    else {
        this->video_ctx = nullptr;
    }
}

void VideoManager::setAudiocontext() {
    if (this->audio_stream_idx >= 0) {
        AVStream* audio_stream = (this->input_ctx)->streams[this->audio_stream_idx];
        const AVCodec* decoder = avcodec_find_decoder(audio_stream->codecpar->codec_id);
        this->audio_ctx = avcodec_alloc_context3(decoder);
        avcodec_parameters_to_context(this->audio_ctx, audio_stream->codecpar);
        this->audio_ctx->pkt_timebase = audio_stream->time_base;
        avcodec_open2(this->audio_ctx, decoder, NULL);
    }
    else {
        this->audio_ctx = nullptr;
    }
}


void VideoManager::buildVideo() {
    try {
        avformat_close_input(&this->input_ctx);
        this->setInputContext();
        this->setAudioStreamIndex();
        this->setVideoStreamIndex();
        this->createOutputContext();
        this->createOutputStreams();
        this->setVideoContext();
        this->openOutputFile();
        this->writeFileHeader();
    }
    catch (const std::exception& e) {
        std::cerr << "Setup error: " << e.what() << std::endl;
        return;
    }

    /*
        TODO:
        Reader:
        Read in packet
        Parse RMS of audio in packet
        Append packet into struct passed to queue for output, set "keep" if threshold met
        In queue:
        Queue size dependant on silence buffer size, instantiate in constructor
        Struct should have: packet pointer, and a "keep" bool, start_pts, end_pts
        Function needed to mark the frames from index 0 -> prelude_buffer_index as "keep", decrement a counter as it sees !keep and then resume parse process
    */

    while (av_read_frame(this->input_ctx, this->packet) >= 0) {
        if (packet->stream_index == this->video_stream_idx) {
            // This is compressed video data
            printf("Packet size: %d bytes\n", packet->size);

            // Decode it
            avcodec_send_packet(this->video_ctx, this->packet);

            while (avcodec_receive_frame(this->video_ctx, frame) >= 0) {
                // Now we have uncompressed pixel data
            }
        }
        else if (packet->stream_index == this->audio_stream_idx) {

        }
        av_packet_unref(packet);
    }
}

float VideoManager::calculateRMS(AVFrame* frame, AVCodecContext* audio_codec_ctx) {
    double sum = 0.0;
    int total_samples = 0;
    int channels = audio_codec_ctx->ch_layout.nb_channels;
    int nb_samples = frame->nb_samples;
    enum AVSampleFormat fmt = audio_codec_ctx->sample_fmt;

    // Process all samples based on format
    for (int ch = 0; ch < channels; ch++) {
        for (int s = 0; s < nb_samples; s++) {
            double sample = 0.0;

            // Get sample value based on format
            switch (fmt) {
                // Planar formats (most common)
            case AV_SAMPLE_FMT_FLTP:
                sample = ((float**)frame->extended_data)[ch][s];
                break;
            case AV_SAMPLE_FMT_S16P:
                sample = ((int16_t**)frame->extended_data)[ch][s] / 32768.0;
                break;
            case AV_SAMPLE_FMT_S32P:
                sample = ((int32_t**)frame->extended_data)[ch][s] / 2147483648.0;
                break;

                // Interleaved formats
            case AV_SAMPLE_FMT_FLT:
                sample = ((float*)frame->data[0])[s * channels + ch];
                break;
            case AV_SAMPLE_FMT_S16:
                sample = ((int16_t*)frame->data[0])[s * channels + ch] / 32768.0;
                break;
            case AV_SAMPLE_FMT_S32:
                sample = ((int32_t*)frame->data[0])[s * channels + ch] / 2147483648.0;
                break;

            default:
                continue;
            }

            sum += sample * sample;
            total_samples++;
        }
    }

    // Calculate RMS
    if (total_samples == 0) {
        return -100.0;
    }

    double rms = sqrt(sum / total_samples);

    // Convert to dB
    if (rms <= 0.0) {
        return -100.0;
    }

    return 20.0 * log10(rms);
}