#include <windows.h>
#include <stdbool.h>
#include <stdio.h>
#include "win32_vulkan_bridge.h"
#include "win32_cereus_bridge.h"

// temp for sleep
typedef MMRESULT (WINAPI *timeBeginPeriod_t)(UINT);
typedef MMRESULT (WINAPI *timeEndPeriod_t)(UINT);

HWND global_window_handle = 0;
Input input = {0};
bool cursor_locked = false; // TODO: let the player have a cursor until game input, but give it back as soon as mouse is attempted to be moved (in game mode)
bool window_focused = false;
DisplayInfo display_info = {0};

void centerCursorInWindow()
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

void pushTextChar(Input *in, uint32 codepoint)
{
    if (in->text.count < (int32)(sizeof(in->text.codepoints) / sizeof(in->text.codepoints[0])))
    {
		in->text.codepoints[in->text.count++] = codepoint;
    }
}

LRESULT CALLBACK windowMessageProcessor(
    HWND window_handle, 
    UINT message_id,
    WPARAM wParam, 
    LPARAM lParam)
{
    switch (message_id)
    {
        case WM_CHAR:
        {
            uint32 character = (uint32)wParam;
            if (character == '\b' || character == '\r' || (character >= 32 && character < 128)) pushTextChar(&input, character);
            return 0;
        }
        case WM_ACTIVATE:
        {
            if (LOWORD(wParam) == WA_INACTIVE)
            {
                window_focused = false;
                cursor_locked = false;
                while (ShowCursor(TRUE) < 0) { }
            }
            else // WM_ACTIVE or WA_CLICKACTIVE
            {
                // only re-lock if not clicking on title bar. when activating via alt-tab, the click isn't on a non-client area
                POINT point = {0};
                GetCursorPos(&point);
                POINT client_point = point;
                ScreenToClient(window_handle, &client_point);
                bool click_in_client = (client_point.x >= 0 && client_point.x < display_info.client_width && client_point.y >= 0 && client_point.y < display_info.client_height);

                // re-lock if alt-tab (WA_ACTIVE) or clicked inside client area
                if (LOWORD(wParam) == WA_ACTIVE || click_in_client)
                {
                    window_focused = true;
                    cursor_locked = true;
                    while (ShowCursor(FALSE) >= 0) { }
                    centerCursorInWindow();
                }
            }
            break;
        }
        case WM_LBUTTONDOWN:
        {
            if (!cursor_locked)
            {
                cursor_locked = true;
                while (ShowCursor(FALSE) >= 0) { }
                centerCursorInWindow();
            }
            break;
        }
        case WM_SIZE:
        {
            if (wParam != SIZE_MINIMIZED)
            {
                display_info.client_width = LOWORD(lParam);
                display_info.client_height = HIWORD(lParam);
                vulkanResize(LOWORD(lParam), HIWORD(lParam));
                gameRedraw(display_info);
            }
            return 0;
        }
        case WM_DESTROY:
        {
            PostQuitMessage(0);
            return 0;
        }
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
                    input.mouse_dx -= raw_input->data.mouse.lLastX;
                    input.mouse_dy -= raw_input->data.mouse.lLastY;
                }
            }
            return 0;
        }
    }
    return DefWindowProcW(window_handle, message_id, wParam, lParam);
}

