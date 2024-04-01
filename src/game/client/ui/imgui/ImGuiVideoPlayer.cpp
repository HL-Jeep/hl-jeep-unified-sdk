#include "ImGuiVideoPlayer.h"

#include "cl_dll.h"
#include "imgui.h"
#include "SDL.h"
#include "SDL_audio.h"
#include "SDL_opengl.h"

#define PL_MPEG_IMPLEMENTATION
#include "pl_mpeg.h"
#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"

#include "stb_image.h"

#include <thread>
#include <chrono>

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
	CImGuiVideoPlayer* player = (CImGuiVideoPlayer*)user;
	// Do something with frame->y.data, frame->cr.data, frame->cb.data
	plm_frame_to_rgba(frame, player->GetImageBufferPtr(), 960 * 4);
}

void audio_decode_callback(plm_t* plm, plm_samples_t* frame, void* user)
{
	CImGuiVideoPlayer* player = (CImGuiVideoPlayer*) user;
	// Do something with samples->interleaved
	for (size_t i = 0; i < frame->count; i++)
	{
		player->PushAudioSample(frame->interleaved[i]);
	}
}

void decode_mpeg(CImGuiVideoPlayer* player)
{
	plm_t* plm;
	plm = plm_create_with_filename("jeep/media/bjork-all-is-full-of-love.mpeg");
	plm_set_video_decode_callback(plm, video_decode_callback, player);
	// plm_set_audio_decode_callback(plm, audio_decode_callback, player); // unused

	player->m_lastTime = gEngfuncs.GetClientTime();
	while (!plm_has_ended(plm))
	{
		while (player->m_reading_frame)
		{
			// Wait for rendering to finish before overwriting the buffer
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
		}
		if (player->m_shutdown)
		{
			break;
		}
		float currentTime = gEngfuncs.GetClientTime();
		plm_decode(plm, currentTime - player->m_lastTime);
		player->m_lastTime = currentTime;
		//std::this_thread::sleep_for(std::chrono::milliseconds(5));
	}
	plm_destroy(plm);
}

void CImGuiVideoPlayer::Init()
{
	// Create a OpenGL texture identifier
	m_image_texture = (GLuint*) malloc(sizeof(GLuint));
	glGenTextures(1, m_image_texture);
	glBindTexture(GL_TEXTURE_2D, *m_image_texture);

	m_image_width = 960;
	m_image_height = 540;
	m_image_depth = 4;
	m_image_data = (unsigned char*) malloc(m_image_width * m_image_height * m_image_depth);

	m_reading_frame = false;
	m_shutdown = false;
	m_lastTime = 0;
	m_decoder_thread = new std::thread(decode_mpeg, this);

	ma_result result;

	result = ma_engine_init(NULL, &m_ma_engine);
	if (result == MA_SUCCESS)
	{
		ma_engine_play_sound(&m_ma_engine, "jeep/media/theme.mp3", NULL);
	}
	else
	{
		fprintf(stderr, "MINIAUDIO ERROR: %d", result);
	}
}

void CImGuiVideoPlayer::Render()
{
	m_reading_frame = true;
	SDL_PauseAudio(0);
	
	bool ret = LoadImageDataIntoTexture(m_image_data, m_image_width, m_image_height, m_image_texture);
	
	ImGui::Begin("Video Player");
	ImGui::SetWindowSize(ImVec2(1000, 600));
	ImGui::Image((void*)(intptr_t)*m_image_texture, ImVec2(m_image_width, m_image_height));
	ImGui::End();
	/*ImGui::Begin("Video Player", NULL, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
	ImGuiIO io = ImGui::GetIO();
	ImGui::SetWindowSize(ImVec2(io.DisplaySize.x, io.DisplaySize.y));
	ImGui::SetWindowPos(ImVec2(0,0));
	ImGui::Image((void*)(intptr_t)*m_image_texture, ImVec2(io.DisplaySize.x, io.DisplaySize.y));
	ImGui::End();*/

	m_reading_frame = false;
}

void CImGuiVideoPlayer::PushAudioSample(float sample)
{
	m_audio_samples.push_back(sample);
}

float* CImGuiVideoPlayer::AudioSampleData()
{
	return m_audio_samples.data();
}

size_t CImGuiVideoPlayer::NumAudioSamples()
{
	return m_audio_samples.size();
}

void CImGuiVideoPlayer::ClearAudioSamples()
{
	m_audio_samples.clear();
}

unsigned char* CImGuiVideoPlayer::GetImageBufferPtr()
{
	return m_image_data;
}

void CImGuiVideoPlayer::Shutdown()
{
	m_shutdown = true;
	m_decoder_thread->join();
	free(m_image_data);
	m_image_data = nullptr;
	ma_engine_uninit(&m_ma_engine);
}