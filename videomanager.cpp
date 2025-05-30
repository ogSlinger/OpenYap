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
	this->volume_threshold_db = -10.0f;

	this->current_segment.start_pts = AV_NOPTS_VALUE;
	this->current_segment.start_dts = AV_NOPTS_VALUE;
	this->current_segment.keep = false;
	this->writeOutBufferState = 0;
	this->reached_end = 0;
	this->linear_volume_threshold = 0.0f;

	this->PTS_offset = 0;
	this->DTS_offset = 0;
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

void VideoManager::calculateLinearScaleThreshold(int& bytes_per_sample) {
	switch (this->audio_ctx->sample_fmt) {
	case AV_SAMPLE_FMT_U8:
	case AV_SAMPLE_FMT_U8P:
		bytes_per_sample = 1;
		break;
	case AV_SAMPLE_FMT_S16:
	case AV_SAMPLE_FMT_S16P:
		bytes_per_sample = 2;
		break;
	case AV_SAMPLE_FMT_S32:
	case AV_SAMPLE_FMT_S32P:
	case AV_SAMPLE_FMT_FLT:
	case AV_SAMPLE_FMT_FLTP:
		bytes_per_sample = 4;
		break;
	case AV_SAMPLE_FMT_DBL:
	case AV_SAMPLE_FMT_DBLP:
		bytes_per_sample = 8;
		break;
	}

	switch (this->audio_ctx->sample_fmt) {
	case AV_SAMPLE_FMT_S16:
		this->linear_volume_threshold = pow(10.0f, this->volume_threshold_db / 20.0f) * 32767.0f;
		bytes_per_sample *= this->audio_ctx->ch_layout.nb_channels;
		break;
	case AV_SAMPLE_FMT_S16P:
		this->linear_volume_threshold = pow(10.0f, this->volume_threshold_db / 20.0f) * 32767.0f;
		break;
	case AV_SAMPLE_FMT_FLT:
		this->linear_volume_threshold = pow(10.0f, this->volume_threshold_db / 20.0f);
		bytes_per_sample *= this->audio_ctx->ch_layout.nb_channels;
		break;
	case AV_SAMPLE_FMT_FLTP:
		this->linear_volume_threshold = pow(10.0f, this->volume_threshold_db / 20.0f);
		break;
	case AV_SAMPLE_FMT_S32:
		this->linear_volume_threshold = pow(10.0f, this->volume_threshold_db / 20.0f) * 2147483647.0f;
		bytes_per_sample *= this->audio_ctx->ch_layout.nb_channels;
		break;
	case AV_SAMPLE_FMT_S32P:
		this->linear_volume_threshold = pow(10.0f, this->volume_threshold_db / 20.0f) * 2147483647.0f;
		break;
	case AV_SAMPLE_FMT_U8:
		// U8 is unsigned: 0-255, with 128 as zero point
		this->linear_volume_threshold = pow(10.0f, this->volume_threshold_db / 20.0f) * 128.0f;
		bytes_per_sample *= this->audio_ctx->ch_layout.nb_channels;
		break;
	case AV_SAMPLE_FMT_U8P:
		this->linear_volume_threshold = pow(10.0f, this->volume_threshold_db / 20.0f) * 128.0f;
		break;
	case AV_SAMPLE_FMT_DBL:
		this->linear_volume_threshold = pow(10.0f, this->volume_threshold_db / 20.0f);
		bytes_per_sample *= this->audio_ctx->ch_layout.nb_channels;
		break;
	case AV_SAMPLE_FMT_DBLP:
		this->linear_volume_threshold = pow(10.0f, this->volume_threshold_db / 20.0f);
		break;
	default:
		this->linear_volume_threshold = 0.0f;
		break;
	}
}

