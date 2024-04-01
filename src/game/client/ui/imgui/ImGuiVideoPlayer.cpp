#include "ImGuiVideoPlayer.h"

#include "cl_dll.h"
#include "imgui.h"
#include "SDL.h"
#include "SDL_audio.h"
#include "SDL_opengl.h"

#define PL_MPEG_IMPLEMENTATION
#include "pl_mpeg.h"

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

void audio_decode_callback(plm_t* plm, plm_samples_t* samples, void* user)
{
	CImGuiVideoPlayer* player = (CImGuiVideoPlayer*) user;

	int size = sizeof(float) * samples->count * 2;

	// memcpy samples into buffer
	unsigned int remaining_buffer_length = player->m_audio_buffer_size-player->m_audio_write_index;
	memcpy(player->m_audio_samples + player->m_audio_write_index, samples->interleaved, remaining_buffer_length > size ? size : remaining_buffer_length);
	player->m_audio_write_index += remaining_buffer_length > size ? size : remaining_buffer_length;

	/*for (size_t i = 0; i < (remaining_buffer_length > frame->count ? frame->count : remaining_buffer_length); i++)
	{
		player->m_audio_samples[i + player->m_audio_write_index] = frame->interleaved[i];
	}
	player->m_audio_write_index += remaining_buffer_length > frame->count ? frame->count : remaining_buffer_length;*/

	// roll over into start of buffer if necessary
	int remainder = size - remaining_buffer_length;
	if (remainder > 0)
	{
		memcpy(player->m_audio_samples, samples->interleaved + (remaining_buffer_length/sizeof(float)/2), remainder);
		player->m_audio_write_index = remainder;
		
		/*for (size_t i = 0; i < remainder; i++)
		{
			player->m_audio_samples[i] = samples->interleaved[i + remaining_buffer_length];
		}
		player->m_audio_write_index = remainder;*/
	}

	/*for (size_t i = 0; i < frame->count; i++)
	{
		player->m_audio_samples[i + player->m_audio_write_index] = frame->interleaved[i];
	}
	player->m_audio_write_index += frame->count;*/
}

void decode_mpeg(CImGuiVideoPlayer* player)
{
	plm_t* plm;
	plm = plm_create_with_filename("jeep/media/bjork-all-is-full-of-love.mpeg");
	plm_set_video_decode_callback(plm, video_decode_callback, player);
	plm_set_audio_decode_callback(plm, audio_decode_callback, player);

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
		plm_decode(plm, 1);
		player->m_lastTime = currentTime;
		std::this_thread::sleep_for(std::chrono::milliseconds(5));
	}
	plm_destroy(plm);
}

void fill_audio(void* udata, Uint8* stream, int len)
{
	// In playback mode copy data to pOutput. In capture mode read data from pInput. In full-duplex mode, both
	// pOutput and pInput will be valid and you can move data from pInput into pOutput. Never process more than
	// frameCount frames.
	CImGuiVideoPlayer* player = (CImGuiVideoPlayer*) udata;

	// Check if write index is ahead of read index
	if (player->m_audio_write_index > player->m_audio_read_index)
	{
		// Queue the samples between read index and write index
		unsigned int num_samples = player->m_audio_write_index - player->m_audio_read_index;
		SDL_MixAudio(stream, (Uint8*)(player->m_audio_samples+player->m_audio_read_index), len > num_samples ? num_samples : len, SDL_MIX_MAXVOLUME);
		player->m_audio_read_index += len > num_samples ? num_samples : len;
	}
	// Otherwise, write index has rolled over
	else
	{
		// Queue the samples between read index and end of buffer
		unsigned int num_samples = player->m_audio_buffer_size - player->m_audio_read_index - 1;
		SDL_MixAudio(stream, player->m_audio_samples + player->m_audio_read_index, len > num_samples ? num_samples : len, SDL_MIX_MAXVOLUME);

		// Queue the samples between start of buffer and write index
		int new_length = len - len > num_samples ? num_samples : len;
		if (new_length > 0)
		{
			num_samples = player->m_audio_write_index;
			SDL_MixAudio(stream, player->m_audio_samples, new_length > num_samples ? num_samples : new_length, SDL_MIX_MAXVOLUME);
			player->m_audio_read_index = new_length > num_samples ? num_samples : new_length;
		}
	}
}

void CImGuiVideoPlayer::Init()
{
	SDL_AudioSpec wanted;

	/* Set the audio format */
	wanted.freq = 44100;
	wanted.format = AUDIO_F32;
	wanted.channels = 2;
	wanted.samples = 4096;
	wanted.callback = fill_audio;
	wanted.userdata = this;

	/* Open the audio device, forcing the desired format */
	m_audio_device = SDL_OpenAudioDevice(NULL, 0, &wanted, NULL, 0);
	if (m_audio_device == 0)
	{
		SDL_Log("Failed to open audio device: %s", SDL_GetError());
	}
	SDL_PauseAudioDevice(m_audio_device, 0);

	m_audio_buffer_size = 44100 * 10 * sizeof(float) * 2 + 1; // Ten seconds of audio
	m_audio_samples = (Uint8*)malloc(m_audio_buffer_size);

	// Create a OpenGL texture identifier
	m_image_texture = (GLuint*) malloc(sizeof(GLuint));
	glGenTextures(1, m_image_texture);
	glBindTexture(GL_TEXTURE_2D, *m_image_texture);

	m_image_width = 960;
	m_image_height = 540;
	m_image_depth = 4;
	m_image_data = (unsigned char*) malloc(m_image_width * m_image_height * m_image_depth);

	m_audio_read_index = 0;
	m_audio_write_index = 0;
	m_reading_frame = false;
	m_shutdown = false;
	m_lastTime = 0;
	m_decoder_thread = new std::thread(decode_mpeg, this);
}

void CImGuiVideoPlayer::Render()
{
	m_reading_frame = true;
	
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
	SDL_CloseAudio();
}