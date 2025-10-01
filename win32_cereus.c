#include <windows.h>
#include <stdbool.h>
#include "win32_renderer_bridge.h"
#include "win32_cereus_bridge.h"

#define local_persist static
#define global_variable static 
#define internal static

TickInput tick_input = {0};

LRESULT CALLBACK windowMessageProcessor(
    HWND window_handle, 
    UINT message_id,
    WPARAM wParam, 
    LPARAM lParam)
{
    switch (message_id)
    {
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;

        case WM_KEYDOWN:
            switch (wParam)
            {
                case 'W':
                    tick_input.w_press = true; 
                    break;
                case 'A':
                    tick_input.a_press = true; 
                    break;
                case 'S':
                    tick_input.s_press = true; 
                    break;
                case 'D':
                    tick_input.d_press = true; 
                    break;
                case 'I':
                    tick_input.i_press = true;
                    break;
                case 'J':
                    tick_input.j_press = true;
                	break;
                case 'K':
                    tick_input.k_press = true;
                	break;
                case 'L':
                    tick_input.l_press = true;
                	break;
                case 'Z':
                    tick_input.z_press = true;
                	break;
            }
            break;
        case WM_KEYUP:
            switch (wParam)
            {
            	case 'W':
                    tick_input.w_press = false; 
                    break;
            	case 'A':
                    tick_input.a_press = false; 
                    break;
                case 'S':
                    tick_input.s_press = false; 
                    break;
                case 'D':
                    tick_input.d_press = false; 
                    break;
                case 'I':
                    tick_input.i_press = false;
                    break;
                case 'J':
                    tick_input.j_press = false;
                	break;
                case 'K':
                    tick_input.k_press = false;
                	break;
                case 'L':
                    tick_input.l_press = false;
                	break;
                case 'Z':
                    tick_input.z_press = false;
                	break;
            }
    }
    return DefWindowProcW(window_handle, message_id, wParam, lParam);
}

int CALLBACK WinMain(
	HINSTANCE module_handle,
	HINSTANCE _,
	LPSTR     command_line,
	int       initial_show_state)
{
	(void)_;
    (void)command_line;

	WNDCLASSEXW window_class = {0};

	window_class.cbSize = sizeof(window_class);
	window_class.lpfnWndProc = windowMessageProcessor;
	window_class.hInstance = module_handle;
	window_class.hCursor = LoadCursor(0, IDC_ARROW); // NOTE(spike): remove when want custom cursor
	window_class.lpszClassName = L"standard_window_class";

    RegisterClassExW(&window_class);

	//int screen_width  = GetSystemMetrics(SM_CXSCREEN);
	//int screen_height = GetSystemMetrics(SM_CYSCREEN);
    int screen_width = 800;
    int screen_height = 450;

	HWND window_handle = CreateWindowExW(
		WS_EX_TOPMOST,
		L"standard_window_class",
		L"Window Name",
		WS_OVERLAPPEDWINDOW,
        0, 0,
		screen_width, screen_height,
		0,
		0,
		module_handle, 
		0);

    ShowWindow(window_handle, initial_show_state);

    RendererPlatformHandles platform_handles = { .module_handle = module_handle, .window_handle = window_handle};
    rendererInitialise(platform_handles);

    LARGE_INTEGER ticks_per_second;
    LARGE_INTEGER last_tick;
    QueryPerformanceFrequency(&ticks_per_second);
    QueryPerformanceCounter(&last_tick);
    double seconds_per_tick = 1.0 / ticks_per_second.QuadPart;

    MSG queued_message = {0};
    bool running = true;
	
    gameInitialise(); 

    while (running)
    {
		// OutputDebugStringA("hello running loop\n");

		while (PeekMessageW(&queued_message, 0, 0, 0, PM_REMOVE))
        {
            if (queued_message.message == WM_QUIT) 
			{
                running = false;
                break;
            }
            DispatchMessage(&queued_message);
        }

        LARGE_INTEGER current_tick;
        QueryPerformanceCounter(&current_tick);
        double delta_time = (current_tick.QuadPart - last_tick.QuadPart) * seconds_per_tick;

        last_tick = current_tick;

        gameFrame(delta_time, tick_input); 
    }

    return (int)queued_message.wParam;
}
