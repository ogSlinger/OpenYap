//© 2025[Derek Spaulding].All rights reserved.
#include "videomanager.h"

#define MAX_STREAMS 10


VideoManager::VideoManager(const char* input_file, const char* output_file) {
	this->input_file = input_file;
	this->output_file = output_file;
	this->audio_stream_idx = -1;
	this->video_stream_idx = -1;

	this->input_ctx = nullptr;
	this->output_ctx = nullptr;
	this->audio_decoder_ctx = nullptr;
	this->out_pkt_ptr = nullptr;

	this->packets_per_sec = -1;
	this->dead_space_buffer = 1.5f;
	this->dead_space_buffer_pts = 0;
	this->buffer_running_duration = 0;
	this->volume_threshold_db = -20.0f;

	this->current_segment.keep = false;
	this->writeOutBufferState = 0;
	this->reached_end = 0;
	this->linear_volume_threshold = 0.0f;

	this->video_pts_offset = 0;
	this->audio_pts_offset = 0;
	this->video_dts_offset = 0;
	this->audio_dts_offset = 0;
}


VideoManager::~VideoManager() {
	avformat_close_input(&this->input_ctx);
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
			num_increment = (sample_count / 8);
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
			num_increment = (sample_count / 8);
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
			num_increment = (sample_count / 8);
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
			num_increment = (sample_count / 8);
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
			num_increment = (sample_count / 8);
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
				this->out_pkt_ptr->pts -= this->video_pts_offset;
				this->out_pkt_ptr->dts -= this->video_dts_offset;
			}
			else {
				this->out_pkt_ptr->pts -= this->audio_pts_offset;
				this->out_pkt_ptr->dts -= this->audio_dts_offset;
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

void VideoManager::popHalfQueue(std::queue<VideoSegment*>* outputBuffer) {
	while (!outputBuffer->empty()) {
		while (!outputBuffer->front()->queue.empty()) {
			if (outputBuffer->front()->queue.front()->stream_index == this->video_stream_idx) {
				this->video_pts_offset += outputBuffer->front()->queue.front()->duration;
				this->video_dts_offset += outputBuffer->front()->queue.front()->duration;
			}
			else {
				this->audio_pts_offset += outputBuffer->front()->queue.front()->duration;
				this->audio_dts_offset += outputBuffer->front()->queue.front()->duration;
			}
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
			this->writeOutBufferState <<= 1;
			this->writeOutBufferState &= 0b011;
		}
		break;
	case 0b01:
	case 0b10:
		// Write Both
		this->writeFullQueue();
		this->writeOutBufferState = 0;
		break;
	case 0b11:
		//Write 1
		this->writeHalfQueue();
		this->writeOutBufferState <<= 1;
		this->writeOutBufferState &= 0b011;
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
			this->writeOutBufferState <<= 0b1;
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

int64_t VideoManager::ptsCheck(AVPacket* last_pkt_ptr = nullptr, AVPacket* packet = nullptr) {
	int64_t discrepency = 0;
	
	if ((packet) && (last_pkt_ptr != nullptr)) {
		if (last_pkt_ptr->dts >= 0) {
			if 	((abs(packet->pts - last_pkt_ptr->pts) > (last_pkt_ptr->duration * 1.5)) ||
				((abs(packet->dts - last_pkt_ptr->dts) > (last_pkt_ptr->duration * 1.5)))) {
				std::cout << "=============================================" << std::endl;
				std::cout << "Last packet - PTS: " << last_pkt_ptr->pts
					<< ", DTS: " << last_pkt_ptr->dts
					<< ", Duration: " << last_pkt_ptr->duration << std::endl;
				std::cout << "Current packet - PTS: " << packet->pts
					<< ", DTS: " << packet->dts
					<< ", Duration: " << packet->duration << std::endl;
				std::cout << "PTS gap: " << (packet->pts - last_pkt_ptr->pts)
					<< ", DTS gap: " << (packet->dts - last_pkt_ptr->dts) << std::endl;
				std::cout << "Discrepency before: " << discrepency << std::endl;

				discrepency = packet->pts;
				if (packet->stream_index == this->video_stream_idx) {
					packet->pts = (last_pkt_ptr->pts + last_pkt_ptr->duration);
					packet->dts = (last_pkt_ptr->dts + last_pkt_ptr->duration);
					discrepency -= packet->pts;
					std::cout << "This is Video" << std::endl;
				}
				else {
					packet->pts = (last_pkt_ptr->pts + last_pkt_ptr->duration);
					packet->dts = (last_pkt_ptr->dts + last_pkt_ptr->duration);
					discrepency -= packet->pts;
					std::cout << "This is audio" << std::endl;
				}

				std::cout << "Discrepency after: " << discrepency << std::endl;
				std::cout << "Last packet - PTS: " << last_pkt_ptr->pts
					<< ", DTS: " << last_pkt_ptr->dts
					<< ", Duration: " << last_pkt_ptr->duration << std::endl;
				std::cout << "Current packet - PTS: " << packet->pts
					<< ", DTS: " << packet->dts
					<< ", Duration: " << packet->duration << std::endl;
				std::cout << "PTS gap: " << (packet->pts - last_pkt_ptr->pts)
					<< ", DTS gap: " << (packet->dts - last_pkt_ptr->dts) << std::endl;
				std::cout << "=============================================" << std::endl;
				return discrepency;
			}
		}
	}
	return 0;
}

void VideoManager::writeOutLoop() {
	VideoSegment* current_segment = nullptr;
	std::queue<VideoSegment*> outputBuffer;
	int64_t video_running_discrepency = 0;
	int64_t audio_running_discrepency = 0;
	this->calculateLinearScaleThreshold();
	bool first_audio_is_ref = false;
	bool is_video = false;
	
	AVPacket* packet = av_packet_alloc();
	AVPacket* last_video_pkt_ptr = av_packet_alloc();
	AVPacket* last_audio_pkt_ptr = av_packet_alloc();
	if (!last_video_pkt_ptr || !last_audio_pkt_ptr) {
		throw std::runtime_error("Packet ptr allocation error.");
	}

	last_video_pkt_ptr->dts = -1000;
	last_audio_pkt_ptr->dts = -1000;

	this->reached_end = av_read_frame(this->input_ctx, packet);
	// Valid data check
	while (((!packet) ||  
		((packet->pts == AV_NOPTS_VALUE || packet->dts == AV_NOPTS_VALUE)) && (this->reached_end != AVERROR_EOF))) {
		std::cout << "INVALID PACKET" << std::endl;
		av_packet_free(&packet);
		packet = av_packet_alloc();
		this->reached_end = av_read_frame(this->input_ctx, packet);
	}
	is_video = (packet->stream_index == this->video_stream_idx) ? true : false;

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
			// May not need, but in future uncomment if audio keyframes are not first audio packet in queue
			if ((packet->stream_index == this->audio_stream_idx) && (!(packet->flags & AV_PKT_FLAG_KEY)) && (!first_audio_is_ref)) {
				std::cout << "First audio packet was not keyframed first" << std::endl;
				first_audio_is_ref = true;
				this->reached_end = av_read_frame(this->input_ctx, packet);
				continue;
			}
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
		while (((!packet) ||
			(packet->pts == AV_NOPTS_VALUE) || (packet->dts == AV_NOPTS_VALUE) && (this->reached_end != AVERROR_EOF))) {
			std::cout << "INVALID PACKET" << std::endl;
			av_packet_free(&packet);
			packet = av_packet_alloc();
			this->reached_end = av_read_frame(this->input_ctx, packet);
		}

		// End of file logic
		if (this->reached_end == AVERROR_EOF) {
			av_packet_free(&packet);
			if (current_segment != nullptr) {
				writeOutputBuffer(&outputBuffer, current_segment);
			}
		}
		else {
			is_video = (packet->stream_index == this->video_stream_idx) ? true : false;

			//PTS Check
			if (is_video) {
				if (packet->flags & AV_PKT_FLAG_KEY) {
					//std::cout << "Keyframed Video" << std::endl;
					video_running_discrepency = 0;
					video_running_discrepency = this->ptsCheck(last_video_pkt_ptr, packet);
					last_video_pkt_ptr->pts = packet->pts;
					last_video_pkt_ptr->dts = packet->dts;
					last_video_pkt_ptr->duration = packet->duration;
				}
				else {
					packet->pts += video_running_discrepency;
					packet->dts += video_running_discrepency;
				}
			}

			else {
				if (packet->flags & AV_PKT_FLAG_KEY) {
					//std::cout << "Audio" << std::endl;
					audio_running_discrepency = 0;
					audio_running_discrepency = this->ptsCheck(last_audio_pkt_ptr, packet);
					last_audio_pkt_ptr->pts = packet->pts;
					last_audio_pkt_ptr->dts = packet->dts;
					last_audio_pkt_ptr->duration = packet->duration;
				}
				else {
					packet->pts += audio_running_discrepency;
					packet->dts += audio_running_discrepency;
				}
			}
		}
	}
	av_packet_free(&last_audio_pkt_ptr);
	av_packet_free(&last_video_pkt_ptr);
}

void VideoManager::buildVideo() {
	// av_log_set_level(AV_LOG_DEBUG);
	// Prelude initialization
	try {
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
		this->writeOutLoop();
		this->writeFileTrailer();
		std::cout << "Reached End" << std::endl;
	}
	catch (const std::exception& e) {
		std::cerr << "Parse error: " << e.what() << std::endl;
		return;
	}

}

//© 2025[Derek Spaulding].All rights reserved.