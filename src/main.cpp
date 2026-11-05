// ============================================================================
//   H Y P E R S T A C K
//   One-thumb neon tower stacker.
//
//   Pure C++17 + raylib. Zero asset files: every color, shape and sound
//   is generated procedurally at runtime.
//
//   HOW TO PLAY
//     * A slab slides over the tower. TAP (click / touch / space) to drop it.
//     * Whatever hangs over the edge is sliced off and falls.
//     * Land PERFECTLY (<= 9 units off) to keep the full size and build a
//       combo: 3+ perfects in a row regrow your slab and raise the chime pitch.
//     * Miss completely and the run is over. One thumb is all you need.
//
//   BUILD (desktop):  cmake -B build -DCMAKE_BUILD_TYPE=Release
//                     cmake --build build --parallel
//   BUILD (android):  bash android/build_apk.sh   ->  hyperstack.apk
// ============================================================================

#include "raylib.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

// --------------------------------------------------------------- constants --
static constexpr float DESIGN_H       = 900.0f;   // virtual screen height
static constexpr float BLOCK_H        = 54.0f;    // slab height, world units
static constexpr float START_SIZE     = 230.0f;   // base slab footprint
static constexpr float MIN_SLAB       = 4.0f;     // thinner than this = death
static constexpr float BASE_SPEED     = 250.0f;   // world units / second
static constexpr float SPEED_STEP     = 5.5f;     // speed gain per level
static constexpr float MAX_SPEED      = 640.0f;
static constexpr float PERFECT_EPS    = 9.0f;     // snap tolerance
static constexpr float SLIDE_MARGIN   = 115.0f;   // travel past the tower edge
static constexpr float GROW_PER_COMBO = 7.0f;     // slab regrow per perfect
static constexpr int   COMBO_GROW_MIN = 3;        // combo needed to regrow
static constexpr float GRAVITY        = 2600.0f;
static const char   *BEST_PATH        = "hyperstack_best.txt";

// ----------------------------------------------------------------- helpers --
static float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
static float lerpf(float a, float b, float t)    { return a + (b - a) * t; }

static Color shade(Color c, float f)
{
    c.r = (unsigned char)clampf(c.r * f + 0.5f, 0.0f, 255.0f);
    c.g = (unsigned char)clampf(c.g * f + 0.5f, 0.0f, 255.0f);
    c.b = (unsigned char)clampf(c.b * f + 0.5f, 0.0f, 255.0f);
    return c;
}

// Dimetric projection: world (x, y up, z) -> screen.
static Vector2 proj(float x, float y, float z)
{
    return Vector2{ (x - z) * 0.8660254f, (x + z) * 0.5f - y };
}

static Color slabColor(int level, float hueBase)
{
    float h = fmodf(hueBase + level * 4.5f, 360.0f);
    if (h < 0) h += 360.0f;
    return ColorFromHSV(h, 0.62f, 0.95f);
}

static Color goldColor(float t) { return ColorFromHSV(fmodf(45.0f + 10.0f * sinf(t * 3.0f), 360.0f), 0.85f, 1.0f); }

// -------------------------------------------------------------- persistence --
static int loadBest()
{
    if (!FileExists(BEST_PATH)) return 0;
    char *txt = LoadFileText(BEST_PATH);
    int v = txt ? atoi(txt) : 0;
    if (txt) MemFree(txt);
    return v < 0 ? 0 : v;
}

static void saveBest(int v)
{
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%d", v);
    SaveFileText(BEST_PATH, buf);
}

// ------------------------------------------------------- procedural audio --
typedef float (*SampleFn)(float t);

static Sound soundFrom(float dur, SampleFn fn)
{
    const int sr = 22050;
    int n = (int)(sr * dur);
    if (n < 1) n = 1;
    Wave w{};
    w.frameCount = (unsigned)n;
    w.sampleRate = (unsigned)sr;
    w.sampleSize = 16;
    w.channels   = 1;
    short *data = (short *)MemAlloc((size_t)n * sizeof(short));
    for (int i = 0; i < n; i++) {
        float t = (float)i / (float)sr;
        data[i] = (short)clampf(fn(t) * 32767.0f, -32768.0f, 32767.0f);
    }
    w.data = data;
    Sound snd = LoadSoundFromWave(w);
    UnloadWave(w);                      // frees the sample buffer
    return snd;
}

