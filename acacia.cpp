// acacia.cpp : Defines the entry point for the application.
//

#include "framework.h"
#include "acacia.h"
#include "engine.h"
#include "engine3d.h"

#define MAX_LOADSTRING 100

// Global Variables:
HINSTANCE hInst;                                // current instance
WCHAR szTitle[MAX_LOADSTRING];                  // The title bar text
WCHAR szWindowClass[MAX_LOADSTRING];            // the main window class name

// Engine state
static acacia::Renderer g_renderer(640, 360);
static acacia::Input    g_input;
static acacia::Scene    g_scene;
static HWND             g_hWnd = nullptr;

static void SetupScene()
{
    using namespace acacia;
    const float W = float(g_renderer.width()), H = float(g_renderer.height());

    Entity& player = g_scene.spawn({ W / 2 - 12, H / 2 - 12 }, { 24, 24 }, { 80, 200, 120 });
    player.onUpdate = [W, H](Entity& e, const Input& in, float dt) {
        const float speed = 220.f;
        e.vel = { 0, 0 };
        if (in.down(VK_LEFT)  || in.down('A')) e.vel.x -= speed;
        if (in.down(VK_RIGHT) || in.down('D')) e.vel.x += speed;
        if (in.down(VK_UP)    || in.down('W')) e.vel.y -= speed;
        if (in.down(VK_DOWN)  || in.down('S')) e.vel.y += speed;
        // Keep the player on screen.
        Vec2 next = e.pos + e.vel * dt;
        if (next.x < 0 || next.x + e.size.x > W) e.vel.x = 0;
        if (next.y < 0 || next.y + e.size.y > H) e.vel.y = 0;
    };

    // Bouncing obstacles.
    for (int i = 0; i < 6; ++i) {
        Entity& b = g_scene.spawn({ 40.f + i * 90.f, 30.f + i * 40.f }, { 20, 20 }, { 220, uint8_t(80 + i * 20), 80 });
        b.vel = { (i % 2 ? 90.f : -90.f) - i * 10.f, 70.f + i * 15.f };
        b.onUpdate = [W, H](Entity& e, const Input&, float dt) {
            Vec2 next = e.pos + e.vel * dt;
            if (next.x < 0 || next.x + e.size.x > W) e.vel.x = -e.vel.x;
            if (next.y < 0 || next.y + e.size.y > H) e.vel.y = -e.vel.y;
        };
    }
}

// ---- 3D demo: fly camera over procedural terrain (Tab toggles with the 2D demo)
static bool             g_mode3D = true;
static acacia::Camera   g_camera;
static acacia::Lighting g_lighting;
static std::unique_ptr<acacia::Terrain> g_terrain;
static uint32_t         g_seed = 1337;
static float            g_sunAngle = 1.1f;   // radians, see Lighting::setSunAngle
static bool             g_torch = false;
static bool             g_sunDirty = true;

static void GenerateTerrain()
{
    acacia::TerrainParams p;
    p.seed = g_seed;
    g_terrain = std::make_unique<acacia::Terrain>(p);
    float mid = g_terrain->extent() * 0.5f;
    g_camera.pos = { mid, g_terrain->params().amplitude + 30.f, -30.f };
    g_camera.yaw = 3.14159f;                 // face +Z, toward the island
    g_camera.pitch = -0.3f;
    g_sunDirty = true;
}

