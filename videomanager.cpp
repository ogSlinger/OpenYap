/**
 * Copyright (c) 2025 Derek Spaulding
 * Licensed under MIT License - see LICENSE file
 * Requires FFmpeg to be installed separately
 * Users responsible for FFmpeg licensing compliance
 */

#include "videomanager.h"

#define MAX_STREAMS 10

VideoManager::VideoManager(const char* input_file, const char* output_file, float vtdb, float dsb) {
	this->input_file = input_file;
	this->output_file = output_file;
	this->audio_stream_idx = -1;
	this->video_stream_idx = -1;

	this->input_ctx = nullptr;
	this->output_ctx = nullptr;
	this->audio_decoder_ctx = nullptr;
	this->out_pkt_ptr = nullptr;
	this->dead_video_pkt_ptr = nullptr;
	this->dead_audio_pkt_ptr = nullptr;
	this->last_read_video_pkt = nullptr;

	this->dead_space_buffer = dsb;
	this->dead_space_buffer_pts = 0;
	this->buffer_running_duration = 0;
	this->volume_threshold_db = vtdb;

	this->writeOutBufferState = 0;
	this->reached_end = 0;
	this->linear_volume_threshold = 0.0f;

	this->video_pts = 0;;
	this->video_dts = 0;;
	this->audio_pts = 0;;
	this->audio_dts = 0;;
}


VideoManager::~VideoManager() {
	if (input_ctx) { avformat_close_input(&this->input_ctx); }
	if (dead_video_pkt_ptr) { av_packet_free(&this->dead_video_pkt_ptr); }
	if (dead_audio_pkt_ptr) { av_packet_free(&this->dead_audio_pkt_ptr); }
	if (last_read_video_pkt) { av_packet_free(&this->last_read_video_pkt); }
	if (out_pkt_ptr) { av_packet_free(&this->out_pkt_ptr); }
	if (output_ctx) { avformat_free_context(output_ctx); }
}


void VideoManager::openInput() {
	if (avformat_open_input(&this->input_ctx, this->input_file, nullptr, nullptr) < 0) {
		throw std::runtime_error("Could not open input file");
	}
}