uint64 pollKeys()
{
    if (!window_focused) return 0;

    uint64 held = 0;

    if (GetAsyncKeyState('0') & 0x8000) held |= KEY_0;
    if (GetAsyncKeyState('1') & 0x8000) held |= KEY_1;
    if (GetAsyncKeyState('2') & 0x8000) held |= KEY_2;
    if (GetAsyncKeyState('3') & 0x8000) held |= KEY_3;
    if (GetAsyncKeyState('4') & 0x8000) held |= KEY_4;
    if (GetAsyncKeyState('5') & 0x8000) held |= KEY_5;
    if (GetAsyncKeyState('6') & 0x8000) held |= KEY_6;
    if (GetAsyncKeyState('7') & 0x8000) held |= KEY_7;
    if (GetAsyncKeyState('8') & 0x8000) held |= KEY_8;
    if (GetAsyncKeyState('9') & 0x8000) held |= KEY_9;

    if (GetAsyncKeyState('A') & 0x8000) held |= KEY_A;
    if (GetAsyncKeyState('B') & 0x8000) held |= KEY_B;
    if (GetAsyncKeyState('C') & 0x8000) held |= KEY_C;
    if (GetAsyncKeyState('D') & 0x8000) held |= KEY_D;
    if (GetAsyncKeyState('E') & 0x8000) held |= KEY_E;
    if (GetAsyncKeyState('F') & 0x8000) held |= KEY_F;
    if (GetAsyncKeyState('G') & 0x8000) held |= KEY_G;
    if (GetAsyncKeyState('H') & 0x8000) held |= KEY_H;
    if (GetAsyncKeyState('I') & 0x8000) held |= KEY_I;
    if (GetAsyncKeyState('J') & 0x8000) held |= KEY_J;
    if (GetAsyncKeyState('K') & 0x8000) held |= KEY_K;
    if (GetAsyncKeyState('L') & 0x8000) held |= KEY_L;
    if (GetAsyncKeyState('M') & 0x8000) held |= KEY_M;
    if (GetAsyncKeyState('N') & 0x8000) held |= KEY_N;
    if (GetAsyncKeyState('O') & 0x8000) held |= KEY_O;
    if (GetAsyncKeyState('P') & 0x8000) held |= KEY_P;
    if (GetAsyncKeyState('Q') & 0x8000) held |= KEY_Q;
    if (GetAsyncKeyState('R') & 0x8000) held |= KEY_R;
    if (GetAsyncKeyState('S') & 0x8000) held |= KEY_S;
    if (GetAsyncKeyState('T') & 0x8000) held |= KEY_T;
    if (GetAsyncKeyState('U') & 0x8000) held |= KEY_U;
    if (GetAsyncKeyState('V') & 0x8000) held |= KEY_V;
    if (GetAsyncKeyState('W') & 0x8000) held |= KEY_W;
    if (GetAsyncKeyState('X') & 0x8000) held |= KEY_X;
    if (GetAsyncKeyState('Y') & 0x8000) held |= KEY_Y;
    if (GetAsyncKeyState('Z') & 0x8000) held |= KEY_Z;

    // arrow keys aliased to wasd
    if (GetAsyncKeyState(VK_UP)    & 0x8000) held |= KEY_W;
    if (GetAsyncKeyState(VK_DOWN)  & 0x8000) held |= KEY_S;
    if (GetAsyncKeyState(VK_LEFT)  & 0x8000) held |= KEY_A;
    if (GetAsyncKeyState(VK_RIGHT) & 0x8000) held |= KEY_D;

    if (GetAsyncKeyState(VK_OEM_PERIOD) & 0x8000) held |= KEY_DOT;
    if (GetAsyncKeyState(VK_OEM_COMMA)  & 0x8000) held |= KEY_COMMA;
    if (GetAsyncKeyState(VK_SPACE)      & 0x8000) held |= KEY_SPACE;
    if (GetAsyncKeyState(VK_SHIFT)      & 0x8000) held |= KEY_SHIFT;
    if (GetAsyncKeyState(VK_TAB)        & 0x8000) held |= KEY_TAB;
    if (GetAsyncKeyState(VK_ESCAPE)     & 0x8000) held |= KEY_ESCAPE;
    if (GetAsyncKeyState(VK_BACK)       & 0x8000) held |= KEY_BACKSPACE;
    if (GetAsyncKeyState(VK_RETURN)     & 0x8000) held |= KEY_ENTER;

    if (GetAsyncKeyState(VK_LBUTTON) & 0x8000) held |= KEY_LEFT_MOUSE;
    if (GetAsyncKeyState(VK_RBUTTON) & 0x8000) held |= KEY_RIGHT_MOUSE;
    if (GetAsyncKeyState(VK_MBUTTON) & 0x8000) held |= KEY_MIDDLE_MOUSE;

    return held;
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
	
    HMONITOR monitor = MonitorFromWindow(0, MONITOR_DEFAULTTOPRIMARY); // passing null means getting primary monitor
    MONITORINFOEXW monitor_info = { .cbSize = sizeof(monitor_info) };
    GetMonitorInfoW(monitor, (MONITORINFO*)&monitor_info);

    DEVMODEW dev_mode = { .dmSize = sizeof(dev_mode) };
    EnumDisplaySettingsW(monitor_info.szDevice, ENUM_CURRENT_SETTINGS, &dev_mode);

    display_info.display_width = (int32)dev_mode.dmPelsWidth;
    display_info.display_height = (int32)dev_mode.dmPelsHeight;
    display_info.refresh_rate = (int32)dev_mode.dmDisplayFrequency;

    RECT work_area; // TODO: think about putting work area as display_width/height in display_info
	SystemParametersInfoW(SPI_GETWORKAREA, 0, &work_area, 0);

	HWND window_handle = CreateWindowExW(
		0,
		L"standard_window_class",
		L"Window Name",
		WS_OVERLAPPEDWINDOW,
        work_area.left, work_area.top,
		//display_info.display_width, display_info.display_height,
        //1920, 1080, // temp overwrite dims for easier debugging
        work_area.right - work_area.left, work_area.bottom - work_area.top,
		0, 0, module_handle, 0);

    global_window_handle = window_handle;

    ShowWindow(window_handle, initial_show_state);
    //window_focused = true; // uncomment if required, probably isn't

    // lock cursor on startup
    cursor_locked = true;
    while (ShowCursor(FALSE) >= 0) { }

	RAWINPUTDEVICE raw_input_device = {0};
    raw_input_device.usUsagePage = 0x01; // generic desktop controls
    raw_input_device.usUsage 	 = 0x02; // mouse
    raw_input_device.dwFlags     = 0;
    raw_input_device.hwndTarget  = window_handle;
    RegisterRawInputDevices(&raw_input_device, 1, sizeof(raw_input_device));

    RendererPlatformHandles platform_handles = { .module_handle = module_handle, .window_handle = window_handle };

    RECT client_rect = {0};
	GetClientRect(window_handle, &client_rect);
    display_info.client_width = client_rect.right - client_rect.left;
    display_info.client_height = client_rect.bottom - client_rect.top;

    vulkanInitialize(platform_handles, display_info);

    LARGE_INTEGER ticks_per_second;
    LARGE_INTEGER last_tick;
    QueryPerformanceFrequency(&ticks_per_second);
    QueryPerformanceCounter(&last_tick);
    double seconds_per_tick = 1.0 / ticks_per_second.QuadPart;

    MSG queued_message = {0};
    bool running = true;
	
    char* file_path = 0;
    if (command_line[0] != '\0') file_path = command_line;

    gameInitialize(file_path, display_info); 

	double frame_times[60] = {0};
	int32 frame_time_index = 0;
    int32 title_update_counter = 0;

	LARGE_INTEGER work_start, work_end;

    // sleep code
    {
        HMODULE random_ass_library = LoadLibraryA("winmm.dll");
        if (random_ass_library)
        {
            timeBeginPeriod_t begin_time_period = (timeBeginPeriod_t)GetProcAddress(random_ass_library, "timeBeginPeriod");
            if (begin_time_period) begin_time_period(1);
        }
    }

    while (running)
    {
		QueryPerformanceCounter(&work_start);

		while (PeekMessageW(&queued_message, 0, 0, 0, PM_REMOVE))
        {
            if (queued_message.message == WM_QUIT) 
			{
                running = false;
                break;
            }
            TranslateMessage(&queued_message);
            DispatchMessage(&queued_message);
        }

        LARGE_INTEGER current_tick;
        QueryPerformanceCounter(&current_tick);
        double delta_time = (current_tick.QuadPart - last_tick.QuadPart) * seconds_per_tick;
        last_tick = current_tick;

        input.keys_held = pollKeys();

        gameFrame(delta_time, &input); 

        input.mouse_dx = 0;
        input.mouse_dy = 0;
        input.text.count = 0;

        centerCursorInWindow();

        QueryPerformanceCounter(&work_end);

        // sleep code
        /*
        {
            double work_ms = (work_end.QuadPart - work_start.QuadPart) * seconds_per_tick * 1000.0;
            double target_ms = 1000.0 / 120.0;
            double sleep_ms = target_ms - work_ms;
            if (sleep_ms > 1.5)
            {
                Sleep((DWORD)(sleep_ms - 1.5));
            }
        }
        */

        LARGE_INTEGER frame_end;
        QueryPerformanceCounter(&frame_end);
        double total_ms = (frame_end.QuadPart - work_start.QuadPart) * seconds_per_tick * 1000.0;
        frame_times[frame_time_index] = total_ms;
        frame_time_index = (frame_time_index + 1) % 60;
        
        title_update_counter++;
        if (title_update_counter >= 300)
        {
            double avg_ms = 0.0;
            for (int i = 0; i < 60; i++) avg_ms += frame_times[i];
            avg_ms /= 60.0;

            double fps = 1000.0 / avg_ms;

            wchar_t title_buffer[256];
            //swprintf(title_buffer, 256, L"mspt: %.2f", work_ms);
            swprintf(title_buffer, 256, L"fps: %.0f", fps);
            SetWindowTextW(window_handle, title_buffer);
            title_update_counter = 0;
        }
    }

    return (int)queued_message.wParam;
}
