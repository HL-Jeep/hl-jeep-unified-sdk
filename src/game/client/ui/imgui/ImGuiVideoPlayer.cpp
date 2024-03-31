#include "ImGuiVideoPlayer.h"

#include "cl_dll.h"
#include "imgui.h"
#include "SDL_opengl.h"

#define PL_MPEG_IMPLEMENTATION
#include "pl_mpeg.h"

#include "stb_image.h"

#include <stdio.h>
#include "libavcodec/avcodec.h"
#include "libavformat/avformat.h"
#include "libavutil/pixdesc.h"
#include "libavutil/hwcontext.h"
#include "libavutil/opt.h"
#include "libavutil/avassert.h"
#include "libavutil/imgutils.h"

static AVBufferRef* hw_device_ctx = NULL;
static enum AVPixelFormat hw_pix_fmt;
static FILE* output_file = NULL;

static int hw_decoder_init(AVCodecContext* ctx, const enum AVHWDeviceType type)
{
	int err = 0;

	if ((err = av_hwdevice_ctx_create(&hw_device_ctx, type,
			 NULL, NULL, 0)) < 0)
	{
		fprintf(stderr, "Failed to create specified HW device.\n");
		return err;
	}
	ctx->hw_device_ctx = av_buffer_ref(hw_device_ctx);

	return err;
}

static enum AVPixelFormat get_hw_format(AVCodecContext* ctx,
	const enum AVPixelFormat* pix_fmts)
{
	const enum AVPixelFormat* p;

	for (p = pix_fmts; *p != -1; p++)
	{
		if (*p == hw_pix_fmt)
			return *p;
	}

	fprintf(stderr, "Failed to get HW surface format.\n");
	return AV_PIX_FMT_NONE;
}

static int decode_write(AVCodecContext* avctx, AVPacket* packet)
{
	AVFrame *frame = NULL, *sw_frame = NULL;
	AVFrame* tmp_frame = NULL;
	uint8_t* buffer = NULL;
	int size;
	int ret = 0;

	ret = avcodec_send_packet(avctx, packet);
	if (ret < 0)
	{
		fprintf(stderr, "Error during decoding\n");
		return ret;
	}

	while (1)
	{
		if (!(frame = av_frame_alloc()) || !(sw_frame = av_frame_alloc()))
		{
			fprintf(stderr, "Can not alloc frame\n");
			ret = AVERROR(ENOMEM);
			goto fail;
		}

		ret = avcodec_receive_frame(avctx, frame);
		if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
		{
			av_frame_free(&frame);
			av_frame_free(&sw_frame);
			return 0;
		}
		else if (ret < 0)
		{
			fprintf(stderr, "Error while decoding\n");
			goto fail;
		}

		if (frame->format == hw_pix_fmt)
		{
			/* retrieve data from GPU to CPU */
			if ((ret = av_hwframe_transfer_data(sw_frame, frame, 0)) < 0)
			{
				fprintf(stderr, "Error transferring the data to system memory\n");
				goto fail;
			}
			tmp_frame = sw_frame;
		}
		else
			tmp_frame = frame;

		size = av_image_get_buffer_size((AVPixelFormat) tmp_frame->format, tmp_frame->width,
			tmp_frame->height, 1);
		buffer = (uint8_t*) av_malloc(size);
		if (!buffer)
		{
			fprintf(stderr, "Can not alloc buffer\n");
			ret = AVERROR(ENOMEM);
			goto fail;
		}
		ret = av_image_copy_to_buffer(buffer, size,
			(const uint8_t* const*)tmp_frame->data,
			(const int*)tmp_frame->linesize, (AVPixelFormat) tmp_frame->format,
			tmp_frame->width, tmp_frame->height, 1);
		if (ret < 0)
		{
			fprintf(stderr, "Can not copy image to buffer\n");
			goto fail;
		}

		if ((ret = fwrite(buffer, 1, size, output_file)) < 0)
		{
			fprintf(stderr, "Failed to dump raw data.\n");
			goto fail;
		}

	fail:
		av_frame_free(&frame);
		av_frame_free(&sw_frame);
		av_freep(&buffer);
		if (ret < 0)
			return ret;
	}
}