void VideoManager::setInputContext() {
	this->openInput();

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

const AVCodec* VideoManager::getVideoCodec() {
	const AVCodec* audio_codec = avcodec_find_decoder(this->input_ctx->streams[this->video_stream_idx]->codecpar->codec_id);
	if (!audio_codec) {
		throw std::runtime_error("Could not find video codec");
		return nullptr;
	}
	return audio_codec;
}


void VideoManager::setAudioCodec() {
	this->audio_decoder_ctx = avcodec_alloc_context3(this->getAudioCodec());
	if (!this->audio_decoder_ctx) {
		throw std::runtime_error("Could not allocate audio codec context");
	}
}

void VideoManager::copyAudioCodecParams() {
	if (avcodec_parameters_to_context(this->audio_decoder_ctx, this->input_ctx->streams[this->audio_stream_idx]->codecpar) < 0) {
		throw std::runtime_error("Could not copy codec parameters");
	}
}

void VideoManager::openAudioCodec() {
	if (avcodec_open2(this->audio_decoder_ctx, this->getAudioCodec(), nullptr) < 0) {
		throw std::runtime_error("Could not open audio codec");
	}
}

void VideoManager::createOutputContext() {
	if (avformat_alloc_output_context2(&this->output_ctx, nullptr, nullptr, this->output_file) < 0) {
		throw std::runtime_error("Could not create output context");
	}
}


void VideoManager::createOutputStreams() {
	for (unsigned int i = 0; i < this->input_ctx->nb_streams; i++) {
		AVStream* out_stream = avformat_new_stream(this->output_ctx, nullptr);
		if (!out_stream) {
			throw std::runtime_error("Failed to allocate output stream");
		}

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
	this->output_ctx->streams[this->video_stream_idx]->time_base.num = this->input_ctx->streams[this->video_stream_idx]->time_base.num;
	this->output_ctx->streams[this->video_stream_idx]->time_base.den = this->input_ctx->streams[this->video_stream_idx]->time_base.den;

	if (avformat_write_header(this->output_ctx, nullptr) < 0) {
		throw std::runtime_error("Error writing header");
	}
}

void VideoManager::writeFileTrailer() {
	if (av_write_trailer(this->output_ctx) < 0) {
		throw std::runtime_error("Error writing trailer");
	}
}

void VideoManager::setAudioDecoder() {
	if (this->audio_stream_idx >= 0) {
		AVStream* audio_stream = this->input_ctx->streams[this->audio_stream_idx];
		const AVCodec* decoder = avcodec_find_decoder(audio_stream->codecpar->codec_id);
		if (!decoder) { throw std::runtime_error("Audio decoder not found"); }

		this->audio_decoder_ctx = avcodec_alloc_context3(decoder);
		if (!this->audio_decoder_ctx) { throw std::runtime_error("Audio decoder not found"); }

		int ret = avcodec_parameters_to_context(this->audio_decoder_ctx, audio_stream->codecpar);
		if (ret < 0) { throw std::runtime_error("Audio decoder parameter copy failure."); }

		this->audio_decoder_ctx->pkt_timebase = audio_stream->time_base;
		ret = avcodec_open2(this->audio_decoder_ctx, decoder, NULL);
		if (ret < 0) { throw std::runtime_error("Could not open audio decoder."); }
	}
	else {
		this->audio_decoder_ctx = nullptr;
	}
}

void VideoManager::secondsToPTS() {
	AVRational time_base = this->input_ctx->streams[this->audio_stream_idx]->time_base;
	this->dead_space_buffer_pts = (int64_t)(this->dead_space_buffer * time_base.den / time_base.num);
}

void VideoManager::calculateLinearScaleThreshold() {
	switch (this->audio_decoder_ctx->sample_fmt) {
	case AV_SAMPLE_FMT_S16:
		this->linear_volume_threshold = pow(10.0f, this->volume_threshold_db / 20.0f) * 32767.0f;
		break;
	case AV_SAMPLE_FMT_S16P:
		this->linear_volume_threshold = pow(10.0f, this->volume_threshold_db / 20.0f) * 32767.0f;
		break;
	case AV_SAMPLE_FMT_FLT:
		this->linear_volume_threshold = pow(10.0f, this->volume_threshold_db / 20.0f);
		break;
	case AV_SAMPLE_FMT_FLTP:
		this->linear_volume_threshold = pow(10.0f, this->volume_threshold_db / 20.0f);
		break;
	case AV_SAMPLE_FMT_S32:
		this->linear_volume_threshold = pow(10.0f, this->volume_threshold_db / 20.0f) * 2147483647.0f;
		break;
	case AV_SAMPLE_FMT_S32P:
		this->linear_volume_threshold = pow(10.0f, this->volume_threshold_db / 20.0f) * 2147483647.0f;
		break;
	case AV_SAMPLE_FMT_U8:
		this->linear_volume_threshold = pow(10.0f, this->volume_threshold_db / 20.0f) * 128.0f;
		break;
	case AV_SAMPLE_FMT_U8P:
		this->linear_volume_threshold = pow(10.0f, this->volume_threshold_db / 20.0f) * 128.0f;
		break;
	case AV_SAMPLE_FMT_DBL:
		this->linear_volume_threshold = pow(10.0f, this->volume_threshold_db / 20.0f);
		break;
	case AV_SAMPLE_FMT_DBLP:
		this->linear_volume_threshold = pow(10.0f, this->volume_threshold_db / 20.0f);
		break;
	default:
		this->linear_volume_threshold = 0.0f;
		break;
	}
}

void VideoManager::calculateFrameAudio(VideoSegment* current_segment, AVPacket* packet) {
	int peak_threshold_count = 0;
	int audio_frame_index = 0;
	int channels = this->audio_decoder_ctx->ch_layout.nb_channels;
	int sample_count = 0;
	int num_increment = 0;
	int divisor = 32;
	bool is_planar = false;
	avcodec_send_packet(this->audio_decoder_ctx, packet);
	AVFrame* frame = av_frame_alloc();

	switch (this->audio_decoder_ctx->sample_fmt) {
	case AV_SAMPLE_FMT_S16P:
		num_increment *= channels;
		is_planar = true;
		__fallthrough;
	case AV_SAMPLE_FMT_S16:
	{
		while (avcodec_receive_frame(this->audio_decoder_ctx, frame) >= 0) {
			int16_t* samples = (int16_t*)frame->data[0];
			sample_count = (is_planar) ? frame->nb_samples : (frame->nb_samples * channels);
			num_increment = (sample_count / divisor);
			current_segment->keep |= processAudioSamples(frame, samples, &channels,
				&num_increment, peak_threshold_count, &this->linear_volume_threshold, &sample_count);
			av_frame_unref(frame);
			peak_threshold_count = 0;
			if (current_segment->keep == true) { break; }
		}
	}
	break;
	case AV_SAMPLE_FMT_FLTP:
		num_increment *= channels;
		is_planar = true;
		__fallthrough;
	case AV_SAMPLE_FMT_FLT:
	{
		while (avcodec_receive_frame(this->audio_decoder_ctx, frame) >= 0) {
			float* samples = (float*)frame->data[0];
			sample_count = (is_planar) ? frame->nb_samples : (frame->nb_samples * channels);
			num_increment = (sample_count / divisor);
			current_segment->keep |= processAudioSamples(frame, samples, &channels,
				&num_increment, peak_threshold_count, &this->linear_volume_threshold, &sample_count);
			av_frame_unref(frame);
			peak_threshold_count = 0;
			if (current_segment->keep == true) { break; }
		}
	}
	break;
	case AV_SAMPLE_FMT_S32P:
		num_increment *= channels;
		is_planar = true;
		__fallthrough;
	case AV_SAMPLE_FMT_S32:
	{
		while (avcodec_receive_frame(this->audio_decoder_ctx, frame) >= 0) {
			int32_t* samples = (int32_t*)frame->data[0];
			sample_count = (is_planar) ? frame->nb_samples : (frame->nb_samples * channels);
			num_increment = (sample_count / divisor);
			current_segment->keep |= processAudioSamples(frame, samples, &channels,
				&num_increment, peak_threshold_count, &this->linear_volume_threshold, &sample_count);
			peak_threshold_count = 0;
			if (current_segment->keep == true) { break; }
		}
	}
	break;
	case AV_SAMPLE_FMT_U8P:
		num_increment *= channels;
		is_planar = true;
		__fallthrough;
	case AV_SAMPLE_FMT_U8:
	{
		while (avcodec_receive_frame(this->audio_decoder_ctx, frame) >= 0) {
			uint8_t* samples = (uint8_t*)frame->data[0];
			sample_count = (is_planar) ? frame->nb_samples : (frame->nb_samples * channels);
			num_increment = (sample_count / divisor);
			current_segment->keep |= processAudioSamples(frame, samples, &channels,
				&num_increment, peak_threshold_count, &this->linear_volume_threshold, &sample_count);
			av_frame_unref(frame);
			peak_threshold_count = 0;
			if (current_segment->keep == true) { break; }
		}
	}
	break;
	case AV_SAMPLE_FMT_DBLP:
		num_increment *= channels;
		is_planar = true;
		__fallthrough;
	case AV_SAMPLE_FMT_DBL:
	{
		while (avcodec_receive_frame(this->audio_decoder_ctx, frame) >= 0) {
			double* samples = (double*)frame->data[0];
			sample_count = (is_planar) ? frame->nb_samples : (frame->nb_samples * channels);
			num_increment = (sample_count / divisor);
			current_segment->keep |= processAudioSamples(frame, samples, &channels,
				&num_increment, peak_threshold_count, &this->linear_volume_threshold, &sample_count);
			av_frame_unref(frame);
			peak_threshold_count = 0;
			if (current_segment->keep == true) { break; }
		}
	}
	break;

	default:
		break;
	}

	av_frame_free(&frame);
}

void VideoManager::emptyOutfileBuffer(std::queue<VideoSegment*>* outputBuffer) {
	bool is_video;
	while (!outputBuffer->empty()) {
		while (!outputBuffer->front()->queue.empty()) {
			is_video = this->outputQueue.front()->front()->queue.front()->stream_index == this->video_stream_idx;
			this->out_pkt_ptr = outputBuffer->front()->queue.front();

			if (is_video) {
				this->out_pkt_ptr->pts = this->video_pts;
				this->out_pkt_ptr->dts = this->video_dts;
				this->video_pts += this->out_pkt_ptr->duration;
				this->video_dts += this->out_pkt_ptr->duration;
			}
			else {
				this->out_pkt_ptr->pts = this->audio_pts;
				this->out_pkt_ptr->dts = this->audio_dts;
				this->audio_pts += this->out_pkt_ptr->duration;
				this->audio_dts += this->out_pkt_ptr->duration;
			}
			av_interleaved_write_frame(this->output_ctx, out_pkt_ptr);
			outputBuffer->front()->queue.pop();
		}

		this->out_pkt_ptr = NULL;
		delete outputBuffer->front();
		outputBuffer->pop();
	}
}

void VideoManager::writeFullQueue() {
	this->writeHalfQueue();
	this->writeHalfQueue();
}

void VideoManager::writeHalfQueue() {
	if (!this->outputQueue.empty()) {
		this->emptyOutfileBuffer(this->outputQueue.front());
		delete this->outputQueue.front();
		this->outputQueue.pop();
	}
}

void VideoManager::purgeBuffer(std::queue<VideoSegment*>* outputBuffer) {
	while (!outputBuffer->empty()) {
		while (!outputBuffer->front()->queue.empty()) {
			av_packet_free(&outputBuffer->front()->queue.front());
			outputBuffer->front()->queue.pop();
		}
		delete outputBuffer->front();
		outputBuffer->pop();
	}
}

void VideoManager::popHalfQueue(std::queue<VideoSegment*>* outputBuffer) {
	while (!outputBuffer->empty()) {
		while (!outputBuffer->front()->queue.empty()) {
			av_packet_free(&outputBuffer->front()->queue.front());
			outputBuffer->front()->queue.pop();
		}
		delete outputBuffer->front();
		outputBuffer->pop();
	}
}

void VideoManager::invokeQueueSM() {
	//State Machine Writing Output
	//this->writeOutBufferState = 0b01;
	switch (this->writeOutBufferState & 0b00000011) {
	case 0b00:
		// Pop one
		if (!outputQueue.empty()) {
			this->popHalfQueue(this->outputQueue.front());
			delete this->outputQueue.front();
			this->outputQueue.pop();
		}
		break;
	case 0b10:
		// Write Both
		this->writeFullQueue();
		this->writeOutBufferState &= 0b000;
		break;
	case 0b01:
	case 0b11:
		//Write 1
		this->writeHalfQueue();
		break;
	}
}

void VideoManager::writeToOutputQueue(std::queue<VideoSegment*>* outputBuffer) {
	if (!outputBuffer->empty()) {
		this->writeOutBufferState <<= 1;
		this->writeOutBufferState &= 0b011;
		this->writeOutBufferState = (outputBuffer->front()->keep) ? (this->writeOutBufferState | 0b1) : (this->writeOutBufferState | 0b0);
		this->outputQueue.push(outputBuffer);
		if (outputQueue.size() == 1) {
			this->writeOutBufferState <<= 1;
			this->writeOutBufferState &= 0b011;
		}
		else {
			this->invokeQueueSM();
		}
		if (this->reached_end == AVERROR_EOF) {
			this->invokeQueueSM();
			this->invokeQueueSM();
		}
	}
}

std::queue<VideoManager::VideoSegment*>* VideoManager::copyOutputBuffer(std::queue<VideoSegment*>* old_outputBuffer) {
	std::queue<VideoSegment*>* new_outputBuffer = new std::queue<VideoSegment*>();
	while (!old_outputBuffer->empty()) {
		new_outputBuffer->push(old_outputBuffer->front());
		old_outputBuffer->pop();
	}
	return new_outputBuffer;
}

void VideoManager::writeOutputBuffer(std::queue<VideoSegment*>* outputBuffer, VideoSegment* current_segment) {
	if (this->reached_end != AVERROR_EOF) {
		outputBuffer->push(current_segment);
		outputBuffer->front()->keep |= current_segment->keep;
	}
	else {
		outputBuffer->push(current_segment);
		outputBuffer->front()->keep |= current_segment->keep;
		std::queue<VideoManager::VideoSegment*>* newBuffer = this->copyOutputBuffer(outputBuffer);
		this->writeToOutputQueue(newBuffer);
		return;
	}

	outputBuffer->front()->ready_to_push = (this->buffer_running_duration >= this->dead_space_buffer_pts) ? true : false;
	if (outputBuffer->front()->ready_to_push) {
		this->buffer_running_duration = 0;
		std::queue<VideoManager::VideoSegment*>* newBuffer = this->copyOutputBuffer(outputBuffer);
		this->writeToOutputQueue(newBuffer);
		return;
	}
}

int64_t VideoManager::get_expected_video_duration() {
	AVStream* video_stream = this->input_ctx->streams[this->video_stream_idx];
	AVRational frame_rate = video_stream->avg_frame_rate;
	if (frame_rate.num == 0 || frame_rate.den == 0) {
		frame_rate = video_stream->r_frame_rate;
	}
	if (frame_rate.num == 0 || frame_rate.den == 0) {
		frame_rate = { 60, 1 };
	}
	int64_t actual_duration = av_rescale_q(1, av_inv_q(frame_rate), video_stream->time_base);

	int64_t duration_60fps = 1500;  // 60 FPS
	int64_t duration_30fps = 3000;  // 30 FPS

	int64_t diff_60fps = abs(actual_duration - duration_60fps);
	int64_t diff_30fps = abs(actual_duration - duration_30fps);

	if (diff_60fps <= diff_30fps) {
		return duration_60fps;
	}
	else {
		return duration_30fps;
	}
}

int64_t VideoManager::get_expected_audio_duration() {
	AVStream* audio_stream = this->input_ctx->streams[this->audio_stream_idx];
	AVCodecParameters* codecpar = audio_stream->codecpar;
	int samples_per_frame;

	switch (codecpar->codec_id) {
	case AV_CODEC_ID_AAC:
		samples_per_frame = 1024;
		break;
	case AV_CODEC_ID_MP3:
		samples_per_frame = 1152;
		break;
	case AV_CODEC_ID_AC3:
		samples_per_frame = 1536;
		break;
	case AV_CODEC_ID_OPUS:
		samples_per_frame = 960;
		break;
	case AV_CODEC_ID_VORBIS:
		samples_per_frame = 1024;
		break;
	case AV_CODEC_ID_PCM_S16LE:
	case AV_CODEC_ID_PCM_S16BE:
	case AV_CODEC_ID_PCM_F32LE:
	case AV_CODEC_ID_PCM_F32BE:
	case AV_CODEC_ID_PCM_S32LE:
	case AV_CODEC_ID_PCM_S32BE:
	case AV_CODEC_ID_PCM_U8:
	case AV_CODEC_ID_PCM_F64LE:
	case AV_CODEC_ID_PCM_F64BE:
		samples_per_frame = codecpar->frame_size > 0 ? codecpar->frame_size : 1024;
		break;
	default:
		samples_per_frame = codecpar->frame_size > 0 ? codecpar->frame_size : 1024;
		break;
	}
	return av_rescale_q(samples_per_frame, { 1, codecpar->sample_rate }, audio_stream->time_base);
}

void VideoManager::timingCheck(bool is_video, AVPacket* packet, std::queue<VideoSegment*>& outputBuffer) {
	if (is_video) {
		if (packet->flags & AV_PKT_FLAG_KEY) {
			if ((packet->pts - (packet->duration * 2)) > this->dead_video_pkt_ptr->pts) {
				this->purgeBuffer(&outputBuffer);
				this->buffer_running_duration = 0;
				//std::queue<VideoManager::VideoSegment*>* newBuffer = this->copyOutputBuffer(&outputBuffer);
				//this->writeToOutputQueue(newBuffer);
				this->writeOutBufferState = 0b00;
				this->invokeQueueSM();
				this->writeOutBufferState = 0b00;
				this->invokeQueueSM();
			}
			this->dead_video_pkt_ptr->pts = packet->pts + packet->duration;
			this->dead_video_pkt_ptr->dts = packet->dts + packet->duration;
		}
		else {
			this->dead_video_pkt_ptr->pts += packet->duration;
			this->dead_video_pkt_ptr->dts += packet->duration;
		}
	}
}

void VideoManager::writeOutLoop() {
	VideoSegment* current_segment = nullptr;
	std::queue<VideoSegment*> outputBuffer;
	bool first_audio_is_ref = false;
	bool is_video = false;
	this->expected_video_duration = this->get_expected_video_duration();
	int64_t expected_audio_duration = this->get_expected_audio_duration();
	this->calculateLinearScaleThreshold();
	
	this->dead_video_pkt_ptr = av_packet_alloc();
	this->last_read_video_pkt = av_packet_alloc();
	this->dead_audio_pkt_ptr = av_packet_alloc();
	if (!this->dead_video_pkt_ptr || !this->dead_audio_pkt_ptr ||!this->last_read_video_pkt) {
		throw std::runtime_error("Packet ptr allocation error.");
	}
	this->dead_video_pkt_ptr->dts = -1000;
	this->dead_audio_pkt_ptr->dts = -1000;
	this->last_read_video_pkt->dts = -1000;
	
	AVPacket* packet = av_packet_alloc();
	this->reached_end = av_read_frame(this->input_ctx, packet);

	// Validate packet 
	while ((!packet) ||  
		((packet->pts == AV_NOPTS_VALUE || packet->dts == AV_NOPTS_VALUE))) {
		if (this->reached_end == AVERROR_EOF) {
			if (this->reached_end == AVERROR_EOF) {
				av_packet_free(&packet);
				if (current_segment != nullptr) {
					writeOutputBuffer(&outputBuffer, current_segment);
				}
			}
			av_packet_free(&this->dead_audio_pkt_ptr);
			av_packet_free(&this->dead_video_pkt_ptr);
			return;
		}
		av_packet_free(&packet);
		packet = av_packet_alloc();
		this->reached_end = av_read_frame(this->input_ctx, packet);
	}
	is_video = (packet->stream_index == this->video_stream_idx) ? true : false;
	this->timingCheck(is_video, packet, outputBuffer);

	//Loop through the whole input file
	while (this->reached_end >= 0) {
		if ((packet->flags & AV_PKT_FLAG_KEY) && (is_video)) {
			if (current_segment != nullptr && !current_segment->queue.empty()) {
				this->writeOutputBuffer(&outputBuffer, current_segment);
			}
			// Reset and push recent packet
			VideoSegment* new_segment = new VideoSegment();
			new_segment->queue.push(packet);
			current_segment = new_segment;
			first_audio_is_ref = false;
		}
		else {
			if (current_segment == nullptr) {
				current_segment = new VideoSegment();
			}
			current_segment->queue.push(packet);
		}
		
		// If packet is audio
		if (!is_video) {
			this->calculateFrameAudio(current_segment, packet);
			this->buffer_running_duration += packet->duration;
		}
		
		// Allocate and read in new packet
		packet = av_packet_alloc();
		this->reached_end = av_read_frame(this->input_ctx, packet);

		// Valid data check
		while ((!packet) ||
			(packet->pts == AV_NOPTS_VALUE) || (packet->dts == AV_NOPTS_VALUE)) {
			if (this->reached_end == AVERROR_EOF) {
				if (this->reached_end == AVERROR_EOF) {
					av_packet_free(&packet);
					if (current_segment != nullptr) {
						writeOutputBuffer(&outputBuffer, current_segment);
					}
				}
				av_packet_free(&this->dead_audio_pkt_ptr);
				av_packet_free(&this->dead_video_pkt_ptr);
				return;
			}
			av_packet_free(&packet);
			packet = av_packet_alloc();
			this->reached_end = av_read_frame(this->input_ctx, packet);
		}
		is_video = (packet->stream_index == this->video_stream_idx) ? true : false;
		this->timingCheck(is_video, packet, outputBuffer);
	}
}

void VideoManager::buildVideo() {
	// Prelude initialization
	try {
		std::cout << "Prelude" << std::endl;
		avformat_close_input(&this->input_ctx);
		this->setInputContext();
		this->setAudioStreamIndex();
		this->setVideoStreamIndex();
		this->setAudioCodec();
		this->copyAudioCodecParams();
		this->openAudioCodec();
		this->createOutputContext();
		this->createOutputStreams();
		this->openOutputFile();
		this->setAudioDecoder();
		this->writeFileHeader();
		this->secondsToPTS();
	}
	catch (const std::exception& e) {
		std::cerr << "Setup error: " << e.what() << std::endl;
		return;
	}

	// Runtime Logic
	try {
		std::cout << "Write Out Loop" << std::endl;
		this->writeOutLoop();
		this->writeFileTrailer();
		std::cout << "Reached End" << std::endl;
	}
	catch (const std::exception& e) {
		std::cerr << "Parse error: " << e.what() << std::endl;
		return;
	}
}

/**
 * Copyright (c) 2025 Derek Spaulding
 * Licensed under MIT License - see LICENSE file
 * Requires FFmpeg to be installed separately
 * Users responsible for FFmpeg licensing compliance
 */
