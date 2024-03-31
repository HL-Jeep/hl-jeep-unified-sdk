#pragma once

#include "pl_mpeg.h"
#include "stdint.h"
#include "SDL.h"
#include "SDL_opengl.h"
#include <vector>

class CImGuiVideoPlayer
{
public:
	void Init();
	void Render();
	void Shutdown();

	void PushAudioSample(float sample);
	float* AudioSampleData();
	size_t NumAudioSamples();
	void ClearAudioSamples();
private:
	plm_t* m_plm;
	float m_lastTime;
	unsigned char* m_image_data;
	GLuint *m_image_texture;
	int m_image_width;
	int m_image_height;
	int m_image_depth;
	std::vector<float> m_audio_samples;
};