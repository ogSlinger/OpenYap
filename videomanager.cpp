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
	this->out_pkt_ptr = nullptr;

	this->packets_per_sec = -1;
	this->dead_space_buffer = 5;
	this->volume = 0;
	this->volume_threshold_db = -20.0f;

	this->current_segment.start_pts = AV_NOPTS_VALUE;
	this->current_segment.keep = false;
	this->writeOutBufferState = 0;
	this->reached_end = 0;
	this->linear_volume_threshold = 0.0f;

	this->previous_next_pts = 0;
	this->PTS_offset = 0;
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

void VideoManager::writeFileTrailer() {
	if (av_write_trailer(this->output_ctx) < 0) {
		throw std::runtime_error("Error writing trailer");
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

float VideoManager::calculateLinearScaleThreshold() {
	switch (this->audio_ctx->sample_fmt) {
	case AV_SAMPLE_FMT_S16:
	case AV_SAMPLE_FMT_S16P:
		this->linear_volume_threshold = pow(10.0f, this->volume_threshold_db / 20.0f) * 32767.0f;
		break;

	case AV_SAMPLE_FMT_FLT:
	case AV_SAMPLE_FMT_FLTP:
		this->linear_volume_threshold = pow(10.0f, this->volume_threshold_db / 20.0f);
		break;

	case AV_SAMPLE_FMT_S32:
	case AV_SAMPLE_FMT_S32P:
		this->linear_volume_threshold = pow(10.0f, this->volume_threshold_db / 20.0f) * 2147483647.0f;
		break;

	case AV_SAMPLE_FMT_U8:
	case AV_SAMPLE_FMT_U8P:
		this->linear_volume_threshold = pow(10.0f, this->volume_threshold_db / 20.0f) * 127.0f + 128.0f;
		break;

	case AV_SAMPLE_FMT_DBL:
	case AV_SAMPLE_FMT_DBLP:
		this->linear_volume_threshold = pow(10.0f, this->volume_threshold_db / 20.0f);
		break;

	default:
		break;
	}
}

void VideoManager::calculateFrameAudio(VideoSegment* current_segment, AVPacket* packet, int bytes_per_sample) {
	int peak_threshold_count = 0;
	switch (this->audio_ctx->sample_fmt) {
	case AV_SAMPLE_FMT_S16:
	case AV_SAMPLE_FMT_S16P:
	{
		int sample_count = packet->size / bytes_per_sample;
		int16_t* samples = (int16_t*)packet->data;
		for (int audio_frame_index = 0; audio_frame_index < sample_count; audio_frame_index += 8) {
			if (abs(samples[audio_frame_index]) > this->linear_volume_threshold) {
				peak_threshold_count++;
				if (peak_threshold_count == 5) {
					current_segment->keep = true;
					break;
				}
			}
		}
	}
	break;

	case AV_SAMPLE_FMT_FLT:
	case AV_SAMPLE_FMT_FLTP:
	{
		int sample_count = packet->size / bytes_per_sample;
		float* samples = (float*)packet->data;
		for (int audio_frame_index = 0; audio_frame_index < sample_count; audio_frame_index += 8) {
			if (fabs(samples[audio_frame_index]) > this->linear_volume_threshold) {
				peak_threshold_count++;
				if (peak_threshold_count == 5) {
					current_segment->keep = true;
					break;
				}
			}
		}
	}
	break;

	case AV_SAMPLE_FMT_S32:
	case AV_SAMPLE_FMT_S32P:
	{
		int sample_count = packet->size / bytes_per_sample;
		int32_t* samples = (int32_t*)packet->data;
		for (int audio_frame_index = 0; audio_frame_index < sample_count; audio_frame_index += 8) {
			if (abs(samples[audio_frame_index]) > this->linear_volume_threshold) {
				peak_threshold_count++;
				if (peak_threshold_count == 5) {
					current_segment->keep = true;
					break;
				}
			}
		}
	}
	break;

	case AV_SAMPLE_FMT_U8:
	case AV_SAMPLE_FMT_U8P:
	{
		int sample_count = packet->size / bytes_per_sample;
		uint8_t* samples = (uint8_t*)packet->data;
		for (int audio_frame_index = 0; audio_frame_index < sample_count; audio_frame_index += 8) {
			if (abs(samples[audio_frame_index] - 128) > this->linear_volume_threshold) {
				peak_threshold_count++;
				if (peak_threshold_count == 5) {
					current_segment->keep = true;
					break;
				}
			}
		}
	}
	break;

	case AV_SAMPLE_FMT_DBL:
	case AV_SAMPLE_FMT_DBLP:
	{
		int sample_count = packet->size / bytes_per_sample;
		double* samples = (double*)packet->data;
		for (int audio_frame_index = 0; audio_frame_index < sample_count; audio_frame_index += 8) {
			if (fabs(samples[audio_frame_index]) > this->linear_volume_threshold) {
				peak_threshold_count++;
				if (peak_threshold_count == 5) {
					current_segment->keep = true;
					break;
				}
			}
		}
	}
	break;

	default:
		break;
	}
}

void VideoManager::emptyOutfileBuffer(std::queue<VideoSegment*>* outputBuffer) {
	while (!outputBuffer->empty()) {
		while (!outputBuffer->front()->queue.empty()) {
			this->out_pkt_ptr = outputBuffer->front()->queue.front();
			this->out_pkt_ptr->pts -= this->PTS_offset;
			av_interleaved_write_frame(output_ctx, this->out_pkt_ptr);
			av_packet_free(&outputBuffer->front()->queue.front());
			outputBuffer->front()->queue.pop();
		}
		this->out_pkt_ptr = NULL;
		delete outputBuffer->front();
		outputBuffer->pop();

		if (outputBuffer->size() == 1) {
			this->previous_next_pts = outputBuffer->front()->next_pts;
		}
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
	int64_t start_pts = outputBuffer->front()->start_pts;
	while (!outputBuffer->empty()) {
		while (!outputBuffer->front()->queue.empty()) {
			av_packet_free(&outputBuffer->front()->queue.front());
			outputBuffer->front()->queue.pop();
		}
		delete outputBuffer->front();
		outputBuffer->pop();

		if (outputBuffer->size() == 1) {
			this->PTS_offset += (outputBuffer->front()->next_pts - start_pts);
		}
	}
	
}

void VideoManager::invokeQueueSM() {
	//State Machine Writing Output
	switch (this->writeOutBufferState & 0b00000011) {
	case 0b00:
		// Pop one
		this->popHalfQueue(this->outputQueue.front());
		delete this->outputQueue.front();
		this->outputQueue.pop();
		this->writeOutBufferState <<= 1;
		this->writeOutBufferState &= 0b011;
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
	if (this->outputQueue.empty() && (this->reached_end != AVERROR_EOF)) {
		this->writeOutBufferState = (outputBuffer->front()->keep) ? 0b10 : 0b00;
		this->outputQueue.push(outputBuffer);
		return;
	}
	this->writeOutBufferState = (outputBuffer->front()->keep) ? (this->writeOutBufferState | 0b1) : (this->writeOutBufferState | 0b0);
	this->outputQueue.push(outputBuffer);
	this->invokeQueueSM();
}

void VideoManager::emptyFPSBuffer(std::queue<VideoSegment*>* outputBuffer) {
	while (!outputBuffer->empty()) {
		outputBuffer->pop();
	}
}

void VideoManager::writeOutputBuffer(std::queue<VideoSegment*>* outputBuffer, VideoSegment* current_segment) {
	if (this->reached_end != AVERROR_EOF) {
		if (outputBuffer->size() < this->dead_space_buffer) {
			outputBuffer->push(current_segment);
			outputBuffer->front()->keep = outputBuffer->front()->keep || current_segment->keep;
		}
		else {
			std::queue<VideoSegment*>* outputBuffer_ptr = new std::queue<VideoSegment*>(*outputBuffer);
			this->writeToOutputQueue(outputBuffer_ptr);
			this->emptyFPSBuffer(outputBuffer);
		}
	}
	else {
		outputBuffer->push(current_segment);
		outputBuffer->front()->keep = outputBuffer->front()->keep || current_segment->keep;
		std::queue<VideoSegment*>* outputBuffer_ptr = new std::queue<VideoSegment*>(*outputBuffer);
		this->writeToOutputQueue(outputBuffer_ptr);
		this->invokeQueueSM();
		this->invokeQueueSM();
	}
}

void VideoManager::writeOutLoop() {
	VideoSegment* current_segment = nullptr;
	std::queue<VideoSegment*> outputBuffer;
	bool write_to_outputBuffer = false;
	int audio_frame_index = 0;
	int bytes_per_sample = av_get_bytes_per_sample(this->audio_ctx->sample_fmt);
	double packet_duration = 0.0f;
	double running_duration = 0.0f;
	int sample_count = 0;
	double num_audio_packets = 0;
	double running_db_total = 0;
	this->calculateLinearScaleThreshold();

	//Loop through the whole input file
	while ((this->reached_end = av_read_frame(this->input_ctx, this->packet)) >= 0) 
		// If EOF is reached, finish the output
		if (this->reached_end == AVERROR_EOF) {
			if (current_segment != nullptr) {
				writeOutputBuffer(&outputBuffer, current_segment);
			}
			
		}

		// Create new segment if none exists
		if (current_segment == nullptr) {
			//Start new VideoSegment and set PTS
			current_segment = new VideoSegment();
			current_segment->start_pts = this->packet->pts;
			current_segment->queue.push(this->packet);
		}

		// If packet is video
		if (this->packet->stream_index == this->video_stream_idx) {
			if (current_segment != nullptr) {
				// Check audio profile of buffer
				if ((running_db_total / num_audio_packets) > this->volume_threshold_db) {
					if (current_segment != nullptr) {
						current_segment->keep = true;
					}
				}
			}
		}

		// if packet is audio
		else if (this->packet->stream_index == this->audio_stream_idx) {
			this->calculateFrameAudio(current_segment, packet, bytes_per_sample);
			current_segment->queue.push(this->packet);
			
		}

		// If PTS exceeds, push and reset
		packet_duration = packet->duration * av_q2d(input_ctx->streams[packet->stream_index]->time_base);
		if (running_duration + packet_duration > 1.0f) {
			current_segment->next_pts = this->packet->pts;
			writeOutputBuffer(&outputBuffer, current_segment);
			running_duration = 0.0f;
			current_segment = new VideoSegment();
			current_segment->start_pts = this->packet->pts;
			current_segment->queue.push(this->packet);
		}
		else {
			running_duration += packet_duration;
			writeOutputBuffer(&outputBuffer, current_segment);
		}
}

void VideoManager::buildVideo() {
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
		this->setVideoContext();
		this->openOutputFile();
		this->writeFileHeader();
	}
	catch (const std::exception& e) {
		std::cerr << "Setup error: " << e.what() << std::endl;
		return;
	}

	// Runtime Logic
	try {
		this->writeOutLoop();
		this->writeFileTrailer();
	}
	catch (const std::exception& e) {
		std::cerr << "Parse error: " << e.what() << std::endl;
		return;
	}
}