int test_libav(int argc, const char* argv[])
{
	AVFormatContext* input_ctx = NULL;
	int video_stream, ret;
	AVStream* video = NULL;
	AVCodecContext* decoder_ctx = NULL;
	const AVCodec* decoder = NULL;
	AVPacket* packet = NULL;
	enum AVHWDeviceType type;
	int i;

	if (argc < 4)
	{
		fprintf(stderr, "Usage: %s <device type> <input file> <output file>\n", argv[0]);
		return -1;
	}

	type = av_hwdevice_find_type_by_name(argv[1]);
	if (type == AV_HWDEVICE_TYPE_NONE)
	{
		fprintf(stderr, "Device type %s is not supported.\n", argv[1]);
		fprintf(stderr, "Available device types:");
		while ((type = av_hwdevice_iterate_types(type)) != AV_HWDEVICE_TYPE_NONE)
			fprintf(stderr, " %s", av_hwdevice_get_type_name(type));
		fprintf(stderr, "\n");
		return -1;
	}

	packet = av_packet_alloc();
	if (!packet)
	{
		fprintf(stderr, "Failed to allocate AVPacket\n");
		return -1;
	}

	/* open the input file */
	if (avformat_open_input(&input_ctx, argv[2], NULL, NULL) != 0)
	{
		fprintf(stderr, "Cannot open input file '%s'\n", argv[2]);
		return -1;
	}

	if (avformat_find_stream_info(input_ctx, NULL) < 0)
	{
		fprintf(stderr, "Cannot find input stream information.\n");
		return -1;
	}

	/* find the video stream information */
	ret = av_find_best_stream(input_ctx, AVMEDIA_TYPE_VIDEO, -1, -1, &decoder, 0);
	if (ret < 0)
	{
		fprintf(stderr, "Cannot find a video stream in the input file\n");
		return -1;
	}
	video_stream = ret;

	for (i = 0;; i++)
	{
		const AVCodecHWConfig* config = avcodec_get_hw_config(decoder, i);
		if (!config)
		{
			fprintf(stderr, "Decoder %s does not support device type %s.\n",
				decoder->name, av_hwdevice_get_type_name(type));
			return -1;
		}
		if (config->methods & AV_CODEC_HW_CONFIG_METHOD_HW_DEVICE_CTX &&
			config->device_type == type)
		{
			hw_pix_fmt = config->pix_fmt;
			break;
		}
	}

	if (!(decoder_ctx = avcodec_alloc_context3(decoder)))
		return AVERROR(ENOMEM);

	video = input_ctx->streams[video_stream];
	if (avcodec_parameters_to_context(decoder_ctx, video->codecpar) < 0)
		return -1;

	decoder_ctx->get_format = get_hw_format;

	if (hw_decoder_init(decoder_ctx, type) < 0)
		return -1;

	if ((ret = avcodec_open2(decoder_ctx, decoder, NULL)) < 0)
	{
		fprintf(stderr, "Failed to open codec for stream #%u\n", video_stream);
		return -1;
	}

	/* open the file to dump raw data */
	output_file = fopen(argv[3], "w+b");

	/* actual decoding and dump the raw data */
	while (ret >= 0)
	{
		if ((ret = av_read_frame(input_ctx, packet)) < 0)
			break;

		if (video_stream == packet->stream_index)
			ret = decode_write(decoder_ctx, packet);

		av_packet_unref(packet);
	}

	/* flush the decoder */
	ret = decode_write(decoder_ctx, NULL);

	if (output_file)
		fclose(output_file);
	av_packet_free(&packet);
	avcodec_free_context(&decoder_ctx);
	avformat_close_input(&input_ctx);
	av_buffer_unref(&hw_device_ctx);

	return 0;
}

// Simple helper function to load an image into a OpenGL texture with common settings
bool LoadImageDataIntoTexture(unsigned char* image_data, int image_width, int image_height, GLuint* image_texture)
{
	glBindTexture(GL_TEXTURE_2D, *image_texture);

	// Setup filtering parameters for display
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE); // This is required on WebGL for non power-of-two textures
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE); // Same

	// Upload pixels into texture