static float thudFn(float t)
{
    float f = 50.0f + 190.0f * expf(-t * 16.0f);
    return sinf(2.0f * PI * f * t) * expf(-t * 22.0f) * 0.9f;
}

static float cutFn(float t)
{
    float nse = (float)GetRandomValue(-1000, 1000) / 1000.0f;
    return nse * expf(-t * 38.0f) * 0.5f;
}

static float g_chimeFreq = 523.25f;
static float chimeFn(float t)
{
    float a = sinf(2.0f * PI * g_chimeFreq * t)
            + 0.5f * sinf(2.0f * PI * g_chimeFreq * 1.5f * t)
            + 0.25f * sinf(2.0f * PI * g_chimeFreq * 2.0f * t);
    float atk = clampf(t / 0.012f, 0.0f, 1.0f);
    return a * expf(-t * 9.0f) * atk * 0.34f;
}

static float overFn(float t)
{
    float f = 60.0f + 220.0f * expf(-t * 4.5f);
    return sinf(2.0f * PI * f * t) * expf(-t * 4.5f) * 0.7f
         + sinf(2.0f * PI * f * 0.5f * t) * expf(-t * 3.0f) * 0.4f;
}

// ------------------------------------------------------------------- types --
struct Slab   { float x, z, hx, hz, y; Color color; };
struct Debris { float x, z, hx, hz, y; float vx, vz, vy; Color color; float t; };
struct Ring   { float x, z, y, t; Color color; };
struct Popup  { float px, py, t; std::string text; Color color; float size; };
struct Dust   { float x, y, r, spd, alpha; };

struct Game
{
    enum class State { MENU, PLAY, DEAD };

    std::vector<Slab>   tower;
    std::vector<Debris> debris;
    std::vector<Ring>   rings;
    std::vector<Popup>  popups;
    std::vector<Dust>   dust;

    // sliding slab
    bool  axisX;                 // slides along X (else Z)
    float pos, speed, dir;
    float sMin, sMax;            // bounce bounds
    float curHx, curHz;
    float curY;

    int   level;                 // tower.size()
    int   score, best, combo;
    float hueBase;
    float shake, flash, zoomKick;
    float camY, camZoom;
    float stateT;
    float tapCd;                 // input debounce (mouse press + tap gesture)

    State state;
    bool  newBest;

    Sound sndThud{}, sndCut{}, sndOver{}, sndChime[3]{};

    float topY() const { return (float)tower.size() * BLOCK_H; }

    // ---------------------------------------------------------------- core --
    void reset(bool toMenu)
    {
        hueBase = (float)GetRandomValue(0, 359);
        tower.clear(); debris.clear(); rings.clear(); popups.clear();

        Slab base{};
        base.x = 0; base.z = 0;
        base.hx = START_SIZE / 2.0f; base.hz = START_SIZE / 2.0f;
        base.y = 0;
        base.color = slabColor(0, hueBase);
        tower.push_back(base);

        level = 1; score = 0; combo = 0; newBest = false;
        shake = flash = zoomKick = 0.0f;
        state = toMenu ? State::MENU : State::PLAY;
        stateT = 0.0f;

        camY = topY();
        camZoom = (float)GetScreenHeight() / DESIGN_H * (toMenu ? 0.94f : 1.0f);

        spawnCur();

        if (!toMenu) popupAt(0, topY() + 210, 0, "GO!", RAYWHITE, 52);
    }

    void spawnCur()
    {
        const Slab &p = tower.back();
        axisX = (level % 2) == 1;
        speed = std::min(MAX_SPEED, BASE_SPEED + (float)(level - 1) * SPEED_STEP);
        if (state == State::MENU) speed = std::min(speed, BASE_SPEED * 1.25f);
        dir = (level % 4 < 2) ? 1.0f : -1.0f;

        curHx = p.hx; curHz = p.hz;
        curY  = (float)level * BLOCK_H;

        float half = axisX ? p.hx : p.hz;
        float c    = axisX ? p.x  : p.z;
        sMin = c - half - SLIDE_MARGIN;
        sMax = c + half + SLIDE_MARGIN;
        pos  = (dir > 0) ? sMin : sMax;
    }

    void popupAt(float wx, float wy, float wz, const char *txt, Color c, float size)
    {
        Vector2 p = proj(wx, wy, wz);
        Popup pp{};
        pp.px = p.x; pp.py = p.y; pp.t = 0;
        pp.text = txt; pp.color = c; pp.size = size;
        popups.push_back(pp);
    }

