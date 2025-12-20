#include <windows.h>
#include <stdbool.h>
#include <stdio.h>
#include "win32_renderer_bridge.h"
#include "win32_cereus_bridge.h"

#define local_persist static
#define global_variable static 
#define internal static

int screen_width = 1920;
int screen_height = 1080;
//int screen_width = 800;
//int screen_height = 450;

HWND global_window_handle = 0;
TickInput tick_input = {0};
bool cursor_locked = false;

void centerCursorInWindow(void)
{
    if (!global_window_handle || !cursor_locked) return;
    RECT client_rect;
    if (!GetClientRect(global_window_handle, &client_rect)) return;

    POINT center;
    center.x = (client_rect.right - client_rect.left) / 2;
    center.y = (client_rect.bottom - client_rect.top) / 2;
    ClientToScreen(global_window_handle, &center);
    SetCursorPos(center.x, center.y);
}

LRESULT CALLBACK windowMessageProcessor(
    HWND window_handle, 
    UINT message_id,
    WPARAM wParam, 
    LPARAM lParam)
{
    switch (message_id)
    {
        case WM_ACTIVATE:
        {
			if (LOWORD(wParam) == WA_ACTIVE || LOWORD(wParam) == WA_CLICKACTIVE)
            {
                cursor_locked = true;
                while (ShowCursor(FALSE) >= 0) { }
                centerCursorInWindow();
            }
			else
            {
                cursor_locked = false;
                while (ShowCursor(TRUE) < 0) { }
            }
            break;
        }
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

                    USHORT buttons = raw_input->data.mouse.usButtonFlags;
                    if (buttons & RI_MOUSE_LEFT_BUTTON_DOWN)   tick_input.left_mouse_press   = true;
                    if (buttons & RI_MOUSE_LEFT_BUTTON_UP)     tick_input.left_mouse_press   = false;
                    if (buttons & RI_MOUSE_RIGHT_BUTTON_DOWN)  tick_input.right_mouse_press  = true;
                    if (buttons & RI_MOUSE_RIGHT_BUTTON_UP)    tick_input.right_mouse_press  = false;
                    if (buttons & RI_MOUSE_MIDDLE_BUTTON_DOWN) tick_input.middle_mouse_press = true;
                    if (buttons & RI_MOUSE_MIDDLE_BUTTON_UP)   tick_input.middle_mouse_press = false;
                }
            }
            return 0;
        }

        case WM_KEYDOWN:
            switch (wParam)
            {
                case 'A': tick_input.a_press = true; break;
                case 'B': tick_input.b_press = true; break;
                case 'C': tick_input.c_press = true; break;
                case 'D': tick_input.d_press = true; break;
                case 'E': tick_input.e_press = true; break;
                case 'F': tick_input.f_press = true; break;
                case 'G': tick_input.g_press = true; break;
                case 'H': tick_input.h_press = true; break;
                case 'I': tick_input.i_press = true; break;
                case 'J': tick_input.j_press = true; break;
                case 'K': tick_input.k_press = true; break;
                case 'L': tick_input.l_press = true; break;
                case 'O': tick_input.o_press = true; break;
                case 'P': tick_input.p_press = true; break;
                case 'Q': tick_input.q_press = true; break;
                case 'R': tick_input.r_press = true; break;
                case 'S': tick_input.s_press = true; break;
                case 'T': tick_input.t_press = true; break;
                case 'U': tick_input.u_press = true; break;
                case 'V': tick_input.v_press = true; break;
                case 'W': tick_input.w_press = true; break;
                case 'X': tick_input.x_press = true; break;
                case 'Y': tick_input.y_press = true; break;
                case 'Z': tick_input.z_press = true; break;

                case '0': tick_input.zero_press  = true; break;
                case '1': tick_input.one_press   = true; break;
                case '2': tick_input.two_press   = true; break;
                case '3': tick_input.three_press = true; break;
                case '4': tick_input.four_press  = true; break;
                case '5': tick_input.five_press  = true; break;
                case '6': tick_input.six_press   = true; break;
                case '7': tick_input.seven_press = true; break;
                case '8': tick_input.eight_press = true; break;
                case '9': tick_input.nine_press  = true; break;

                case VK_SPACE: 	   tick_input.space_press = true; break;
                case VK_SHIFT: 	   tick_input.shift_press = true; break;
                case VK_BACK: 	   tick_input.back_press  = true; break;
                case VK_OEM_MINUS: tick_input.dash_press  = true; break;
            }
            break;
        case WM_KEYUP:
            switch (wParam)
            {
                case 'A': tick_input.a_press = false; break;
                case 'B': tick_input.b_press = false; break;
                case 'C': tick_input.c_press = false; break;
                case 'D': tick_input.d_press = false; break;
                case 'E': tick_input.e_press = false; break;
                case 'F': tick_input.f_press = false; break;
                case 'G': tick_input.g_press = false; break;
                case 'H': tick_input.h_press = false; break;
                case 'I': tick_input.i_press = false; break;
                case 'J': tick_input.j_press = false; break;
                case 'K': tick_input.k_press = false; break;
                case 'L': tick_input.l_press = false; break;
                case 'O': tick_input.o_press = false; break;
                case 'P': tick_input.p_press = false; break;
                case 'Q': tick_input.q_press = false; break;
                case 'R': tick_input.r_press = false; break;
                case 'S': tick_input.s_press = false; break;
                case 'T': tick_input.t_press = false; break;
                case 'U': tick_input.u_press = false; break;
                case 'V': tick_input.v_press = false; break;
                case 'W': tick_input.w_press = false; break;
                case 'X': tick_input.x_press = false; break;
                case 'Y': tick_input.y_press = false; break;
                case 'Z': tick_input.z_press = false; break;

                case '0': tick_input.zero_press  = false; break;
                case '1': tick_input.one_press   = false; break;
                case '2': tick_input.two_press   = false; break;
                case '3': tick_input.three_press = false; break;
                case '4': tick_input.four_press  = false; break;
                case '5': tick_input.five_press  = false; break;
                case '6': tick_input.six_press   = false; break;
                case '7': tick_input.seven_press = false; break;
                case '8': tick_input.eight_press = false; break;
                case '9': tick_input.nine_press  = false; break;

                case VK_SPACE: 	   tick_input.space_press = false; break;
                case VK_SHIFT: 	   tick_input.shift_press = false; break;
                case VK_BACK: 	   tick_input.back_press  = false; break;
                case VK_OEM_MINUS: tick_input.dash_press  = false; break;
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

    global_window_handle = window_handle;

    ShowWindow(window_handle, initial_show_state);

	RAWINPUTDEVICE raw_input_device = {0};
    raw_input_device.usUsagePage = 0x01; // generic desktop controls
    raw_input_device.usUsage 	 = 0x02; // mouse
    raw_input_device.dwFlags     = 0;
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
	
    char* file_path = 0;
    if (command_line[0] != '\0')
    {
        file_path = command_line;
    }

    gameInitialise(file_path); 

    int32 fps_timer = 0;
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

        double fps = 1.0 / delta_time;
        wchar_t title_buffer[256];
        if (fps_timer == 0)
        {
        	swprintf(title_buffer, 256, L"FPS: %.1f", fps);
            fps_timer = 20;
        }
        else (fps_timer--);
		SetWindowTextW(window_handle, title_buffer);

        gameFrame(delta_time, tick_input); 

        tick_input.mouse_dx = 0;
        tick_input.mouse_dy = 0;

        centerCursorInWindow();
    }

    return (int)queued_message.wParam;
}
