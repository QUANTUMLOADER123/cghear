// ============================================================================
//  Minimal headless raylib stub — used ONLY to run the game logic as a pure
//  C++ unit test without any window/GPU (see tests/run_headless.sh).
//  Mirrors the small subset of the raylib API that src/main.cpp uses.
// ============================================================================
#pragma once

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <cstdarg>

#ifndef PI
#define PI 3.14159265358979323846f
#endif

enum { LOG_WARNING = 4 };
enum { FLAG_VSYNC_HINT = 0x40, FLAG_MSAA_4X_HINT = 0x80 };
enum { MOUSE_BUTTON_LEFT = 0 };
enum { KEY_SPACE = 32, KEY_ENTER = 257 };
enum { GESTURE_TAP = 2 };

typedef struct Vector2 { float x, y; } Vector2;
typedef struct Color { unsigned char r, g, b, a; } Color;
typedef struct Camera2D { Vector2 offset; Vector2 target; float rotation; float zoom; } Camera2D;
typedef struct Font { int dummy; } Font;
typedef struct Sound { int dummy; } Sound;
typedef struct Wave {
    unsigned int frameCount;
    unsigned int sampleRate;
    unsigned int sampleSize;
    unsigned int channels;
    void *data;
} Wave;

static const Color BLANK    = { 0, 0, 0, 0 };
static const Color RAYWHITE = { 200, 200, 245, 255 };
static const Color WHITE    = { 255, 255, 255, 255 };
static const Color BLACK    = { 0, 0, 0, 255 };

// ------------------------------------------------------------- fake timing --
static int   hs_frame = 0;
static const int hs_totalFrames = 12000;

// ------------------------------------------------------------ fake storage --
static std::map<std::string, std::string> hs_fs;

inline void SetTraceLogLevel(int) {}
inline void SetConfigFlags(unsigned) {}
inline void InitWindow(int, int, const char *) { std::puts("[stub] window opened"); }
inline void CloseWindow(void) { std::puts("[stub] window closed"); }
inline bool WindowShouldClose(void) { return ++hs_frame > hs_totalFrames; }
inline bool IsMouseButtonPressed(int) { return hs_frame > 4500 && hs_frame % 37 == 0; }
inline bool IsKeyPressed(int) { return false; }
inline bool IsGestureDetected(int) { return false; }
inline int  GetScreenWidth(void) { return 720; }
inline int  GetScreenHeight(void) { return 1600; }
inline float GetFrameTime(void) { return 1.0f / 60.0f; }
inline double GetTime(void) { return (double)hs_frame / 60.0; }
inline int  GetRandomValue(int lo, int hi) { return lo + std::rand() % (hi - lo + 1); }
inline void BeginDrawing(void) {}
inline void EndDrawing(void) {}
inline void ClearBackground(Color) {}
inline void BeginMode2D(Camera2D) {}
inline void EndMode2D(void) {}
inline void TakeScreenshot(const char *p) { std::printf("[stub] screenshot -> %s\n", p); }

inline void InitAudioDevice(void) {}
inline void SetMasterVolume(float) {}
inline Sound LoadSoundFromWave(Wave w) { Sound s{}; (void)w; return s; }
inline void UnloadWave(Wave w) { std::free(w.data); }
inline void PlaySound(Sound) {}
inline void *MemAlloc(size_t n) { return std::malloc(n); }
inline void MemFree(void *p) { std::free(p); }

inline bool FileExists(const char *path) { return hs_fs.count(path) != 0; }
inline char *LoadFileText(const char *path) {
    auto it = hs_fs.find(path);
    if (it == hs_fs.end()) return nullptr;
    return strdup(it->second.c_str());
}
inline void SaveFileText(const char *path, const char *text) { hs_fs[path] = text; }

inline Font GetFontDefault(void) { return Font{ 0 }; }
inline Vector2 MeasureTextEx(Font, const char *t, float size, float) {
    return Vector2{ size * 0.5f * (float)strlen(t), size };
}
inline void DrawTextEx(Font, const char *, Vector2, float, float, Color) {}

inline Color Fade(Color c, float a) { c.a = (unsigned char)(c.a * a); return c; }
inline Color ColorFromHSV(float h, float s, float v) {
    h = fmodf(h, 360.0f); if (h < 0) h += 360.0f;
    float c = v * s, x = c * (1.0f - fabsf(fmodf(h / 60.0f, 2.0f) - 1.0f)), m = v - c;
    float r, g, b;
    if (h < 60)      { r = c; g = x; b = 0; }
    else if (h < 120){ r = x; g = c; b = 0; }
    else if (h < 180){ r = 0; g = c; b = x; }
    else if (h < 240){ r = 0; g = x; b = c; }
    else if (h < 300){ r = x; g = 0; b = c; }
    else             { r = c; g = 0; b = x; }
    Color out;
    out.r = (unsigned char)((r + m) * 255.0f);
    out.g = (unsigned char)((g + m) * 255.0f);
    out.b = (unsigned char)((b + m) * 255.0f);
    out.a = 255;
    return out;
}

inline void DrawTriangle(Vector2, Vector2, Vector2, Color) {}
inline void DrawLineEx(Vector2, Vector2, float, Color) {}
inline void DrawCircle(int x, int y, float r, Color) { (void)x; (void)y; (void)r; }
inline void DrawCircleV(Vector2 p, float r, Color) { (void)p; (void)r; }
inline void DrawRing(Vector2, float, float, float, float, int, Color) {}
inline void DrawRectangle(int, int, int, int, Color) {}
inline void DrawRectangleGradientV(int, int, int, int, Color, Color) {}

inline const char *TextFormat(const char *fmt, ...) {
    static char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    return buf;
}
