#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdbool.h>
#include <WinUser.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <tlhelp32.h>
#include <wchar.h>
#include <stdint.h>
#include <process.h>
#define COBJMACROS

#include <shlwapi.h>

#pragma comment(lib, "Shlwapi.lib")

#include <wtsapi32.h>

#pragma comment(lib, "Wtsapi32.lib")

#include <d3d11.h>
#include <dxgi.h>
#include <d3dcompiler.h>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3dcompiler.lib")
#pragma comment(lib, "dxguid.lib")

#define MAX_MICE 16
#define OCR_NORMAL      32512
#define OCR_IBEAM       32513
#define OCR_WAIT        32514
#define OCR_CROSS       32515
#define OCR_UP          32516
#define OCR_SIZE        32640
#define OCR_ICON        32641
#define OCR_SIZENWSE    32642
#define OCR_SIZENESW    32643
#define OCR_SIZEWE      32644
#define OCR_SIZENS      32645
#define OCR_SIZEALL     32646
#define OCR_NO          32648
#define OCR_HAND        32649
#define OCR_APPSTARTING 32650

typedef struct RGBAColor
{
    float r;
    float g;
    float b;
    float a;
} RGBAColor;

typedef struct MouseData
{
    LONG x;
    LONG y;
    LONG dx; //last Delta x
    LONG dy; //last Delta y
    bool leftDown;
    bool rightDown;
    bool middleDown;
    bool oldLeftDown;
    bool oldRightDown;
    bool oldMiddleDown;
    SHORT wheel;
    void* device;
    bool hasBlocked;
    bool isLinked;
    HWND targetWindow;
    RGBAColor color;

}MouseData;

typedef struct KeyboardData
{
    HANDLE device;

    MouseData* linkedMouse;

}KeyboardData;

/*
 * fixed Wire-Format:
 * no native Pointers, bools or implicite Padding-Bytes.
 */
#pragma pack(push, 1)
typedef struct MousePacket
{
    uint32_t version;       /* Offset  0, 4 Bytes */
    int32_t x, y, dx, dy;  /* Offset  4, 16 Bytes */

    uint8_t leftDown;
    uint8_t rightDown;
    uint8_t middleDown;
    uint8_t oldLeftDown;
    uint8_t oldRightDown;
    uint8_t oldMiddleDown; /* Offset 20, 6 Bytes */

    int16_t wheel;          /* Offset 26, 2 Bytes */

    uint64_t device;        /* Offset 28, 8 Bytes */

    uint8_t hasBlocked;     /* Offset 36 */
    uint8_t isLinked;       /* Offset 37 */

    uint64_t targetWindow;  /* Offset 38, 8 Bytes */

    float r, g, b, a;       /* Offset 46, 16 Bytes */
} MousePacket;
#pragma pack(pop)

#pragma pack(push, 1)
typedef struct MouseBatchHeader
{
    UINT32 version;     // version: 1
    UINT32 mouseCount;  // mouseCount
    UINT32 packetSize;  // Header + all MousePackets
} MouseBatchHeader;
#pragma pack(pop)

#define PIPE_NAME "\\\\.\\pipe\\MouseDataPipe"

static MouseData g_mice[MAX_MICE] = { 0 };
static UINT32 g_mouseCount = 0;
static CRITICAL_SECTION g_lock;
static volatile LONG g_running = 1;


static ID3D11Device* g_device = NULL;
static ID3D11DeviceContext* g_context = NULL;
static IDXGISwapChain* g_swapChain = NULL;
static ID3D11RenderTargetView* g_renderTarget = NULL;

static ID3D11VertexShader* g_vertexShader = NULL;
static ID3D11PixelShader* g_pixelShader = NULL;
static ID3D11InputLayout* g_inputLayout = NULL;
static ID3D11Buffer* g_vertexBuffer = NULL;
static ID3D11ShaderResourceView* g_cursorTexture = NULL;
static ID3D11SamplerState* g_sampler = NULL;

static ID3D11Buffer* g_constantBuffer = NULL;

ID3D11BlendState* g_blendState = NULL;

typedef struct
{
    float screenWidth;
    float screenHeight;
    float padding[2];
} ScreenConstants;



static const char* cursorVS =
"float2 screenSize;"
""
"struct VS_IN"
"{"
"    float2 pos : POSITION;"
"    float2 uv  : TEXCOORD0;"
"    float4 color : COLOR0;"
"};"
""
"struct VS_OUT"
"{"
"    float4 pos : SV_POSITION;"
"    float2 uv  : TEXCOORD0;"
"    float4 color : COLOR0;"
"};"
""
"VS_OUT main(VS_IN input)"
"{"
"    VS_OUT output;"
""
"    float x = input.pos.x / screenSize.x * 2.0f - 1.0f;"
"    float y = 1.0f - input.pos.y / screenSize.y * 2.0f;"
""
"    output.pos = float4(x, y, 0.0f, 1.0f);"
"    output.uv = input.uv;"
"    output.color = input.color;"
""
"    return output;"
"}";

static const char* cursorPS =
"Texture2D cursorTexture : register(t0);"
"SamplerState cursorSampler : register(s0);"
""
"struct PS_IN"
"{"
"    float4 pos : SV_POSITION;"
"    float2 uv : TEXCOORD0;"
"    float4 color : COLOR0;"
"};"
""
"float4 main(PS_IN input) : SV_TARGET"
"{"
"    float4 texColor = cursorTexture.Sample(cursorSampler, input.uv);"
"    return texColor * input.color;"
"}";