    void addDebris(float lo, float hi)
    {
        Debris d{};
        d.hx = curHx; d.hz = curHz; d.y = curY;
        d.color = slabColor(level, hueBase);
        if (axisX) { d.x = (lo + hi) / 2.0f; d.hx = (hi - lo) / 2.0f; d.vx = dir * 130.0f; d.vz = 0; }
        else       { d.z = (lo + hi) / 2.0f; d.hz = (hi - lo) / 2.0f; d.vz = dir * 130.0f; d.vx = 0; }
        d.vy = 160.0f; d.t = 0;
        debris.push_back(d);
        PlaySound(sndCut);
    }

    void die()
    {
        Debris d{};
        d.hx = curHx; d.hz = curHz; d.y = curY;
        d.color = slabColor(level, hueBase);
        d.x = axisX ? pos : tower.back().x;
        d.z = axisX ? tower.back().z : pos;
        d.vx = axisX ? dir * 90.0f : 0.0f;
        d.vz = axisX ? 0.0f : dir * 90.0f;
        d.vy = 140.0f; d.t = 0;
        debris.push_back(d);

        state = State::DEAD;
        stateT = 0.0f;
        newBest = score > best;
        if (newBest) { best = score; saveBest(best); }
        PlaySound(sndOver);
        shake = 14.0f;
    }

    void place()
    {
        const Slab p = tower.back();
        float cp = axisX ? p.x : p.z;             // previous center on axis
        float hp = axisX ? p.hx : p.hz;           // previous half size on axis
        float half = axisX ? curHx : curHz;
        float lo = pos - half;
        float hi = pos + half;
        float off = pos - cp;

        bool perfect = fabsf(off) <= PERFECT_EPS;
        float nLo, nHi;
        if (perfect) { nLo = cp - hp; nHi = cp + hp; }
        else {
            nLo = std::max(lo, cp - hp);
            nHi = std::min(hi, cp + hp);
        }

        if (nHi - nLo < MIN_SLAB) { die(); return; }

        if (perfect) {
            combo++;
            PlaySound(sndChime[std::min((combo - 1) / 3, 2)]);
            flash = std::max(flash, 0.16f);
            zoomKick = 0.022f;
            shake = std::max(shake, 6.0f);
        } else {
            if (lo < nLo - 0.5f) addDebris(lo, nLo);
            if (hi > nHi + 0.5f) addDebris(nHi, hi);
            combo = 0;
            PlaySound(sndThud);
            shake = std::max(shake, 3.5f);
        }

        // commit the slab
        Slab s{};
        s.y = curY;
        s.color = slabColor(level, hueBase);
        if (axisX) { s.x = (nLo + nHi) / 2.0f; s.hx = (nHi - nLo) / 2.0f; s.z = p.z; s.hz = curHz; }
        else       { s.z = (nLo + nHi) / 2.0f; s.hz = (nHi - nLo) / 2.0f; s.x = p.x; s.hx = curHx; }
        tower.push_back(s);

        if (perfect) {
            rings.push_back(Ring{ s.x, s.z, s.y, 0.0f, goldColor(GetTime()) });
            if (combo >= 2)
                popupAt(s.x, s.y + BLOCK_H + 36, s.z, TextFormat("PERFECT x%d", combo), goldColor(GetTime()), 44);
            else
                popupAt(s.x, s.y + BLOCK_H + 36, s.z, "PERFECT", goldColor(GetTime()), 44);

            if (combo >= COMBO_GROW_MIN) {          // reward: the slab regrows
                s.hx = std::min(s.hx + GROW_PER_COMBO, START_SIZE / 2.0f);
                s.hz = std::min(s.hz + GROW_PER_COMBO, START_SIZE / 2.0f);
                tower.back() = s;
                curHx = s.hx; curHz = s.hz;
                popupAt(s.x, s.y + BLOCK_H + 104, s.z, "+SIZE", ColorFromHSV(185, 0.8f, 1.0f), 34);
                flash = 0.22f;
            }
        }

        score = (int)tower.size() - 1;
        level++;
        spawnCur();
    }

