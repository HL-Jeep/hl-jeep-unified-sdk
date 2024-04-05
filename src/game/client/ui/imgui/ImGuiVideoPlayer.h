#pragma once

#include "pl_mpeg.h"
#include "stdint.h"
#include "SDL.h"
#include "SDL_opengl.h"
#include "miniaudio.h"
#include <vector>
#include <thread>
#include <string>

class CImGuiVideoPlayer
{
public:
	void Init();
	void Render(int screen_width, int screen_height);
	void Shutdown();

	unsigned char* GetImageBufferPtr();
	void SetActivationState(bool active);
	bool LoadVideo(std::string path);

	// TODO: should be private -- separate out to a context struct or something
	bool m_reading_frame;
	bool m_shutdown;
	std::chrono::high_resolution_clock::time_point m_lastTime;
	float m_lastTimeAudio;
	ma_engine m_ma_engine;
	bool m_paused;
	bool m_inactive;
	bool m_video_done;
	std::string m_path;
	bool m_first_load;
	int m_image_width;
	int m_image_height;
	int m_image_depth;

private:
	unsigned char* m_image_data;
	GLuint *m_image_texture;
	std::vector<float> m_audio_samples;
	std::thread *m_decoder_thread;
};