KeyboardData keyboards[MAX_MICE];
MouseData mice[MAX_MICE];
MouseData* hasControl;
MouseData* lastMouse;

int currentRegisteredMice = 0;
int currentRegisteredKeyboard = 0;

int RegisterRawInput(HWND hwnd);
LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
MouseData* GetMouse(UINT id);
KeyboardData* GetKeyboard(UINT id);
void HideCursor();
HRESULT InitD3D11(HWND hwnd);
HRESULT InitCursorRenderer(void);
void ClearScreen(void);
HRESULT CreateCursorTexture(void);
HRESULT CompileShader(
    const char* source,
    const char* entry,
    const char* target,
    ID3DBlob** blob);
void Render(void);
volatile LONG blockKeyboard = 0;
RGBAColor GetMouseColor(int index);
static void CopyToPacket(MousePacket* p, const MouseData* m);
static BOOL WriteAll(HANDLE pipe, const void* data, DWORD size);
static unsigned __stdcall SenderThread(void* arg);
void UpdateMouseData(const MouseData* mice, UINT32 count);
void InitMousePipeData();
void GenericMessage(LPCSTR text);
void GenericMessageErrCode(LPCSTR text, DWORD ErrorCode);


int screenWidth;
int screenHeight;
int screenX;
int screenY;

int cursors[] = {
    OCR_NORMAL,
    OCR_IBEAM,
    OCR_WAIT,
    OCR_CROSS,
    OCR_UP,
    OCR_SIZE,
    OCR_ICON,
    OCR_SIZENWSE,
    OCR_SIZENESW,
    OCR_SIZEWE,
    OCR_SIZENS,
    OCR_SIZEALL,
    OCR_NO,
    OCR_HAND,
    OCR_APPSTARTING
};

#define CURSOR_W 24
#define CURSOR_H 24

static const unsigned char cursorMask[CURSOR_H][CURSOR_W] =
{
    "X                       ",
    "XX                      ",
    "XXX                     ",
    "XXXX                    ",
    "XXXXX                   ",
    "XXXXXX                  ",
    "XXXXXXX                 ",
    "XXXXXXXX                ",
    "XXXXXXXXX               ",
    "XXXXXXXXXX              ",
    "XXXXXXXXXXX             ",
    "XXXXXXXXXXXX            ",
    "XXXXXXXXXXXXX           ",
    "XXXXXX                  ",
    "XXXXXX                  ",
    "XXXXX                   ",
    "XXXX                    ",
    "XXX                     ",
    "XX                      ",
    "X                       ",
    "                        ",
    "                        ",
    "                        ",
    "                        "
};

typedef struct
{
    float x;
    float y;
    float u;
    float v;
    RGBAColor color;

} CursorVertex;



typedef HWND(*PFN_CreateBandWindow)(
    int,
    int,
    int,
    int,
    DWORD
    );

typedef void (*PFN_DestroyBandWindow)(HWND);

static const RGBAColor colors[16] =
{
    { 1.0f, 0.7f, 0.3f, 1.0f }, // 13: Gold
    { 0.0f, 0.0f, 1.0f, 1.0f }, // 0: Blau
    { 0.0f, 1.0f, 0.0f, 1.0f }, // 1: Grün
    { 1.0f, 0.0f, 0.0f, 1.0f }, // 2: Rot
    { 1.0f, 1.0f, 0.0f, 1.0f }, // 3: Gelb
    { 1.0f, 0.0f, 1.0f, 1.0f }, // 4: Magenta
    { 0.0f, 1.0f, 1.0f, 1.0f }, // 5: Cyan
    { 1.0f, 0.5f, 0.0f, 1.0f }, // 6: Orange
    { 0.5f, 0.0f, 1.0f, 1.0f }, // 7: Violett
    { 1.0f, 0.4f, 0.7f, 1.0f }, // 8: Pink
    { 0.5f, 1.0f, 0.0f, 1.0f }, // 9: Limette
    { 0.0f, 0.5f, 0.5f, 1.0f }, // 10: Türkis
    { 0.6f, 0.3f, 0.1f, 1.0f }, // 11: Braun
    { 0.5f, 0.5f, 0.5f, 1.0f }, // 12: Grau
    { 0.4f, 0.2f, 0.6f, 1.0f }, // 14: Dunkelviolett
    { 1.0f, 1.0f, 1.0f, 1.0f }  // 15: Weiß
};

typedef HWND(WINAPI* PFN_CreateWindowInBand)(
    DWORD,
    LPCWSTR,
    LPCWSTR,
    DWORD,
    int,
    int,
    int,
    int,
    HWND,
    HMENU,
    HINSTANCE,
    LPVOID,
    DWORD
    );
HWND thisWindow = NULL;