static void Tick3D(float dt)
{
    using namespace acacia;
    const float move = (g_input.down(VK_SHIFT) ? 90.f : 35.f) * dt, turn = 1.6f * dt;
    Vec3 fwd = g_camera.forward(), right = g_camera.right();
    if (g_input.down('W')) g_camera.pos += fwd * move;
    if (g_input.down('S')) g_camera.pos += fwd * -move;
    if (g_input.down('D')) g_camera.pos += right * move;
    if (g_input.down('A')) g_camera.pos += right * -move;
    if (g_input.down('E')) g_camera.pos.y += move;
    if (g_input.down('Q')) g_camera.pos.y -= move;
    if (g_input.down(VK_LEFT))  g_camera.yaw -= turn;
    if (g_input.down(VK_RIGHT)) g_camera.yaw += turn;
    if (g_input.down(VK_UP))    g_camera.pitch += turn;
    if (g_input.down(VK_DOWN))  g_camera.pitch -= turn;
    // Mouse look: hold the right button; the cursor is hidden and recentred each tick.
    static bool looking = false;
    bool wantLook = GetForegroundWindow() == g_hWnd && (GetAsyncKeyState(VK_RBUTTON) & 0x8000);
    if (wantLook) {
        RECT rc; GetClientRect(g_hWnd, &rc);
        POINT centre{ rc.right / 2, rc.bottom / 2 }, cur;
        ClientToScreen(g_hWnd, &centre);
        GetCursorPos(&cur);
        if (looking) {
            g_camera.yaw += (cur.x - centre.x) * 0.003f;
            g_camera.pitch -= (cur.y - centre.y) * 0.003f;
        } else ShowCursor(FALSE);
        SetCursorPos(centre.x, centre.y);
        looking = true;
    } else if (looking) { ShowCursor(TRUE); looking = false; }
    g_camera.pitch = std::clamp(g_camera.pitch, -1.5f, 1.5f);

    // Don't let the camera sink into the ground.
    float ground = std::max(g_terrain->heightAt(g_camera.pos.x, g_camera.pos.z), g_terrain->params().seaLevel);
    g_camera.pos.y = std::max(g_camera.pos.y, ground + 2.f);

    if (g_input.down('Z')) { g_sunAngle -= 0.6f * dt; g_sunDirty = true; }
    if (g_input.down('X')) { g_sunAngle += 0.6f * dt; g_sunDirty = true; }
    if (g_input.pressed('F')) g_torch = !g_torch;
    if (g_input.pressed('R')) { g_seed = g_seed * 1664525u + 1013904223u; GenerateTerrain(); }
}

static void Render3D()
{
    using namespace acacia;
    if (g_sunDirty) {
        g_lighting.setSunAngle(g_sunAngle);
        g_lighting.updateAtmosphere();
        g_terrain->bakeShadows(g_lighting.sunDir);
        g_sunDirty = false;
    }
    g_lighting.points.clear();
    if (g_torch) g_lighting.points.push_back({ g_camera.pos + g_camera.forward() * 6.f, { 1.0f, 0.8f, 0.55f }, 60.f, 2.f });

    g_renderer.clear({ 0, 0, 0 });
    drawSky(g_renderer, g_camera, g_lighting);
    drawMesh(g_renderer, g_terrain->water(), g_camera, g_lighting);
    drawMesh(g_renderer, g_terrain->mesh(), g_camera, g_lighting);
    drawMesh(g_renderer, g_terrain->trees(), g_camera, g_lighting);
}

static void Tick(float dt)
{
    if (g_input.pressed(VK_ESCAPE)) DestroyWindow(g_hWnd);
    if (g_input.pressed(VK_TAB)) g_mode3D = !g_mode3D;
    if (g_mode3D) Tick3D(dt); else g_scene.update(g_input, dt);
    g_input.endTick();
}

static void Render()
{
    if (g_mode3D) Render3D();
    else
    {
        g_renderer.clear({ 20, 24, 32 });
        g_scene.draw(g_renderer);
    }
    HDC hdc = GetDC(g_hWnd);
    RECT rc; GetClientRect(g_hWnd, &rc);
    g_renderer.present(hdc, rc.right, rc.bottom);
    ReleaseDC(g_hWnd, hdc);
}

// Forward declarations of functions included in this code module:
ATOM                MyRegisterClass(HINSTANCE hInstance);
BOOL                InitInstance(HINSTANCE, int);
LRESULT CALLBACK    WndProc(HWND, UINT, WPARAM, LPARAM);
INT_PTR CALLBACK    About(HWND, UINT, WPARAM, LPARAM);

int APIENTRY wWinMain(_In_ HINSTANCE hInstance,
                     _In_opt_ HINSTANCE hPrevInstance,
                     _In_ LPWSTR    lpCmdLine,
                     _In_ int       nCmdShow)
{
    UNREFERENCED_PARAMETER(hPrevInstance);
    UNREFERENCED_PARAMETER(lpCmdLine);

    // Initialize global strings
    LoadStringW(hInstance, IDS_APP_TITLE, szTitle, MAX_LOADSTRING);
    LoadStringW(hInstance, IDC_ACACIA, szWindowClass, MAX_LOADSTRING);
    MyRegisterClass(hInstance);

    // Perform application initialization:
    if (!InitInstance (hInstance, nCmdShow))
    {
        return FALSE;
    }

    HACCEL hAccelTable = LoadAccelerators(hInstance, MAKEINTRESOURCE(IDC_ACACIA));

    SetupScene();
    GenerateTerrain();

    MSG msg{};
    acacia::Clock clock;
    acacia::FixedStep stepper(60.0);
    bool running = true;

    // Main loop: drain messages, run fixed-rate ticks, render once per frame.
    while (running)
    {
        while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE))
        {
            if (msg.message == WM_QUIT) { running = false; break; }
            if (!TranslateAccelerator(msg.hwnd, hAccelTable, &msg))
            {
                TranslateMessage(&msg);
                DispatchMessage(&msg);
            }
        }
        if (!running) break;

        stepper.advance(clock.lap(), Tick);
        Render();
        Sleep(1);  // yield; keeps CPU use sane without vsync
    }

    return (int) msg.wParam;
}