    // -------------------------------------------------------------- update --
    void update(float dt)
    {
        stateT += dt;
        tapCd  -= dt;

        if (state != State::DEAD) {
            pos += dir * speed * dt;
            if (pos > sMax) { pos = sMax; dir = -1.0f; }
            if (pos < sMin) { pos = sMin; dir =  1.0f; }

            if (state == State::MENU) {           // menu autopilot: always perfect
                const Slab &p = tower.back();
                float c = axisX ? p.x : p.z;
                if ((dir > 0 && pos >= c) || (dir < 0 && pos <= c)) place();
            }
        }

        bool tap = IsMouseButtonPressed(MOUSE_BUTTON_LEFT)
                || IsKeyPressed(KEY_SPACE)
                || IsKeyPressed(KEY_ENTER)
                || IsGestureDetected(GESTURE_TAP);
        if (tap && tapCd <= 0.0f) {
            tapCd = 0.14f;
            if (state == State::MENU) {
                state = State::PLAY; stateT = 0.0f;
                speed = std::min(MAX_SPEED, BASE_SPEED + (float)(level - 1) * SPEED_STEP);
                PlaySound(sndThud);
            }
            else if (state == State::PLAY) place();
            else if (state == State::DEAD && stateT > 0.8f) reset(false);
        }

        shake    *= expf(-dt * 7.0f);
        flash    *= expf(-dt * 6.0f);
        zoomKick *= expf(-dt * 5.0f);

        for (size_t i = 0; i < debris.size();) {
            Debris &d = debris[i];
            d.t += dt;
            d.vy -= GRAVITY * dt;
            d.y  += d.vy * dt;
            d.x  += d.vx * dt;
            d.z  += d.vz * dt;
            if (d.t > 2.4f || d.y < camY - 4000.0f) debris.erase(debris.begin() + i);
            else i++;
        }
        for (size_t i = 0; i < rings.size();) {
            rings[i].t += dt;
            if (rings[i].t > 0.5f) rings.erase(rings.begin() + i);
            else i++;
        }
        for (size_t i = 0; i < popups.size();) {
            Popup &pp = popups[i];
            pp.t += dt;
            pp.py -= 62.0f * dt;
            if (pp.t > 1.0f) popups.erase(popups.begin() + i);
            else i++;
        }

        // camera
        float zb = (float)GetScreenHeight() / DESIGN_H;
        float zt;
        if (state == State::DEAD)
            zt = clampf((float)GetScreenHeight() * 0.72f / (topY() + 240.0f), 0.16f, zb);
        else
            zt = zb * (state == State::MENU ? 0.94f : 1.0f);
        camZoom = lerpf(camZoom, zt, 1.0f - expf(-dt * 4.0f));

        float cy = (state == State::DEAD) ? topY() * 0.45f : topY();
        camY = lerpf(camY, cy, 1.0f - expf(-dt * 6.0f));
    }

    // ---------------------------------------------------------------- draw --
    void drawSlab(float x, float z, float hx, float hz, float y, Color c, Color outline, float a) const
    {
        float t = y + BLOCK_H;
        Vector2 A  = proj(x - hx, t, z - hz);
        Vector2 B  = proj(x + hx, t, z - hz);
        Vector2 C  = proj(x + hx, t, z + hz);
        Vector2 D  = proj(x - hx, t, z + hz);
        Vector2 B2 = proj(x + hx, y, z - hz);
        Vector2 C2 = proj(x + hx, y, z + hz);
        Vector2 D2 = proj(x - hx, y, z + hz);

        Color cr = Fade(shade(c, 0.70f), a);      // right face
        DrawTriangle(B, C, C2, cr);
        DrawTriangle(B, C2, B2, cr);
        Color cl = Fade(shade(c, 0.52f), a);      // left face
        DrawTriangle(D, C, C2, cl);
        DrawTriangle(D, C2, D2, cl);
        Color ct = Fade(shade(c, 1.13f), a);      // top face
        DrawTriangle(A, B, C, ct);
        DrawTriangle(A, C, D, ct);

        if (outline.a > 2) {
            DrawLineEx(A, B, 3.0f, outline);
            DrawLineEx(B, C, 3.0f, outline);
            DrawLineEx(C, D, 3.0f, outline);
            DrawLineEx(D, A, 3.0f, outline);
        }
    }