int WINAPI WinMain(
    HINSTANCE hInstance, // Handle to the current instance of the application
    HINSTANCE hPrevInstance, // Always NULL in modern Windows
    LPSTR lpCmdLine, // Command-line arguments as a single string
    int nCmdShow // Flag to indicate how the window should be displayed
)
{
    // argument receiving from C# 
    //
    if (strcmp(lpCmdLine, "mySpecialCode") != 0)
    {
        //blocked if not from GUI
        return 1;
    }
    

    InitMousePipeData();
    uintptr_t thread = _beginthreadex(NULL, 0, SenderThread, NULL, 0, NULL);

    if (thread == 0)
    {
        DeleteCriticalSection(&g_lock);
        GenericMessage("Couldnt Init Thread");
        return 1;
    }
    InterlockedExchange(&g_running, 1);
    /* Wake up the Sender if he blocks right now*/
    HANDLE wake = CreateFileA(PIPE_NAME, GENERIC_READ, 0, NULL, OPEN_EXISTING, 0, NULL);
    
    if (wake != INVALID_HANDLE_VALUE)
    {
        CloseHandle(wake);
    }

    HMODULE user32 = GetModuleHandleW(L"user32.dll"); //Loading the user32.dll for the ProccessAdress we need later for the CreateWindowinBand method
    screenX = GetSystemMetrics(SM_XVIRTUALSCREEN);
    screenY = GetSystemMetrics(SM_YVIRTUALSCREEN);

    screenWidth = GetSystemMetrics(SM_CXVIRTUALSCREEN);
    screenHeight = GetSystemMetrics(SM_CYVIRTUALSCREEN);
    //DWORD band = 2; //Displays over the login Screen Requires special circumstances
    DWORD band = 16;


    if (!user32)
    {
        return NULL;
    }




    PFN_CreateWindowInBand CreateWindowInBand = (PFN_CreateWindowInBand)GetProcAddress(user32, "CreateWindowInBand"); //Getting the ProcessAdress

    if (!CreateWindowInBand)
    {
        GenericMessage("Couldnt get the right ProcessAdress needed for CreateWindowInBand");
        return 1;
    }

    //HINSTANCE hInst = GetModuleHandleW(NULL); //Older Code for ConsoleApplication

    // Own Window Class
    WNDCLASSW wc = { 0 };

    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = L"MyBandWindow";

    RegisterClassW(&wc);

    HWND hwnd = CreateWindowInBand(
        WS_EX_LAYERED |
        WS_EX_TOPMOST |
        WS_EX_NOACTIVATE |
        WS_EX_TRANSPARENT,
        L"MyBandWindow",
        L"",

        WS_POPUP,

        screenX,
        screenY,
        screenWidth,
        screenHeight,

        NULL,
        NULL,
        hInstance,
        NULL,

        band
    );

    thisWindow = hwnd;
    HRESULT hr = InitD3D11(hwnd);
    if (FAILED(hr))
    {
        GenericMessage("D3D11 couldnt initialize");
        return 1;
    }

    SetLayeredWindowAttributes(hwnd, RGB(255, 0, 255), 0, LWA_COLORKEY); //we need that to make the window Transparent
    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);

    if (hwnd == NULL)
    {
        GenericMessageErrCode("Error hwnd null: %lu", GetLastError());
    }

    if (!RegisterRawInput(hwnd))
    {
        
        GenericMessageErrCode("Error %lu", GetLastError());
        return -1;
    }

    
    MSG msg;
    while (1)
    {
        if (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE))
        {
            if (msg.message == WM_QUIT)
                break;

            TranslateMessage(&msg);
            DispatchMessage(&msg);
            
        }
        else
        {
            Render();
        }

    }

    //CleanUp
    WaitForSingleObject((HANDLE)thread, INFINITE);
    CloseHandle((HANDLE)thread);
    DeleteCriticalSection(&g_lock);

}