#if defined(GL_UNPACK_ROW_LENGTH) && !defined(__EMSCRIPTEN__)
	glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
#endif

	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, image_width, image_height, 0, GL_RGBA, GL_UNSIGNED_BYTE, image_data);


	return true;
}

bool LoadTextureFromFile(const char* filename, GLuint* out_texture, int* out_width, int* out_height)
{
	// Load from file
	int image_width = 0;
	int image_height = 0;
	unsigned char* image_data = stbi_load(filename, &image_width, &image_height, NULL, 4);
	if (image_data == NULL)
		return false;

	// Create a OpenGL texture identifier
	GLuint image_texture;
	glGenTextures(1, &image_texture);
	glBindTexture(GL_TEXTURE_2D, image_texture);

	// Setup filtering parameters for display
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE); // This is required on WebGL for non power-of-two textures
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE); // Same

	// Upload pixels into texture
#if defined(GL_UNPACK_ROW_LENGTH) && !defined(__EMSCRIPTEN__)
	glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
#endif
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, image_width, image_height, 0, GL_RGBA, GL_UNSIGNED_BYTE, image_data);
	stbi_image_free(image_data);

	*out_texture = image_texture;
	*out_width = image_width;
	*out_height = image_height;

	return true;
}

void video_decode_callback(plm_t* plm, plm_frame_t* frame, void* user)
{
	unsigned char* m_image_data = (unsigned char*)user;
	// Do something with frame->y.data, frame->cr.data, frame->cb.data
	plm_frame_to_rgba(frame, m_image_data, 544*4);
}

void audio_decode_callback(plm_t* plm, plm_samples_t* frame, void* user)
{
	// Do something with samples->interleaved
}

void CImGuiVideoPlayer::Init()
{
	const char *args[] = {"libav", "NONE", "jeep/media/subway_surfers.mpeg", "jeep/media/subway_surfers.test"};
	test_libav(4, args);

	float m_lastTime = gEngfuncs.GetClientTime();
	// Create a OpenGL texture identifier
	m_image_texture = (GLuint*) malloc(sizeof(GLuint));
	glGenTextures(1, m_image_texture);
	glBindTexture(GL_TEXTURE_2D, *m_image_texture);

	m_image_width = 544;
	m_image_height = 960;
	m_image_depth = 4;
	m_image_data = (unsigned char*) malloc(m_image_width * m_image_height * m_image_depth);

	m_plm = plm_create_with_filename("jeep/media/subway_surfers.mpeg");
	plm_set_video_decode_callback(m_plm, video_decode_callback, m_image_data);
	// plm_set_audio_decode_callback(plm, my_audio_callback, my_data);
}

void CImGuiVideoPlayer::Render()
{
	if (!plm_has_ended(m_plm))
	{
		float currentTime = gEngfuncs.GetClientTime();
		plm_decode(m_plm, currentTime - m_lastTime);
		m_lastTime = currentTime;
	}

	bool ret = LoadImageDataIntoTexture(m_image_data, m_image_width, m_image_height, m_image_texture);
	
	ImGui::Begin("Video Player");
	ImGui::SetWindowSize(ImVec2(600, 1000));
	ImGui::Image((void*)(intptr_t)*m_image_texture, ImVec2(m_image_width, m_image_height));
	ImGui::End();
	/*ImGui::Begin("Video Player", NULL, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
	ImGuiIO io = ImGui::GetIO();
	ImGui::SetWindowSize(ImVec2(io.DisplaySize.x, io.DisplaySize.y));
	ImGui::SetWindowPos(ImVec2(0,0));
	ImGui::Image((void*)(intptr_t)*m_image_texture, ImVec2(io.DisplaySize.x, io.DisplaySize.y));
	ImGui::End();*/
}

void CImGuiVideoPlayer::Shutdown()
{
	plm_destroy(m_plm);
	free(m_image_data);
	m_image_data = nullptr;
}