    void drawWorld()
    {
        float W = (float)GetScreenWidth();
        float H = (float)GetScreenHeight();
        float dt = GetFrameTime();

        // sky gradient slowly drifts through the tower hue
        float bh = fmodf(hueBase + GetTime() * 3.0f, 360.0f);
        Color top = ColorFromHSV(fmodf(bh + 210.0f, 360.0f), 0.50f, 0.10f);
        Color bot = ColorFromHSV(fmodf(bh + 240.0f, 360.0f), 0.55f, 0.17f);
        DrawRectangleGradientV(0, 0, (int)W, (int)H, top, bot);

        // parallax dust
        for (Dust &d : dust) {
            d.y -= d.spd * dt;
            float sy = fmodf(d.y - camY * 0.12f * d.r, H);
            if (sy < 0) sy += H;
            DrawCircleV(Vector2{ d.x * W, sy }, d.r, Fade(RAYWHITE, d.alpha));
        }

        // camera with shake
        float sx = (float)GetRandomValue(-100, 100) / 100.0f * shake;
        float sy = (float)GetRandomValue(-100, 100) / 100.0f * shake;
        Camera2D cam{};
        cam.target   = Vector2{ 0, -camY };
        cam.offset   = Vector2{ W / 2.0f + sx, H * 0.66f + sy };
        cam.zoom     = camZoom * (1.0f + zoomKick);
        cam.rotation = 0.0f;
        BeginMode2D(cam);

        // halo behind the tower
        Vector2 hc = proj(0, topY() + 130.0f, 0);
        DrawCircleV(hc, 300.0f, Fade(RAYWHITE, 0.045f));
        DrawCircleV(hc, 480.0f, Fade(RAYWHITE, 0.028f));

        // tower
        float cull = camY - H / camZoom - BLOCK_H * 2.0f;
        for (size_t i = 0; i < tower.size(); i++) {
            const Slab &s = tower[i];
            if (s.y < cull) continue;
            drawSlab(s.x, s.z, s.hx, s.hz, s.y, s.color, BLANK, 1.0f);
        }

        // sliding slab
        if (state != State::DEAD) {
            Color c = slabColor(level, hueBase);
            Color o = Fade(RAYWHITE, 0.32f + 0.22f * sinf(GetTime() * 9.0f));
            drawSlab(axisX ? pos : tower.back().x,
                     axisX ? tower.back().z : pos,
                     curHx, curHz, curY, c, o, 1.0f);
        }

        // falling debris
        for (const Debris &d : debris) {
            float a = clampf(2.2f - d.t, 0.0f, 1.0f) * 0.95f;
            drawSlab(d.x, d.z, d.hx, d.hz, d.y, d.color, BLANK, a);
        }

        // perfect rings
        for (const Ring &r : rings) {
            float rr = 10.0f + r.t * 560.0f;
            float a  = clampf(1.0f - r.t / 0.5f, 0.0f, 1.0f) * 0.85f;
            Vector2 p = proj(r.x, r.y + BLOCK_H, r.z);
            DrawRing(p, rr, rr + 7.0f, 0.0f, 360.0f, 44, Fade(r.color, a));
        }

        // popups
        for (const Popup &pp : popups) {
            float a = clampf(1.4f - pp.t * 1.4f, 0.0f, 1.0f);
            Vector2 m = MeasureTextEx(GetFontDefault(), pp.text.c_str(), pp.size, pp.size * 0.12f);
            DrawTextEx(GetFontDefault(), pp.text.c_str(),
                       Vector2{ pp.px - m.x / 2.0f, pp.py }, pp.size, pp.size * 0.12f,
                       Fade(pp.color, a));
        }

        EndMode2D();

        if (flash > 0.004f)
            DrawRectangle(0, 0, (int)W, (int)H, Fade(RAYWHITE, flash));
    }

