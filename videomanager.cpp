#include "videomanager.h"

#define MAX_STREAMS 10

VideoManager::VideoManager(const char* input_file, const char* output_file) {
	this->input_file = input_file;
	this->output_file = output_file;
	this->audio_stream_idx = -1;
	this->video_stream_idx = -1;

	this->input_ctx = nullptr;
	this->output_ctx = nullptr;
	this->video_codec_ctx = nullptr;
	this->audio_ctx = nullptr;

	this->packets_per_sec = -1;
	this->output_buffer_capacity = -1;
	this->dead_space_buffer = 5;
	this->volume = 0;
	this->volume_threshold_db = -40.0f;

	this->current_segment.start_pts = AV_NOPTS_VALUE;
	this->current_segment.keep = false;
	this->writeOutBufferState = { false, false };
	this->reached_end = 0;

	this->rms_volume = 0.0f;
	this->rms_nb_samples = 0;
	this->rms_channels = 0;

	this->previous_end_pts = 0;
	this->packet_pts_variance = 0;
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

void VideoManager::setVideoContext() {
	if (this->video_stream_idx >= 0) {
		AVStream* video_stream = this->input_ctx->streams[this->video_stream_idx];
		const AVCodec* decoder = avcodec_find_decoder(video_stream->codecpar->codec_id);
		this->video_codec_ctx = avcodec_alloc_context3(decoder);
		avcodec_parameters_to_context(this->video_codec_ctx, video_stream->codecpar);
		this->video_codec_ctx->pkt_timebase = video_stream->time_base;
		avcodec_open2(this->video_codec_ctx, decoder, NULL);
	}
	else {
		this->video_codec_ctx = nullptr;
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

void VideoManager::setPacketsPerSec() {
	this->packets_per_sec = this->getPacketsPerSecond();
	this->output_buffer_capacity = this->packets_per_sec * dead_space_buffer;
}

double VideoManager::getPacketsPerSecond() {
	AVStream* video_stream = this->input_ctx->streams[this->video_stream_idx];

	if (video_stream->avg_frame_rate.num && video_stream->avg_frame_rate.den) {
		return av_q2d(video_stream->avg_frame_rate);
	}

	if (video_stream->r_frame_rate.num && video_stream->r_frame_rate.den) {
		return av_q2d(video_stream->r_frame_rate);
	}

	return 1.0 / av_q2d(video_stream->time_base);
}

bool VideoManager::profilePacketAudio(const AVPacket* original_packet) {
	AVPacket* packet_copy = av_packet_alloc();
	av_packet_ref(packet_copy, original_packet);
	AVFrame* frame = av_frame_alloc();
	bool keep = false;

	while (avcodec_receive_frame(this->audio_ctx, frame)) {
		if (this->volume_threshold_db >= calculateRMS(frame, this->audio_ctx)) {
			keep = true;
			break;
		}
	}

	av_frame_free(&frame);
	av_packet_unref(packet_copy);
	av_packet_free(&packet_copy);
	return keep;
}

float VideoManager::calculateRMS(AVFrame* frame, AVCodecContext* audio_codec_ctx) {
	double sum = 0.0;
	int total_samples = 0;
	int channels = audio_codec_ctx->ch_layout.nb_channels;
	int nb_samples = frame->nb_samples;
	enum AVSampleFormat fmt = audio_codec_ctx->sample_fmt;
	const double INT16_MAX_F = 32768.0;        // 2^15
	const double INT32_MAX_F = 2147483648.0;   // 2^31

	// Process all samples based on format
	for (int ch = 0; ch < channels; ch++) {
		for (int s = 0; s < nb_samples; s++) {
			double sample = 0.0;
			bool valid_sample = true;

			// Get sample value based on format
			switch (fmt) {
				// Planar formats (most common)
			case AV_SAMPLE_FMT_FLTP:
				sample = ((float**)frame->extended_data)[ch][s];
				break;
			case AV_SAMPLE_FMT_S16P:
				sample = ((int16_t**)frame->extended_data)[ch][s] / INT16_MAX_F;
				break;
			case AV_SAMPLE_FMT_S32P:
				sample = ((int32_t**)frame->extended_data)[ch][s] / INT32_MAX_F;
				break;
			case AV_SAMPLE_FMT_DBLP:
				sample = ((double**)frame->extended_data)[ch][s];
				break;

				// Interleaved formats
			case AV_SAMPLE_FMT_FLT:
				sample = ((float*)frame->data[0])[s * channels + ch];
				break;
			case AV_SAMPLE_FMT_S16:
				sample = ((int16_t*)frame->data[0])[s * channels + ch] / INT16_MAX_F;
				break;
			case AV_SAMPLE_FMT_S32:
				sample = ((int32_t*)frame->data[0])[s * channels + ch] / INT32_MAX_F;
				break;
			case AV_SAMPLE_FMT_DBL:
				sample = ((double*)frame->data[0])[s * channels + ch];
				break;

			default:
				valid_sample = false;
				break;
			}

			if (valid_sample) {
				sum += sample * sample;
				total_samples++;
			}
		}
	}

	// Calculate RMS
	if (total_samples == 0) {
		return -100.0f;  // Return minimum dB value if no valid samples
	}

	double rms = sqrt(sum / total_samples);

	// Convert to dB with a floor of -100dB
	return (rms > 0.0) ? 20.0f * log10(rms) : -100.0f;
}

void VideoManager::writeHalfQueue(std::queue<VideoSegment*>* outputBuffer) {
	// Be sure to call free on packets and delete on videosegments!
}

void VideoManager::writeEntireQueue(std::queue<VideoSegment*>* outputBuffer) {
	// Be sure to call free on packets and delete on videosegments!
}

void VideoManager::emptyOutputBuffer(std::queue<VideoSegment*>* outputBuffer) {
	while (!outputBuffer->empty()) {
		while (!outputBuffer->front()->queue.empty()) {
			av_packet_free(&outputBuffer->front()->queue.front());
			outputBuffer->front()->queue.pop();
		}
		delete outputBuffer->front();
		outputBuffer->pop();
	}
}

void VideoManager::emptyOutputQueue() {
	while (!this->outputQueue.empty()) {
		this->emptyOutputBuffer(&this->outputQueue.front());
		this->outputQueue.pop();
	}
}

void VideoManager::invokeQueueSM() {
	//State Machine Writing Output
	if (!this - outputQueue.size() != output_buffer_capacity * 2) {
		switch (writeOutBufferState & 0b00000011) {
		case 0b00:
			break;
		case 0b01:
		case 0b10:
			break;
		case 0b11:
			break;
		}
	}
}

void VideoManager::writeToOutputQueue(std::queue<VideoSegment*> outputBuffer) {
	if (this->outputQueue.empty()) {

	}
	else if (this->outputQueue.size() == 1) {
		this->outputQueue.push(outputBuffer);
	}
	else {

	}
}

void VideoManager::writeOutputBuffer(std::queue<VideoSegment*>* outputBuffer, VideoSegment* current_segment) {
	if (outputBuffer->size() < this->output_buffer_capacity) {
		outputBuffer->push(current_segment);
	}
	else {
		this->writeToOutputQueue(*outputBuffer);
		//TODO: empty FPS buffer
	}
}

void VideoManager::writeOutLoop() {
	VideoSegment* current_segment = nullptr;
	std::queue<VideoSegment*> outputBuffer;
	bool create_new_queue = true;		//TODO: May not need this.
	bool volume_detected = false;
	bool write_to_outputBuffer = false;

	//Loop through the whole input file
	while ((this->reached_end = av_read_frame(this->input_ctx, this->packet)) >= 0) {
		// If EOF is reached, finish the output
		if (this->reached_end == AVERROR_EOF && !outputBuffer.empty()) {
			// TODO: Finalize output writing
		}

		// If packet is video
		if (this->packet->stream_index == this->video_stream_idx) {
			if (create_new_queue) {
				create_new_queue = false;
				if (current_segment != nullptr) {
					writeOutputBuffer(&outputBuffer, current_segment);
				}
				//Start new VideoSegment and set PTS
				current_segment = new VideoSegment();
				current_segment->start_pts = this->packet->pts;
				current_segment->queue.push(this->packet);

			}
		}

		// if packet is audio
		else if (this->packet->stream_index == this->audio_stream_idx) {
			create_new_queue = true;
			if (current_segment != nullptr) {
				current_segment->queue.push(this->packet);
			}

			if (!volume_detected) {
				// Read in audio frames until db threshold is met
				while (avcodec_receive_frame(this->audio_ctx, this->frame)) {
					if (this->volume_threshold_db < this->calculateRMS(frame, this->audio_ctx)) {
						volume_detected = true;
						if (current_segment != nullptr) {
							current_segment->keep = true;
						}
						break;
					}
				}
			}
		}

		else {
			// Unknown packet
		}
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

	//Generate output buffer

}


/*

Flags needed:
WRITE_OUT_PRELUDE
WRITE_OUT_DURATION
WRITE_OUT_POSTLUDE
keep -> fps_queue


Fill write_out_queue
av_read_frame -> Read in 1 fps_queue -> set keep flag
Append write_out_queue
write


*/