int RegisterRawInput(HWND hwnd)
{
    RAWINPUTDEVICE rid[2];

    // Mouse
    rid[0].usUsagePage = 0x01; // Generic Desktop
    rid[0].usUsage = 0x02; // Mouse
    rid[0].dwFlags = RIDEV_INPUTSINK | RIDEV_NOLEGACY;
    rid[0].hwndTarget = hwnd;

    // Keyboard
    rid[1].usUsagePage = 0x01; // Generic Desktop
    rid[1].usUsage = 0x06; // Keyboard
    rid[1].dwFlags = RIDEV_INPUTSINK | RIDEV_NOLEGACY;
    rid[1].hwndTarget = hwnd;

    return RegisterRawInputDevices(
        rid,
        2,
        sizeof(RAWINPUTDEVICE)
    );
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{

    switch (msg)
    {
    case WM_CREATE:
    {
        if (!WTSRegisterSessionNotification(hwnd, NOTIFY_FOR_THIS_SESSION)) {


            GenericMessageErrCode("WTSRegisterSessionNotification failed: %lu", GetLastError());
        }
        return 0;
    }

    case WM_INPUT:
    {

        UINT size = 0;

        GetRawInputData((HRAWINPUT)lParam, RID_INPUT, NULL, &size, sizeof(RAWINPUTHEADER));

        // Alloce Heap WinApi Style
        BYTE* buffer = (BYTE*)HeapAlloc(GetProcessHeap(), 0, size);
        if (!buffer)
        {
            return 0;
        }

        GetRawInputData((HRAWINPUT)lParam, RID_INPUT, buffer, &size, sizeof(RAWINPUTHEADER));

        RAWINPUT* raw = (RAWINPUT*)buffer;


        if (raw->header.dwType == RIM_TYPEMOUSE)
        {
            int dx = raw->data.mouse.lLastX;
            int dy = raw->data.mouse.lLastY;
            void* device = raw->header.hDevice;
            bool klick = raw->data.mouse.ulButtons;
            MouseData* found = GetMouse(raw->header.hDevice);
            bool willControl = false;
           
            if (found)
            {
                int dx = raw->data.mouse.lLastX;
                int dy = raw->data.mouse.lLastY;

                found->dx = dx;
                found->dy = dy;

                found->x += dx;
                found->y += dy;

                found->x = max(0, min(screenWidth, found->x));
                found->y = max(0, min(screenHeight, found->y));

                USHORT flags = raw->data.mouse.usButtonFlags;

                // -------------------------
                // BUTTON DOWN
                // -------------------------

                if (flags & RI_MOUSE_LEFT_BUTTON_DOWN)
                {
                    found->leftDown = true;

                    hasControl = found;
                    POINT p = { found->x, found->y };
                    HWND target = WindowFromPoint(p);
                    found->targetWindow = target;
                }

                if (flags & RI_MOUSE_RIGHT_BUTTON_DOWN)
                {
                    found->rightDown = true;


                }

                if (flags & RI_MOUSE_MIDDLE_BUTTON_DOWN)
                {
                    found->middleDown = true;



                }

                // -------------------------
                // BUTTON UP
                // -------------------------

                if (flags & RI_MOUSE_LEFT_BUTTON_UP)
                {
                    found->leftDown = false;
                    if (hasControl != NULL)
                    {
                        if (hasControl->device == found->device)
                        {
                            hasControl = NULL;
                        }
                    }

                }


                if (flags & RI_MOUSE_RIGHT_BUTTON_UP)
                {
                    found->rightDown = false;


                }


                if (flags & RI_MOUSE_MIDDLE_BUTTON_UP)
                {
                    found->middleDown = false;


                }



                // -------------------------
                // WHEEL
                // -------------------------

                if (flags & RI_MOUSE_WHEEL)
                {
                    SHORT delta = (SHORT)raw->data.mouse.usButtonData;

                    if (delta > 0)
                        found->wheel++;
                    else if (delta < 0)
                        found->wheel--;
                    willControl = true;

                }

                //Acess control
                if (willControl)
                {
                    willControl = false;

                }
                if (hasControl != NULL)
                {
                    SetCursorPos(hasControl->x, hasControl->y);
                }
                else
                {
                    SetCursorPos(found->x, found->y);
                }
                lastMouse = found;
                UpdateMouseData(mice, (UINT32)currentRegisteredMice);
            }
            else
            {
                // New Mouse
                if (currentRegisteredMice < MAX_MICE)
                {
                    MouseData* m = &mice[currentRegisteredMice];
                    POINT p;
                    GetCursorPos(&p);
                    m->device = device;
                    m->x = p.x;
                    m->y = p.y;
                    m->dx = dx;
                    m->dy = dy;
                    m->leftDown = false;
                    m->rightDown = false;
                    m->middleDown = false;
                    m->oldLeftDown = false;
                    m->oldRightDown = false;
                    m->oldMiddleDown = false;
                    m->wheel = 0;
                    m->hasBlocked = false;
                    m->isLinked = false;
                    m->targetWindow = NULL;
                    m->color = GetMouseColor(currentRegisteredMice);
                    currentRegisteredMice++;
                    UpdateMouseData(mice, (UINT32)currentRegisteredMice);
                    


                }
            }
        }
        //Method for Keyboard Data
        /*else if (raw->header.dwType == RIM_TYPEKEYBOARD)
        {
            //Log("Keyboard input received");

            InterlockedExchange(&blockKeyboard, 1);
            USHORT vkey = raw->data.keyboard.VKey;
            USHORT flags = raw->data.keyboard.Flags;
            void* device = raw->header.hDevice;
            bool keyUp = (flags & RI_KEY_BREAK) != 0;
            KeyboardData* found = GetKeyboard(raw->header.hDevice);
            if (found && found->linkedMouse)
            {
                HWND target = found->linkedMouse->targetWindow;

                if (target)
                {
                    USHORT vk = raw->data.keyboard.VKey;
                    USHORT flags = raw->data.keyboard.Flags;

                    bool keyUp = (flags & RI_KEY_BREAK) != 0;
                    if (keyUp)
                    {
                        // Key UP
                        PostMessage(target, WM_KEYUP, vk, 0);
                    }
                    else
                    {
                        // Key Down
                        PostMessage(target, WM_KEYDOWN, vk, 0);
                    }
                    // ...
                }
                InterlockedExchange(&blockKeyboard, 0);

            }
            else
            {
                // Register new Keyboard
                if (currentRegisteredMice < MAX_MICE)
                {
                    for (int i = 0; i < currentRegisteredMice; i++)
                    {
                        if (mice[i].isLinked == false)
                        {
                            KeyboardData* k = &keyboards[currentRegisteredKeyboard];
                            k->device = device;
                            k->linkedMouse = &mice[i];
                            mice[i].isLinked = true;
                            Log("Registered keyboard: %d", currentRegisteredKeyboard);
                            currentRegisteredKeyboard++;
                            break;
                        }
                    }

                }
            }
        }

        //InvalidateRect(hwnd, NULL, TRUE);
        HeapFree(GetProcessHeap(), 0, buffer);
        //DrawOverlay(hwnd);
        return 0;*/
    }
    case WM_WTSSESSION_CHANGE:
        switch (wParam)
        {
            case WTS_SESSION_LOCK:
                SystemParametersInfo(SPI_SETCURSORS, 0, NULL, 0);
            break;

        case WTS_SESSION_UNLOCK:
            HideCursor();
            break;
        }
        return 0;
    case WM_DESTROY:
        WTSUnRegisterSessionNotification(hwnd);
        PostQuitMessage(0);
        return 0;
    }

    return DefWindowProc(hwnd, msg, wParam, lParam);
}

void HideCursor()
{

    for (int i = 0; i < sizeof(cursors) / sizeof(cursors[0]); i++)
    {
        BYTE andMask[128];
        BYTE xorMask[128];

        memset(andMask, 0xFF, sizeof(andMask)); // Hintergrund unverändert lassen
        memset(xorMask, 0x00, sizeof(xorMask)); // nichts zeichnen

        HCURSOR invisible = CreateCursor(
            NULL,
            0,
            0,
            32,
            32,
            andMask,
            xorMask
        );

        if (invisible)
        {
            SetSystemCursor(invisible, cursors[i]);
        }
    }
}

MouseData* GetMouse(void* id)
{
    for (int i = 0; i < currentRegisteredMice; i++)
    {
        if (mice[i].device == id)
        {
            return &mice[i];
        }
    }
    return NULL;
}

KeyboardData* GetKeyboard(void* id)
{
    for (int i = 0; i < currentRegisteredKeyboard; i++)
    {
        if (keyboards[i].device == id)
        {
            return &keyboards[i];
        }
    }
    return NULL;
}

RGBAColor GetMouseColor(int index)
{
    if (index < 0 || index >= 16)
    {
        return colors[0]; // Standardfarbe: Blau
    }

    return colors[index];
}

//------------------------------------------------------------------------
//Troubleshooting
//------------------------------------------------------------------------
void GenericMessage(LPCSTR text)
{
    MessageBoxA(NULL, text, "Error", MB_OK);
}

void GenericMessageErrCode(LPCSTR text, DWORD ErrorCode)
{

    char buffer[256]; // Buffer for formatted message

    // Safely format the error message
    if (snprintf(buffer, sizeof(buffer), text, (unsigned long)ErrorCode) < 0) {
        
        GenericMessage("Failed to format error message.");
        return;
    }
    MessageBoxA(NULL, buffer, "Error", MB_OK);
}


//------------------------------------------------------------------------

static void CopyToPacket(MousePacket* p, const MouseData* m)
{
    memset(p, 0, sizeof(*p));

    p->version = 1;

    p->x = (int32_t)m->x;
    p->y = (int32_t)m->y;
    p->dx = (int32_t)m->dx;
    p->dy = (int32_t)m->dy;

    p->leftDown = m->leftDown ? 1 : 0;
    p->rightDown = m->rightDown ? 1 : 0;
    p->middleDown = m->middleDown ? 1 : 0;
    p->oldLeftDown = m->oldLeftDown ? 1 : 0;
    p->oldRightDown = m->oldRightDown ? 1 : 0;
    p->oldMiddleDown = m->oldMiddleDown ? 1 : 0;

    p->wheel = (int16_t)m->wheel;

    /* Only numeric transfer, not in the Client dereferenced */
    p->device = (uint64_t)(uintptr_t)m->device;
    p->targetWindow = (uint64_t)(uintptr_t)m->targetWindow;

    p->hasBlocked = m->hasBlocked ? 1 : 0;
    p->isLinked = m->isLinked ? 1 : 0;

    p->r = m->color.r;
    p->g = m->color.g;
    p->b = m->color.b;
    p->a = m->color.a;
}

static BOOL WriteAll(HANDLE pipe, const void* data, DWORD size)
{
    const BYTE* ptr = (const BYTE*)data;

    while (size > 0)
    {
        DWORD written = 0;

        if (!WriteFile(pipe, ptr, size, &written, NULL))
            return FALSE;

        if (written == 0)
            return FALSE;

        ptr += written;
        size -= written;
    }

    return TRUE;
}

static unsigned __stdcall SenderThread(void* arg)
{
    (void)arg;

    while (InterlockedCompareExchange(&g_running, 1, 1))
    {
        HANDLE pipe = CreateNamedPipeA(
            PIPE_NAME,
            PIPE_ACCESS_OUTBOUND,
            PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
            1,
            sizeof(MouseBatchHeader) +
            MAX_MICE * sizeof(MousePacket),
            sizeof(MouseBatchHeader) +
            MAX_MICE * sizeof(MousePacket),
            0,
            NULL
        );

        if (pipe == INVALID_HANDLE_VALUE)
        {
            Sleep(1000);
            continue;
        }

        BOOL connected = ConnectNamedPipe(pipe, NULL) ? TRUE : (GetLastError() == ERROR_PIPE_CONNECTED);

        if (connected)
        {
            while (InterlockedCompareExchange(&g_running, 1, 1))
            {
                MouseData snapshot[MAX_MICE];
                MousePacket packets[MAX_MICE];
                UINT32 count;

                EnterCriticalSection(&g_lock);

                count = g_mouseCount;
                memcpy(snapshot, g_mice,
                    count * sizeof(MouseData));

                LeaveCriticalSection(&g_lock);

                for (UINT32 i = 0; i < count; i++)
                    CopyToPacket(&packets[i], &snapshot[i]);

                MouseBatchHeader header;
                header.version = 1;
                header.mouseCount = count;
                header.packetSize =
                    (UINT32)(sizeof(header) +
                        count * sizeof(MousePacket));

                /* First header after that all Mice in the correct order*/
                if (!WriteAll(pipe, &header, sizeof(header)))
                    break;

                if (count > 0 &&
                    !WriteAll(pipe, packets,
                        count * sizeof(MousePacket)))
                    break;

                Sleep(1000); // ca. 1000 Updates per second
            }
        }

        DisconnectNamedPipe(pipe);
        CloseHandle(pipe);
    }

    return 0;
}

/* Musst be called before the Sender-Thread*/
void InitMousePipeData(void)
{
    InitializeCriticalSection(&g_lock);
}

/* Copys all the registered Mice in the shared Snapshot */
void UpdateMouseData(const MouseData* mice, UINT32 count)
{
    if (count > MAX_MICE)
        count = MAX_MICE;

    EnterCriticalSection(&g_lock);

    memcpy(g_mice, mice, count * sizeof(MouseData));
    g_mouseCount = count;

    LeaveCriticalSection(&g_lock);
}





//---------------------------------------------------------------------------------------------
// Cursor Drawing with raw D3D11 Methods and Handling
//---------------------------------------------------------------------------------------------
HRESULT InitD3D11(HWND hwnd)
{
    DXGI_SWAP_CHAIN_DESC scDesc;
    ZeroMemory(&scDesc, sizeof(scDesc));

    RECT rect;
    GetClientRect(hwnd, &rect);

    UINT width = rect.right - rect.left;
    UINT height = rect.bottom - rect.top;

    scDesc.BufferCount = 2;
    scDesc.BufferDesc.Width = width;
    scDesc.BufferDesc.Height = height;
    scDesc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    scDesc.BufferDesc.RefreshRate.Numerator = 0;
    scDesc.BufferDesc.RefreshRate.Denominator = 1;

    scDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    scDesc.OutputWindow = hwnd;
    scDesc.SampleDesc.Count = 1;
    scDesc.SampleDesc.Quality = 0;
    scDesc.Windowed = TRUE;
    scDesc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    D3D_FEATURE_LEVEL featureLevel;

    HRESULT hr = D3D11CreateDeviceAndSwapChain(
        NULL,
        D3D_DRIVER_TYPE_HARDWARE,
        NULL,
        0,
        NULL,
        0,
        D3D11_SDK_VERSION,
        &scDesc,
        &g_swapChain,
        &g_device,
        &featureLevel,
        &g_context
    );

    if (FAILED(hr))
        return hr;

    ID3D11Texture2D* backBuffer = NULL;

    hr = IDXGISwapChain_GetBuffer(
        g_swapChain,
        0,
        &IID_ID3D11Texture2D,
        (void**)&backBuffer
    );

    if (FAILED(hr))
        return hr;

    hr = ID3D11Device_CreateRenderTargetView(
        g_device,
        (ID3D11Resource*)backBuffer,
        NULL,
        &g_renderTarget
    );

    ID3D11Texture2D_Release(backBuffer);

    if (FAILED(hr))
        return hr;

    ID3D11DeviceContext_OMSetRenderTargets(
        g_context,
        1,
        &g_renderTarget,
        NULL
    );

    D3D11_VIEWPORT viewport;
    ZeroMemory(&viewport, sizeof(viewport));

    viewport.Width = (FLOAT)width;
    viewport.Height = (FLOAT)height;
    viewport.MinDepth = 0.0f;
    viewport.MaxDepth = 1.0f;

    ID3D11DeviceContext_RSSetViewports(
        g_context,
        1,
        &viewport
    );

    hr = InitCursorRenderer();

    if (FAILED(hr))
        return hr;

    return S_OK;
}

HRESULT InitCursorRenderer(void)
{
    HRESULT hr;

    ID3DBlob* vsBlob = NULL;
    ID3DBlob* psBlob = NULL;

    hr = CompileShader(
        cursorVS,
        "main",
        "vs_4_0",
        &vsBlob
    );

    if (FAILED(hr))
        return hr;

    hr = CompileShader(
        cursorPS,
        "main",
        "ps_4_0",
        &psBlob
    );

    if (FAILED(hr))
    {
        ID3D10Blob_Release(vsBlob);
        return hr;
    }

    hr = ID3D11Device_CreateVertexShader(
        g_device,
        ID3D10Blob_GetBufferPointer(vsBlob),
        ID3D10Blob_GetBufferSize(vsBlob),
        NULL,
        &g_vertexShader
    );

    if (FAILED(hr))
        goto cleanup;

    hr = ID3D11Device_CreatePixelShader(
        g_device,
        ID3D10Blob_GetBufferPointer(psBlob),
        ID3D10Blob_GetBufferSize(psBlob),
        NULL,
        &g_pixelShader
    );

    if (FAILED(hr))
        goto cleanup;

    D3D11_INPUT_ELEMENT_DESC layout[] =
    {
        {
            "POSITION",
            0,
            DXGI_FORMAT_R32G32_FLOAT,
            0,
            offsetof(CursorVertex, x),
            D3D11_INPUT_PER_VERTEX_DATA,
            0
        },

        {
            "TEXCOORD",
            0,
            DXGI_FORMAT_R32G32_FLOAT,
            0,
            offsetof(CursorVertex, u),
            D3D11_INPUT_PER_VERTEX_DATA,
            0
        },
        {
            "COLOR",
            0,
            DXGI_FORMAT_R32G32B32A32_FLOAT,
            0,
            offsetof(CursorVertex, color),
            D3D11_INPUT_PER_VERTEX_DATA,
            0
        }
    };

    hr = ID3D11Device_CreateInputLayout(
        g_device,
        layout,
        3,
        ID3D10Blob_GetBufferPointer(vsBlob),
        ID3D10Blob_GetBufferSize(vsBlob),
        &g_inputLayout
    );

    if (FAILED(hr))
        goto cleanup;

    D3D11_BUFFER_DESC bufferDesc;
    ZeroMemory(&bufferDesc, sizeof(bufferDesc));

    bufferDesc.Usage = D3D11_USAGE_DYNAMIC;
    bufferDesc.ByteWidth =
        sizeof(CursorVertex) * 6 * MAX_MICE;
    bufferDesc.BindFlags =
        D3D11_BIND_VERTEX_BUFFER;
    bufferDesc.CPUAccessFlags =
        D3D11_CPU_ACCESS_WRITE;

    hr = ID3D11Device_CreateBuffer(
        g_device,
        &bufferDesc,
        NULL,
        &g_vertexBuffer
    );

    if (FAILED(hr))
        goto cleanup;

    hr = CreateCursorTexture();
    D3D11_SAMPLER_DESC samplerDesc;
    ZeroMemory(&samplerDesc, sizeof(samplerDesc));

    samplerDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;

    samplerDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
    samplerDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
    samplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;

    samplerDesc.ComparisonFunc = D3D11_COMPARISON_NEVER;

    hr = ID3D11Device_CreateSamplerState(
        g_device,
        &samplerDesc,
        &g_sampler
    );

    if (FAILED(hr))
        goto cleanup;

    D3D11_BUFFER_DESC cbDesc;
    ZeroMemory(&cbDesc, sizeof(cbDesc));

    cbDesc.Usage = D3D11_USAGE_DYNAMIC;
    cbDesc.ByteWidth = sizeof(ScreenConstants);
    cbDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    cbDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

    hr = ID3D11Device_CreateBuffer(
        g_device,
        &cbDesc,
        NULL,
        &g_constantBuffer
    );

    if (FAILED(hr))
        goto cleanup;

    D3D11_BLEND_DESC blendDesc;
    ZeroMemory(&blendDesc, sizeof(blendDesc));

    blendDesc.RenderTarget[0].BlendEnable = TRUE;

    blendDesc.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
    blendDesc.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
    blendDesc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;

    blendDesc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
    blendDesc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
    blendDesc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;

    blendDesc.RenderTarget[0].RenderTargetWriteMask =
        D3D11_COLOR_WRITE_ENABLE_ALL;

    hr = ID3D11Device_CreateBlendState(
        g_device,
        &blendDesc,
        &g_blendState
    );

    if (FAILED(hr))
        goto cleanup;

cleanup:

    if (vsBlob)
        ID3D10Blob_Release(vsBlob);

    if (psBlob)
        ID3D10Blob_Release(psBlob);

    return hr;
}


void ClearScreen(void)
{
    FLOAT color[4] = {
        1.0f,  // Rot
        0.0f,  // Grün
        1.0f,  // Blau
        1.0f   // Alpha
    };

    ID3D11DeviceContext_ClearRenderTargetView(
        g_context,
        g_renderTarget,
        color
    );
}

HRESULT CreateCursorTexture(void)
{
    unsigned char pixels[CURSOR_W * CURSOR_H * 4];

    for (int y = 0; y < CURSOR_H; y++)
    {
        for (int x = 0; x < CURSOR_W; x++)
        {
            int index = (y * CURSOR_W + x) * 4;

            if (cursorMask[y][x] == 'X')
            {
                pixels[index + 0] = 255; // R
                pixels[index + 1] = 255; // G
                pixels[index + 2] = 255; // B
                pixels[index + 3] = 255; // Alpha
            }
            else
            {
                pixels[index + 0] = 0;
                pixels[index + 1] = 0;
                pixels[index + 2] = 0;
                pixels[index + 3] = 0;
            }
        }
    }

    D3D11_TEXTURE2D_DESC desc;
    ZeroMemory(&desc, sizeof(desc));

    desc.Width = CURSOR_W;
    desc.Height = CURSOR_H;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_IMMUTABLE;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

    D3D11_SUBRESOURCE_DATA data;
    ZeroMemory(&data, sizeof(data));

    data.pSysMem = pixels;
    data.SysMemPitch = CURSOR_W * 4;

    ID3D11Texture2D* texture = NULL;

    HRESULT hr = ID3D11Device_CreateTexture2D(
        g_device,
        &desc,
        &data,
        &texture
    );

    if (FAILED(hr))
        return hr;

    hr = ID3D11Device_CreateShaderResourceView(
        g_device,
        (ID3D11Resource*)texture,
        NULL,
        &g_cursorTexture
    );

    ID3D11Texture2D_Release(texture);

    return hr;
}

HRESULT CompileShader(
    const char* source,
    const char* entry,
    const char* target,
    ID3DBlob** blob)
{
    ID3DBlob* errorBlob = NULL;

    HRESULT hr = D3DCompile(
        source,
        strlen(source),
        NULL,
        NULL,
        NULL,
        entry,
        target,
        0,
        0,
        blob,
        &errorBlob
    );

    if (FAILED(hr))
    {
        if (errorBlob)
        {
            printf(
                "Shader Fehler: %s\n",
                (char*)ID3D10Blob_GetBufferPointer(errorBlob)
            );

            ID3D10Blob_Release(errorBlob);
        }

        return hr;
    }

    if (errorBlob)
        ID3D10Blob_Release(errorBlob);

    return S_OK;
}



void Render(void)
{
    if (!g_context || !g_renderTarget)
        return;

    ClearScreen();

    FLOAT blendFactor[4] = { 0, 0, 0, 0 };

    ID3D11DeviceContext_OMSetBlendState(
        g_context,
        g_blendState,
        blendFactor,
        0xFFFFFFFF
    );
    /*
        transfer Monitorsize to Vertex Shader
    */
    D3D11_MAPPED_SUBRESOURCE mappedConstants;

    if (SUCCEEDED(
        ID3D11DeviceContext_Map(
            g_context,
            (ID3D11Resource*)g_constantBuffer,
            0,
            D3D11_MAP_WRITE_DISCARD,
            0,
            &mappedConstants
        )))
    {
        ScreenConstants* constants =
            (ScreenConstants*)mappedConstants.pData;

        constants->screenWidth = (float)screenWidth;
        constants->screenHeight = (float)screenHeight;

        ID3D11DeviceContext_Unmap(
            g_context,
            (ID3D11Resource*)g_constantBuffer,
            0
        );
    }

    /*
        binding Constant Buffer to Vertex Shader
    */
    ID3D11DeviceContext_VSSetConstantBuffers(
        g_context,
        0,
        1,
        &g_constantBuffer
    );

    /*
        generate Cursor-Vertices
    */
    CursorVertex vertices[MAX_MICE * 6];

    int vertexCount = 0;

    for (int i = 0; i < currentRegisteredMice; i++)
    {
        float x = (float)(mice[i].x - screenX);
        float y = (float)(mice[i].y - screenY);

        float w = (float)CURSOR_W;
        float h = (float)CURSOR_H;

        CursorVertex cursor[6] =
        {
            { x,     y,     0.0f, 0.0f, mice[i].color },
            { x + w, y,     1.0f, 0.0f, mice[i].color },
            { x,     y + h, 0.0f, 1.0f, mice[i].color },

            { x,     y + h, 0.0f, 1.0f, mice[i].color },
            { x + w, y,     1.0f, 0.0f, mice[i].color },
            { x + w, y + h, 1.0f, 1.0f, mice[i].color }
        };

        memcpy(
            &vertices[vertexCount],
            cursor,
            sizeof(cursor)
        );

        vertexCount += 6;
    }

    /*
        Vertex Buffer aktualisieren
    */
    if (vertexCount > 0)
    {
        D3D11_MAPPED_SUBRESOURCE mapped;

        HRESULT hr =
            ID3D11DeviceContext_Map(
                g_context,
                (ID3D11Resource*)g_vertexBuffer,
                0,
                D3D11_MAP_WRITE_DISCARD,
                0,
                &mapped
            );

        if (SUCCEEDED(hr))
        {
            memcpy(
                mapped.pData,
                vertices,
                sizeof(CursorVertex) * vertexCount
            );

            ID3D11DeviceContext_Unmap(
                g_context,
                (ID3D11Resource*)g_vertexBuffer,
                0
            );
        }

        /*
            Vertex Buffer
        */
        UINT stride = sizeof(CursorVertex);
        UINT offset = 0;

        ID3D11DeviceContext_IASetVertexBuffers(
            g_context,
            0,
            1,
            &g_vertexBuffer,
            &stride,
            &offset
        );

        /*
            Input Layout
        */
        ID3D11DeviceContext_IASetInputLayout(
            g_context,
            g_inputLayout
        );

        /*
            Triangle List
        */
        ID3D11DeviceContext_IASetPrimitiveTopology(
            g_context,
            D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST
        );

        /*
            Vertex Shader
        */
        ID3D11DeviceContext_VSSetShader(
            g_context,
            g_vertexShader,
            NULL,
            0
        );

        /*
            Pixel Shader
        */
        ID3D11DeviceContext_PSSetShader(
            g_context,
            g_pixelShader,
            NULL,
            0
        );

        /*
            Cursor Texture
        */
        ID3D11DeviceContext_PSSetShaderResources(
            g_context,
            0,
            1,
            &g_cursorTexture
        );

        /*
            Texture Sampler
        */
        ID3D11DeviceContext_PSSetSamplers(
            g_context,
            0,
            1,
            &g_sampler
        );

        /*
            Cursor zeichnen
        */
        ID3D11DeviceContext_Draw(
            g_context,
            vertexCount,
            0
        );
    }

    /*
        show Frame
    */
    IDXGISwapChain_Present(
        g_swapChain,
        1,
        0
    );
}