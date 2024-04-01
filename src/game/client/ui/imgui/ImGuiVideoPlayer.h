#pragma once

#include "pl_mpeg.h"
#include "stdint.h"
#include "SDL.h"
#include "SDL_opengl.h"
#include <vector>
#include <thread>

class CImGuiVideoPlayer
{
public:
	void Init();
	void Render();
	void Shutdown();

	unsigned char* GetImageBufferPtr();
	void PushAudioSample(float sample);
	float* AudioSampleData();
	size_t NumAudioSamples();
	void ClearAudioSamples();

	bool m_reading_frame;
	bool m_shutdown;
	float m_lastTime;

private:
	unsigned char* m_image_data;
	GLuint *m_image_texture;
	int m_image_width;
	int m_image_height;
	int m_image_depth;
	std::vector<float> m_audio_samples;
	std::thread *m_decoder_thread;
};