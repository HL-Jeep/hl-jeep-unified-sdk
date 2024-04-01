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
	Uint8* m_audio_samples;
	int m_audio_buffer_size;
	int m_audio_write_index;
	int m_audio_read_index;
	SDL_AudioDeviceID m_audio_device;
private:
	unsigned char* m_image_data;
	GLuint *m_image_texture;
	int m_image_width;
	int m_image_height;
	int m_image_depth;
	std::thread *m_decoder_thread;
};