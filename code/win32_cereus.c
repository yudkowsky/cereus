#include <windows.h>
#include <stdbool.h>
#include "win32_renderer_bridge.h"
#include "win32_cereus_bridge.h"

#define local_persist static
#define global_variable static 
#define internal static

int screen_width = 1920;
int screen_height = 1080;
//int screen_width = 800;
//int screen_height = 450;

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

        case WM_INPUT:
        {	
			UINT size_of_input = 0;
            GetRawInputData((HRAWINPUT)lParam, RID_INPUT, 0, &size_of_input, sizeof(RAWINPUTHEADER));

            BYTE buffer[sizeof(RAWINPUT)];
            if (size_of_input <= sizeof(buffer) && GetRawInputData((HRAWINPUT)lParam, RID_INPUT, buffer, &size_of_input, sizeof(RAWINPUTHEADER)) == size_of_input)
            {
                RAWINPUT* raw_input = (RAWINPUT*)buffer;
                if (raw_input->header.dwType == RIM_TYPEMOUSE)
                {
                    tick_input.mouse_dx -= raw_input->data.mouse.lLastX;
                    tick_input.mouse_dy -= raw_input->data.mouse.lLastY;
                }
            }
            break;
        }
        case WM_LBUTTONDOWN:
            tick_input.left_mouse_press = true;
            break;
        case WM_LBUTTONUP:
            tick_input.left_mouse_press = false;
            break;
        case WM_RBUTTONDOWN:
            tick_input.right_mouse_press = true;
            break;
        case WM_RBUTTONUP:
            tick_input.right_mouse_press = false;
            break;
        case WM_MBUTTONDOWN:
            tick_input.middle_mouse_press = true;
            break;
        case WM_MBUTTONUP:
            tick_input.middle_mouse_press = false;
            break;

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
                case 'Z':
                    tick_input.z_press = true;
                	break;
                case 'R':
                    tick_input.r_press = true;
                	break;
                case 'E':
                    tick_input.e_press = true;
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
                case VK_SPACE:
                    tick_input.space_press = true;
                    break;
                case VK_SHIFT:
                    tick_input.shift_press = true;
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
                case 'Z':
                    tick_input.z_press = false;
                	break;
                case 'R':
                    tick_input.r_press = false;
                	break;
                case 'E':
                    tick_input.e_press = false;
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
                case VK_SPACE:
                    tick_input.space_press = false;
                    break;
                case VK_SHIFT:
                    tick_input.shift_press = false;
                    break;
            }
            break;
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

	HWND window_handle = CreateWindowExW(
		0,
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

    while (ShowCursor(FALSE) >= 0) 
    {
        // fuction does the decrementing
    }

	RAWINPUTDEVICE raw_input_device = {0};
    raw_input_device.usUsagePage = 0x01; // generic desktop controls
    raw_input_device.usUsage 	 = 0x02; // mouse
    raw_input_device.dwFlags     = RIDEV_NOLEGACY;
    raw_input_device.hwndTarget  = window_handle;
    RegisterRawInputDevices(&raw_input_device, 1, sizeof(raw_input_device));

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

        tick_input.mouse_dx = 0;
        tick_input.mouse_dy = 0;
    }

    return (int)queued_message.wParam;
}
