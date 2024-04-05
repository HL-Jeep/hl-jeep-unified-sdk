#include "ImGuiVideoPlayer.h"

#include "cl_dll.h"
#include "imgui.h"
#include "SDL.h"
#include "SDL_audio.h"
#include "SDL_opengl.h"
#include "SDL_input.h"

#define PL_MPEG_IMPLEMENTATION
#include "pl_mpeg.h"
#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"

#include "stb_image.h"

#include <thread>
#include <chrono>

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

void video_decode_callback(plm_t* plm, plm_frame_t* frame, void* user)
{
	CImGuiVideoPlayer* player = (CImGuiVideoPlayer*)user;
	// Do something with frame->y.data, frame->cr.data, frame->cb.data
	plm_frame_to_rgba(frame, player->GetImageBufferPtr(), player->m_image_width * player->m_image_depth);
}

void decode_mpeg(CImGuiVideoPlayer* player)
{
	plm_t* plm;
	plm = plm_create_with_filename((player->m_path + ".mpeg").c_str());
	plm_set_video_decode_callback(plm, video_decode_callback, player);

	player->m_lastTime = std::chrono::high_resolution_clock::now();
	while (!plm_has_ended(plm))
	{
		while (player->m_reading_frame)
		{
			// Wait for rendering to finish before overwriting the buffer
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
			if (player->m_shutdown) break;
		}
		if (player->m_shutdown) break;

		// Decode
		auto stop = std::chrono::high_resolution_clock::now();
		auto time_passed = std::chrono::duration_cast<std::chrono::microseconds>(stop - player->m_lastTime);

		// Check if we need to pause the video (and don't count it in the timer)
		while (player->m_paused || player->m_inactive)
		{
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
			if (player->m_shutdown) break;
		}
		if (player->m_shutdown) break;

		player->m_lastTime = std::chrono::high_resolution_clock::now();
		double time_passed_seconds = time_passed.count() / 1000000.0;
		plm_decode(plm, time_passed_seconds);

		// If we go *too* fast we'll run into floating point issues and never actually decode any frames
		std::this_thread::sleep_for(std::chrono::milliseconds(5));
	}
	plm_destroy(plm);
}

void CImGuiVideoPlayer::Init()
{
	// Create a OpenGL texture identifier
	m_image_texture = (GLuint*) malloc(sizeof(GLuint));
	glGenTextures(1, m_image_texture);
	m_path = "bjork-all-is-full-of-love";
	
	m_first_load = true;
	LoadVideo(m_path);

	m_inactive = true;
}

inline bool file_exists(const std::string& name)
{
	if (FILE* file = fopen(name.c_str(), "r"))
	{
		fclose(file);
		return true;
	}
	else
	{
		return false;
	}
}

bool CImGuiVideoPlayer::LoadVideo(std::string path)
{
	this->m_path = "jeep/media/" + path;
	std::string audio_path = m_path + ".mp3";
	std::string video_path = m_path + ".mp3";

	if (!file_exists(video_path) || !file_exists(audio_path))
		return false;

	// We had a video loaded and it changed, shut down the old stuff
	if (!m_first_load)
	{
		ma_engine_stop(&m_ma_engine);
		m_shutdown = true;
		m_decoder_thread->join();
		delete (m_decoder_thread);
		ma_engine_uninit(&m_ma_engine);
		free(m_image_data);
	}

	m_shutdown = false;
	m_first_load = false;

	ma_result result;
	result = ma_engine_init(NULL, &m_ma_engine);
	if (result == MA_SUCCESS)
	{
		ma_engine_play_sound(&m_ma_engine, audio_path.c_str(), NULL);
		ma_engine_stop(&m_ma_engine);
	}
	else
	{
		fprintf(stderr, "MINIAUDIO ERROR: %d", result);
	}

	// Load the image just to get some stats
	plm_t* plm;
	plm = plm_create_with_filename((m_path + ".mpeg").c_str());

	m_image_width = plm_get_width(plm);
	m_image_height = plm_get_height(plm);
	m_image_depth = 4;
	m_image_data = (unsigned char*)malloc(m_image_width * m_image_height * m_image_depth);

	plm_destroy(plm);

	m_reading_frame = false;
	m_shutdown = false;
	m_decoder_thread = new std::thread(decode_mpeg, this);

	return true;
}

void CImGuiVideoPlayer::Render()
{
	if (m_inactive)
	{
		m_paused = true;
		ma_engine_stop(&m_ma_engine);
		return;
	}

	m_reading_frame = true;
	
	bool ret = LoadImageDataIntoTexture(m_image_data, m_image_width, m_image_height, m_image_texture);

	if (m_lastTimeAudio == gEngfuncs.GetClientTime() && !m_paused)
	{
		// Pause audio if client time isn't advancing
		ma_engine_stop(&m_ma_engine);
		m_paused = true;
	}
	else if (m_lastTimeAudio != gEngfuncs.GetClientTime() && m_paused)
	{
		ma_engine_start(&m_ma_engine);
		m_paused = false;
	}

	if (!m_paused)
	{
		ImGui::Begin("Video Player");
		ImGui::SetWindowSize(ImVec2(1000, 1000));
		ImGui::Image((void*)(intptr_t)*m_image_texture, ImVec2(m_image_width, m_image_height));
		ImGui::End();
		/*ImGui::Begin("Video Player", NULL, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
		ImGuiIO io = ImGui::GetIO();
		ImGui::SetWindowSize(ImVec2(io.DisplaySize.x, io.DisplaySize.y));
		ImGui::SetWindowPos(ImVec2(0,0));
		ImGui::Image((void*)(intptr_t)*m_image_texture, ImVec2(io.DisplaySize.x, io.DisplaySize.y));
		ImGui::End();*/
	}

	m_lastTimeAudio = gEngfuncs.GetClientTime();

	m_reading_frame = false;
}

void CImGuiVideoPlayer::SetActivationState(bool active)
{
	m_inactive = !active;
	m_paused = !active;

	if (active)
		ma_engine_start(&m_ma_engine);
	else
		ma_engine_stop(&m_ma_engine);
}

unsigned char* CImGuiVideoPlayer::GetImageBufferPtr()
{
	return m_image_data;
}

void CImGuiVideoPlayer::Shutdown()
{
	m_shutdown = true;
	m_decoder_thread->join();
	delete (m_decoder_thread);
	free(m_image_data);
	m_image_data = nullptr;
	ma_engine_stop(&m_ma_engine);
	ma_engine_uninit(&m_ma_engine);
	glDeleteTextures(1, m_image_texture);
}