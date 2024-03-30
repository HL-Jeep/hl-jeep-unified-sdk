#pragma once

#include "pl_mpeg.h"
#include "stdint.h"
#include "SDL_opengl.h"

class CImGuiVideoPlayer
{
public:
	void Init();
	void Render();
	void Shutdown();
private:
	plm_t* m_plm;
	float m_lastTime;
	unsigned char* m_image_data;
	GLuint *m_image_texture;
	int m_image_width;
	int m_image_height;
	int m_image_depth;
};