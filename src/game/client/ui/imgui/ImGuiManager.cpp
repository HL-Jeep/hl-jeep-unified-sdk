#include "hud.h"
#include "PlatformHeaders.h"
#include "CImGuiMan.h"
#include "ImGuiVideoPlayer.h"
#include "SDL2/SDL.h"

#include "cl_util.h"
#include "Exports.h"

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

// Adapted from https://github.com/BlueNightHawk/hl_imgui_base

CImGuiMan g_ImGuiMan;
CImGuiVideoPlayer g_ImGuiVideoPlayer;
extern modfuncs_s* g_pModFuncs;

static void ImGuiRenderFunc()
{
	g_ImGuiMan.RenderImGui();
}

static int ImGuiEventFilter(void*, SDL_Event* event)
{
	return static_cast<int>(ImGui_ImplSDL2_ProcessEvent(event));
}

static void ImGuiLoadVideo(const CommandArgs& args)
{
	if (args.Count() < 2)
	{
		Con_Printf("Usage: %s <video name>\n", args.Argument(0));
		return;
	}

	const char* path = args.Argument(1);
	bool success = g_ImGuiVideoPlayer.LoadVideo(path);

	if (!success)
	{
		Con_Printf("Failed to load video: %s\n", path);
		return;
	}
}

// Simple helper function to load an image into a OpenGL texture with common settings
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
	if (out_width)
		*out_width = image_width;
	if (out_height)
		*out_height = image_height;

	return true;
}

void CImGuiMan::InitImgui()
{
	m_pWindow = GetSdlWindow();
	if (!m_pWindow)
	{
		gEngfuncs.Con_DPrintf("Failed to get SDL Window! ImGui is unavailable.\n");
		return;
	}

	// Setup Dear ImGui context
	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGuiIO& io = ImGui::GetIO();
	(void)io;
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard; // Enable Keyboard Controls
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;  // Enable Gamepad Controls
	io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;

	// Setup Dear ImGui style
	ImGui::StyleColorsDark();
	// ImGui::StyleColorsLight();

	// Set up console commands
	g_ConCommands.CreateCVar("np_imgui_demo", "0", FCVAR_CLIENTDLL, CommandLibraryPrefix::No);
	g_ConCommands.CreateCVar("np_video_player", "1", FCVAR_CLIENTDLL, CommandLibraryPrefix::No);
	g_ConCommands.CreateCVar("np_video_fullscreen", "1", FCVAR_CLIENTDLL, CommandLibraryPrefix::No);
	g_ConCommands.CreateCVar("np_video_width", "0.75", FCVAR_CLIENTDLL, CommandLibraryPrefix::No);
	g_ConCommands.CreateCVar("np_mouse", "0", FCVAR_CLIENTDLL, CommandLibraryPrefix::No);
	g_ConCommands.CreateCommand("np_load_video", &ImGuiLoadVideo, CommandLibraryPrefix::No);

	// Setup Platform/Renderer backends
	ImGui_ImplSDL2_InitForOpenGL(m_pWindow, nullptr);
	ImGui_ImplOpenGL2_Init();

	SDL_AddEventWatch(ImGuiEventFilter, nullptr);

	// Set up other ImGui stuff
	g_ImGuiVideoPlayer.Init();

	LoadTextureFromFile("jeep/sprites/misc/cursor.png", &m_cursor_texture, NULL, NULL);

	g_pModFuncs->m_pfnFrameRender2 = ImGuiRenderFunc;
}

void CImGuiMan::ShutdownImgui()
{
	SDL_DelEventWatch(ImGuiEventFilter, nullptr);

	// Cleanup
	glDeleteTextures(1, &m_cursor_texture);
	g_ImGuiVideoPlayer.Shutdown();
	ImGui_ImplOpenGL2_Shutdown();
	ImGui_ImplSDL2_Shutdown();
	ImGui::DestroyContext();
}

bool need_restore_mouse;
void CImGuiMan::RenderImGui()
{
	if (!m_pWindow)
		return;

	int screen_width, screen_height;
	SDL_GetWindowSize(m_pWindow, &screen_width, &screen_height);
	// Start the Dear ImGui frame
	ImGui_ImplOpenGL2_NewFrame();
	ImGui_ImplSDL2_NewFrame();
	ImGui::NewFrame();

	if (g_ConCommands.GetCVar("np_imgui_demo") && g_ConCommands.GetCVar("np_imgui_demo")->value == 1)
		ImGui::ShowDemoWindow();

	if (g_ConCommands.GetCVar("np_video_player"))
	{
		g_ImGuiVideoPlayer.SetActivationState(g_ConCommands.GetCVar("np_video_player")->value == 1);
		if (g_ConCommands.GetCVar("np_video_player")->value == 1)
			g_ImGuiVideoPlayer.Render(screen_width, screen_height);
	}

	if (g_ConCommands.GetCVar("np_mouse") && g_ConCommands.GetCVar("np_mouse")->value == 1)
	{
		int mouse_x, mouse_y;
		SDL_GetMouseState(&mouse_x, &mouse_y);
		ImGui::GetForegroundDrawList()->AddImage((void*)(intptr_t) * (&m_cursor_texture), ImVec2(mouse_x, mouse_y), ImVec2(mouse_x + 16, mouse_y + 16));
		need_restore_mouse = IsMouseInPointerEnabled();
		IN_DeactivateMouse();
	}
	else if (need_restore_mouse)
	{
		IN_ActivateMouse();
		need_restore_mouse = false;
	}

	ImGui::Render();
	ImGui_ImplOpenGL2_RenderDrawData(ImGui::GetDrawData());

	//SDL_SetWindowGrab(m_pWindow, SDL_FALSE);
}

SDL_Window* CImGuiMan::GetSdlWindow()
{
	Uint32 windowID = 0;
	while (windowID < std::numeric_limits<Uint32>::max())
	{
		if (auto window = SDL_GetWindowFromID(windowID); window)
		{
			return window;
		}

		++windowID;
	}
	return nullptr;
}