void VideoManager::calculateFrameAudio(VideoSegment* current_segment, AVPacket* packet, int bytes_per_sample) {
	int peak_threshold_count = 0;
	int channels = 0;
	int sample_count = 0;
	avcodec_send_packet(this->audio_ctx, packet);
	AVFrame* frame = av_frame_alloc();

	switch (this->audio_ctx->sample_fmt) {
	case AV_SAMPLE_FMT_S16:
	case AV_SAMPLE_FMT_S16P:
	{
		int16_t* samples = 0;
		int num_increment = 0;
		while (avcodec_receive_frame(this->audio_ctx, frame) >= 0) {
			channels = frame->ch_layout.nb_channels;
			sample_count = frame->nb_samples * channels;
			samples = (int16_t*)frame->data;
			num_increment = 8 * channels;

			for (int audio_frame_index = 0; audio_frame_index < sample_count; audio_frame_index += num_increment) {
				if (abs(samples[audio_frame_index]) > this->linear_volume_threshold) {
					peak_threshold_count++;
					if (peak_threshold_count == 5) {
						current_segment->keep = true;
						break;
					}
				}
			}
			av_frame_unref(frame);
			if (current_segment->keep == true) { break; }
		}
	}
	break;
	
	case AV_SAMPLE_FMT_FLT:
	case AV_SAMPLE_FMT_FLTP:
	{
		float* samples = 0;
		int num_increment = 0;
		std::cout << ">>>>>New Audio Packet Read<<<<<" << std::endl;
		while (avcodec_receive_frame(this->audio_ctx, frame) >= 0) {
			channels = frame->ch_layout.nb_channels;
			sample_count = frame->nb_samples * channels;
			samples = (float*)frame->data[0];
			num_increment = 8 * channels;
			std::cout << ":::::New Audio Frame Read:::::" << std::endl;
			for (int audio_frame_index = 0; audio_frame_index < sample_count; audio_frame_index += num_increment) {
				std::cout << "Audio Sample Data: " << fabs(samples[audio_frame_index]) << " || Volume Threshold: " << this->linear_volume_threshold << std::endl;
				if (fabs(samples[audio_frame_index]) > this->linear_volume_threshold) {
					peak_threshold_count++;
					if (peak_threshold_count == 5) {
						current_segment->keep = true;
						break;
					}
				}
			}
			av_frame_unref(frame);
			if (current_segment->keep == true) { break; }
		}
	}
	break;

	case AV_SAMPLE_FMT_S32:
	case AV_SAMPLE_FMT_S32P:
	{
		int32_t* samples = 0;
		int num_increment = 0;
		while (avcodec_receive_frame(this->audio_ctx, frame) >= 0) {
			channels = frame->ch_layout.nb_channels;
			sample_count = frame->nb_samples * channels;
			samples = (int32_t*)frame->data;
			num_increment = 8 * channels;

			for (int audio_frame_index = 0; audio_frame_index < sample_count; audio_frame_index += num_increment) {
				if (abs(samples[audio_frame_index]) > this->linear_volume_threshold) {
					peak_threshold_count++;
					if (peak_threshold_count == 5) {
						current_segment->keep = true;
						break;
					}
				}
			}
			av_frame_unref(frame);
			if (current_segment->keep == true) { break; }
		}
	}
	break;

	case AV_SAMPLE_FMT_U8:
	case AV_SAMPLE_FMT_U8P:
	{
		uint8_t* samples = 0;
		int num_increment = 0;
		while (avcodec_receive_frame(this->audio_ctx, frame) >= 0) {
			channels = frame->ch_layout.nb_channels;
			sample_count = frame->nb_samples * channels;
			samples = (uint8_t*)frame->data;
			num_increment = 8 * channels;

			for (int audio_frame_index = 0; audio_frame_index < sample_count; audio_frame_index += num_increment) {
				if (abs(samples[audio_frame_index] - 128) > this->linear_volume_threshold) {
					peak_threshold_count++;
					if (peak_threshold_count == 5) {
						current_segment->keep = true;
						break;
					}
				}
			}
			av_frame_unref(frame);
			if (current_segment->keep == true) { break; }
		}
	}
	break;

	case AV_SAMPLE_FMT_DBL:
	case AV_SAMPLE_FMT_DBLP:
	{
		double* samples = 0;
		int num_increment = 0;
		while (avcodec_receive_frame(this->audio_ctx, frame) >= 0) {
			channels = frame->ch_layout.nb_channels;
			sample_count = frame->nb_samples * channels;
			samples = (double*)frame->data;
			num_increment = 8 * channels;

			for (int audio_frame_index = 0; audio_frame_index < sample_count; audio_frame_index += num_increment) {
				if (fabs(samples[audio_frame_index]) > this->linear_volume_threshold) {
					peak_threshold_count++;
					if (peak_threshold_count == 5) {
						current_segment->keep = true;
						break;
					}
				}
			}
			av_frame_unref(frame);
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
	while (!outputBuffer->empty()) {
		while (!outputBuffer->front()->queue.empty()) {
			this->out_pkt_ptr = outputBuffer->front()->queue.front();
			this->out_pkt_ptr->pts -= this->PTS_offset;
			this->out_pkt_ptr->dts -= this->DTS_offset;
			/*std::cout << "=======WRITING OUT Packet======= " << std::endl;
			std::cout << "PTS: " << outputBuffer->front()->queue.front()->pts << std::endl;
			std::cout << "DTS: " << outputBuffer->front()->queue.front()->dts << std::endl;
			std::cout << "PTS OFFSET: " << this->PTS_offset << std::endl;
			std::cout << "DTS OFFSET: " << this->DTS_offset << std::endl;
			std::cout << "Queue Size: " << outputBuffer->front()->queue.size() << std::endl;*/
			av_interleaved_write_frame(this->output_ctx, outputBuffer->front()->queue.front());
			av_packet_unref(outputBuffer->front()->queue.front());
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
	int64_t start_pts = outputBuffer->front()->start_pts;
	int64_t start_dts = outputBuffer->front()->start_dts;
	while (!outputBuffer->empty()) {
		while (!outputBuffer->front()->queue.empty()) {
			av_packet_free(&outputBuffer->front()->queue.front());
			outputBuffer->front()->queue.pop();
		}
		delete outputBuffer->front();
		outputBuffer->pop();

		if (outputBuffer->size() == 1) {
			this->PTS_offset += (outputBuffer->front()->next_pts - start_pts);
			this->DTS_offset += (outputBuffer->front()->next_dts - start_dts);
		}
	}

}

void VideoManager::invokeQueueSM() {
	std::cout << "Before SM : "
		<< ((this->writeOutBufferState & 4) ? '1' : '0')
		<< ((this->writeOutBufferState & 2) ? '1' : '0')
		<< ((this->writeOutBufferState & 1) ? '1' : '0')
		<< std::endl;
	//State Machine Writing Output
	switch (this->writeOutBufferState & 0b00000011) {
	case 0b00:
		// Pop one
		if (!outputQueue.empty()) {
			this->popHalfQueue(this->outputQueue.front());
			delete this->outputQueue.front();
			this->outputQueue.pop();
			this->writeOutBufferState <<= 1;
			this->writeOutBufferState &= 0b011;
			std::cout << "After Shift 0b00 : "
				<< ((this->writeOutBufferState & 4) ? '1' : '0')
				<< ((this->writeOutBufferState & 2) ? '1' : '0')
				<< ((this->writeOutBufferState & 1) ? '1' : '0')
				<< std::endl;
		}
		break;
	case 0b01:
	case 0b10:
		// Write Both
		this->writeFullQueue();
		this->writeOutBufferState = 0;
		std::cout << "After 0b01 or 0b10 Shift : "
			<< ((this->writeOutBufferState & 4) ? '1' : '0')
			<< ((this->writeOutBufferState & 2) ? '1' : '0')
			<< ((this->writeOutBufferState & 1) ? '1' : '0')
			<< std::endl;
		break;
	case 0b11:
		//Write 1
		this->writeHalfQueue();
		this->writeOutBufferState <<= 1;
		this->writeOutBufferState &= 0b011;
		std::cout << "After 0b11 : "
			<< ((this->writeOutBufferState & 4) ? '1' : '0')
			<< ((this->writeOutBufferState & 2) ? '1' : '0')
			<< ((this->writeOutBufferState & 1) ? '1' : '0')
			<< std::endl;
		break;
	}
}

void VideoManager::writeToOutputQueue(std::queue<VideoSegment*>* outputBuffer) {
	if (!outputBuffer->empty()) {
		this->writeOutBufferState <<= 1;
		this->writeOutBufferState &= 0b011;
		this->writeOutBufferState = (outputBuffer->front()->keep) ? (this->writeOutBufferState | 0b1) : (this->writeOutBufferState | 0b0);
		std::cout << "Adjusting before sending to SM : "
			<< ((this->writeOutBufferState & 4) ? '1' : '0')
			<< ((this->writeOutBufferState & 2) ? '1' : '0')
			<< ((this->writeOutBufferState & 1) ? '1' : '0')
			<< " TRUE? " << outputBuffer->front()->keep
			<< " PTS? " << outputBuffer->front()->start_pts
			<< std::endl;
		this->outputQueue.push(outputBuffer);
		if (outputQueue.size() == 1) {
			this->writeOutBufferState <<= 0b1;
			this->writeOutBufferState &= 0b011;
		}
		else {
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
		outputBuffer->front()->keep = outputBuffer->front()->keep || current_segment->keep;
	}
	else {
		outputBuffer->push(current_segment);
		outputBuffer->front()->keep = outputBuffer->front()->keep || current_segment->keep;
		std::queue<VideoManager::VideoSegment*>* newBuffer = this->copyOutputBuffer(outputBuffer);
		this->writeToOutputQueue(newBuffer);
		return;
	}

	outputBuffer->front()->ready_to_push = (outputBuffer->size() >= this->dead_space_buffer) ? true : false;
	if (!outputBuffer->empty() && outputBuffer->front()->ready_to_push) {
		std::queue<VideoManager::VideoSegment*>* newBuffer = this->copyOutputBuffer(outputBuffer);
		this->writeToOutputQueue(newBuffer);
		return;
	}
}

void VideoManager::writeOutLoop() {
	VideoSegment* current_segment = nullptr;
	std::queue<VideoSegment*> outputBuffer;
	bool push_clear_buffer = false;
	int audio_frame_index = 0;
	int bytes_per_sample = av_get_bytes_per_sample(this->audio_ctx->sample_fmt);
	double packet_duration = 0.0f;
	double running_duration = 0.0f;
	int sample_count = 0;
	double num_audio_packets = 0;
	double running_db_total = 0;
	this->calculateLinearScaleThreshold(bytes_per_sample);

	//Loop through the whole input file
	while ((this->reached_end = av_read_frame(this->input_ctx, this->packet)) >= 0) {

		//		DEBUG
		/*std::cout << "=== READING IN PACKET INFO ===" << std::endl;
		std::cout << "Stream Index: " << this->packet->stream_index << std::endl;
		std::cout << "PTS: " << this->packet->pts << std::endl;
		std::cout << "DTS: " << this->packet->dts << std::endl;
		std::cout << "Duration: " << this->packet->duration << std::endl;
		std::cout << "Size: " << this->packet->size << " bytes" << std::endl;
		std::cout << "Flags: " << (this->packet->flags & AV_PKT_FLAG_KEY ? "KEY " : "") <<
			(this->packet->flags & AV_PKT_FLAG_CORRUPT ? "CORRUPT " : "") << std::endl;
		std::cout << "-------------------" << std::endl;*/

		push_clear_buffer = (this->packet->flags & AV_PKT_FLAG_KEY);

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
			current_segment->start_dts = this->packet->dts;
			current_segment->queue.push(this->packet);
		}

		// if packet is audio
		if (this->packet->stream_index == this->audio_stream_idx) {
			this->calculateFrameAudio(current_segment, packet, bytes_per_sample);
		}

		// If PTS exceeds, push and reset
		packet_duration = packet->duration * av_q2d(input_ctx->streams[packet->stream_index]->time_base);
		if (running_duration + packet_duration > 1.0f && push_clear_buffer) {
			std::cout << "=========STARTING NEW VIDEOSEGMENT==============" << std::endl;
			current_segment->next_pts = this->packet->pts;
			current_segment->next_dts = this->packet->dts;
			this->writeOutputBuffer(&outputBuffer, current_segment);

			// Reset and push recent packet
			running_duration = 0.0f;
			current_segment = new VideoSegment();
			current_segment->start_pts = this->packet->pts;
			current_segment->start_dts = this->packet->dts;
			current_segment->queue.push(this->packet);
		}
		else {
			running_duration += packet_duration;
			current_segment->queue.push(this->packet);
		}
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
		std::cout << "Reached End";
	}
	catch (const std::exception& e) {
		std::cerr << "Parse error: " << e.what() << std::endl;
		return;
	}
}