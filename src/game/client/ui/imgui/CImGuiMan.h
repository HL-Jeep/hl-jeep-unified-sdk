#pragma once

#include "imgui.h"
#include "backends/imgui_impl_opengl2.h"
#include "backends/imgui_impl_sdl2.h"
#include "SDL_opengl.h"

class CImGuiMan
{
public:
	void InitImgui();
	void ShutdownImgui();
	void RenderImGui();

private:
	SDL_Window* GetSdlWindow();
	GLuint m_cursor_texture;

public:
	SDL_Window* m_pWindow;
};

extern CImGuiMan g_ImGuiMan;