    void drawUI()
    {
        float W = (float)GetScreenWidth();
        float H = (float)GetScreenHeight();
        float ui = H / DESIGN_H;
        Font f = GetFontDefault();
        float t = GetTime();

        auto ctext = [&](const char *s, float cy, float size, Color col, float shadow) {
            float sp = size * 0.12f;
            Vector2 m = MeasureTextEx(f, s, size, sp);
            Vector2 p = Vector2{ W / 2.0f - m.x / 2.0f, cy };
            if (shadow > 0.0f)
                DrawTextEx(f, s, Vector2{ p.x + 3.0f * ui, p.y + 3.0f * ui }, size, sp, Fade(BLACK, shadow));
            DrawTextEx(f, s, p, size, sp, col);
        };

        if (state == State::MENU) {
            Color tc = ColorFromHSV(fmodf(hueBase + 90.0f, 360.0f), 0.35f, 1.0f);
            ctext("H Y P E R S T A C K", H * 0.28f, 68 * ui, tc, 0.65f);
            ctext("TAP TO START", H * 0.43f, 34 * ui,
                  Fade(RAYWHITE, 0.55f + 0.45f * sinf(t * 4.0f)), 0.5f);
            ctext(TextFormat("BEST  %d", best), H * 0.51f, 30 * ui, Fade(RAYWHITE, 0.75f), 0.4f);
            ctext("one thumb  -  pure C++  -  raylib", H * 0.94f, 20 * ui, Fade(RAYWHITE, 0.35f), 0.0f);
        }
        else if (state == State::PLAY) {
            ctext(TextFormat("%d", score), H * 0.05f, 118 * ui, Fade(RAYWHITE, 0.92f), 0.5f);
            if (combo >= 2)
                ctext(TextFormat("PERFECT x%d", combo), H * 0.05f + 132 * ui, 34 * ui,
                      Fade(goldColor(t), 0.75f + 0.25f * sinf(t * 8.0f)), 0.4f);
        }
        else { // DEAD
            DrawRectangle(0, 0, (int)W, (int)H, Fade(BLACK, 0.45f * clampf(stateT * 2.0f, 0.0f, 1.0f)));
            ctext("CRASHED", H * 0.26f, 64 * ui, ColorFromHSV(355, 0.70f, 1.0f), 0.6f);
            ctext(TextFormat("%d", score), H * 0.37f, 118 * ui, RAYWHITE, 0.6f);
            if (newBest)
                ctext("NEW BEST!", H * 0.54f, 42 * ui,
                      Fade(goldColor(t), 0.7f + 0.3f * sinf(t * 6.0f)), 0.4f);
            else
                ctext(TextFormat("BEST  %d", best), H * 0.55f, 32 * ui, Fade(RAYWHITE, 0.7f), 0.4f);
            if (stateT > 0.8f)
                ctext("TAP TO RETRY", H * 0.70f, 34 * ui,
                      Fade(RAYWHITE, 0.55f + 0.45f * sinf(t * 4.0f)), 0.5f);
        }
    }
};

// -------------------------------------------------------------------- main --
int main()
{
    SetTraceLogLevel(LOG_WARNING);
    SetConfigFlags(FLAG_VSYNC_HINT | FLAG_MSAA_4X_HINT);
    InitWindow(500, 900, "HYPERSTACK");
    InitAudioDevice();
    SetMasterVolume(0.85f);

    Game g;
    g.best = loadBest();
    g.sndThud = soundFrom(0.14f, thudFn);
    g.sndCut  = soundFrom(0.08f, cutFn);
    g.sndOver = soundFrom(0.90f, overFn);
    g_chimeFreq = 523.25f; g.sndChime[0] = soundFrom(0.40f, chimeFn);
    g_chimeFreq = 659.25f; g.sndChime[1] = soundFrom(0.40f, chimeFn);
    g_chimeFreq = 783.99f; g.sndChime[2] = soundFrom(0.40f, chimeFn);

    for (int i = 0; i < 64; i++) {
        Dust d{};
        d.x = (float)GetRandomValue(0, 1000) / 1000.0f;
        d.y = (float)GetRandomValue(0, 1000) / 1000.0f * 1400.0f;
        d.r = 1.0f + (float)GetRandomValue(0, 220) / 100.0f;
        d.spd = 8.0f + (float)GetRandomValue(0, 3200) / 100.0f;
        d.alpha = 0.05f + (float)GetRandomValue(0, 12) / 100.0f;
        g.dust.push_back(d);
    }

    g.reset(true);

    const char *shot = getenv("HYPERSHOT");
    if (shot)                                    // pre-roll the menu demo tower
        for (int i = 0; i < 900; i++) g.update(1.0f / 60.0f);

    int frame = 0;
    while (!WindowShouldClose()) {
        float dt = std::min(GetFrameTime(), 1.0f / 30.0f);
        g.update(dt);
        BeginDrawing();
        ClearBackground(BLACK);
        g.drawWorld();
        g.drawUI();
        EndDrawing();
        if (shot && ++frame == 150) { TakeScreenshot(shot); break; }
    }

#ifdef HS_HEADLESS
    std::printf("HEADLESS score=%d best=%d level=%d state=%d debris=%zu tower=%zu\n",
                g.score, g.best, g.level, (int)g.state, g.debris.size(), g.tower.size());
#endif

    CloseWindow();
    return 0;
}