//
//  FUNCTION: MyRegisterClass()
//
//  PURPOSE: Registers the window class.
//
ATOM MyRegisterClass(HINSTANCE hInstance)
{
    WNDCLASSEXW wcex;

    wcex.cbSize = sizeof(WNDCLASSEX);

    wcex.style          = CS_HREDRAW | CS_VREDRAW;
    wcex.lpfnWndProc    = WndProc;
    wcex.cbClsExtra     = 0;
    wcex.cbWndExtra     = 0;
    wcex.hInstance      = hInstance;
    wcex.hIcon          = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_ACACIA));
    wcex.hCursor        = LoadCursor(nullptr, IDC_ARROW);
    wcex.hbrBackground  = (HBRUSH)(COLOR_WINDOW+1);
    wcex.lpszMenuName   = MAKEINTRESOURCEW(IDC_ACACIA);
    wcex.lpszClassName  = szWindowClass;
    wcex.hIconSm        = LoadIcon(wcex.hInstance, MAKEINTRESOURCE(IDI_SMALL));

    return RegisterClassExW(&wcex);
}

//
//   FUNCTION: InitInstance(HINSTANCE, int)
//
//   PURPOSE: Saves instance handle and creates main window
//
//   COMMENTS:
//
//        In this function, we save the instance handle in a global variable and
//        create and display the main program window.
//
BOOL InitInstance(HINSTANCE hInstance, int nCmdShow)
{
   hInst = hInstance; // Store instance handle in our global variable

   HWND hWnd = CreateWindowW(szWindowClass, szTitle, WS_OVERLAPPEDWINDOW,
      CW_USEDEFAULT, 0, CW_USEDEFAULT, 0, nullptr, nullptr, hInstance, nullptr);

   if (!hWnd)
   {
      return FALSE;
   }

   g_hWnd = hWnd;
   ShowWindow(hWnd, nCmdShow);
   UpdateWindow(hWnd);

   return TRUE;
}

//
//  FUNCTION: WndProc(HWND, UINT, WPARAM, LPARAM)
//
//  PURPOSE: Processes messages for the main window.
//
//  WM_COMMAND  - process the application menu
//  WM_PAINT    - Paint the main window
//  WM_DESTROY  - post a quit message and return
//
//
LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
    case WM_COMMAND:
        {
            int wmId = LOWORD(wParam);
            // Parse the menu selections:
            switch (wmId)
            {
            case IDM_ABOUT:
                DialogBox(hInst, MAKEINTRESOURCE(IDD_ABOUTBOX), hWnd, About);
                break;
            case IDM_EXIT:
                DestroyWindow(hWnd);
                break;
            default:
                return DefWindowProc(hWnd, message, wParam, lParam);
            }
        }
        break;
    case WM_PAINT:
        {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hWnd, &ps);
            RECT rc; GetClientRect(hWnd, &rc);
            g_renderer.present(hdc, rc.right, rc.bottom);
            EndPaint(hWnd, &ps);
        }
        break;
    case WM_ERASEBKGND:
        return 1;  // present() paints the whole client area
    case WM_KEYDOWN:
    case WM_KEYUP:
        g_input.keyEvent(wParam, message == WM_KEYDOWN);
        break;
    case WM_KILLFOCUS:
        g_input.releaseAll();
        break;
    case WM_DESTROY:
        PostQuitMessage(0);
        break;
    default:
        return DefWindowProc(hWnd, message, wParam, lParam);
    }
    return 0;
}

// Message handler for about box.
INT_PTR CALLBACK About(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
    UNREFERENCED_PARAMETER(lParam);
    switch (message)
    {
    case WM_INITDIALOG:
        return (INT_PTR)TRUE;

    case WM_COMMAND:
        if (LOWORD(wParam) == IDOK || LOWORD(wParam) == IDCANCEL)
        {
            EndDialog(hDlg, LOWORD(wParam));
            return (INT_PTR)TRUE;
        }
        break;
    }
    return (INT_PTR)FALSE;
}
