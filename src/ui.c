#include "ui.h"
#include "screens.h"    // for the shared fonts
#include "codepanel.h"  // CODE_FONT_SIZE
#include "users.h"      // GetCurrentUser

#include <string.h>
#include <stdio.h>
#include <math.h>

// Big text uses the large copy of the font so headings are not stretched (see raylib_game.c)
static Font UiFontFor(float s)
{
    return (s > 28.0f && fontLarge.texture.id != 0) ? fontLarge : font;
}

void UiText(const char *text, Vector2 pos, float size, Color color)
{
    float s = size*UI_FONT_SCALE;

    // Start text on a whole screen pixel; text that starts half way between pixels looks soft
    pos.x = roundf(pos.x*uiScale)/uiScale;
    pos.y = roundf(pos.y*uiScale)/uiScale;

    DrawTextEx(UiFontFor(s), text, pos, s, s*UI_FONT_SPACING, color);
}

void UiTextFit(const char *text, Vector2 pos, float size, float maxWidth, Color color)
{
    if (UiMeasureText(text, size).x <= maxWidth) { UiText(text, pos, size, color); return; }

    char buf[256];
    int n = (int)strlen(text);
    if (n > (int)sizeof(buf) - 4) n = (int)sizeof(buf) - 4;
    while (n > 0)
    {
        memcpy(buf, text, n);
        memcpy(buf + n, "...", 4);
        if (UiMeasureText(buf, size).x <= maxWidth) break;
        n--;
    }
    UiText(buf, pos, size, color);
}

Vector2 UiMeasureText(const char *text, float size)
{
    float s = size*UI_FONT_SCALE;
    return MeasureTextEx(UiFontFor(s), text, s, s*UI_FONT_SPACING);
}

void UiBeginScissor(Rectangle area)
{
    // Scissor works in window pixels, which the 2D camera does not touch, so convert by hand
    BeginScissorMode((int)(uiOffset.x + area.x*uiScale), (int)(uiOffset.y + area.y*uiScale),
                     (int)(area.width*uiScale + 0.5f), (int)(area.height*uiScale + 0.5f));
}

// Set by UiScrollUpdate while a scroll thumb is dragged and on the frame it is released
static bool scrollGrabbed = false;

bool UiButtonClicked(Rectangle bounds)
{
    if (scrollGrabbed) return false;
    return CheckCollisionPointRec(GetMousePosition(), bounds) && IsMouseButtonReleased(MOUSE_BUTTON_LEFT);
}

//----------------------------------------------------------------------------------
// Scroll bar
//----------------------------------------------------------------------------------
#define SCROLL_MIN_THUMB  28.0f
#define SCROLL_HIT_PAD    8.0f      // the bar is thin, so accept clicks a little either side

static Rectangle ScrollThumb(const UiScroll *sc, Rectangle track, float viewHeight, float maxScroll)
{
    float h = track.height*viewHeight/(viewHeight + maxScroll);
    if (h < SCROLL_MIN_THUMB) h = SCROLL_MIN_THUMB;
    if (h > track.height) h = track.height;
    float t = (maxScroll > 0) ? sc->offset/maxScroll : 0.0f;
    return (Rectangle){ track.x, track.y + t*(track.height - h), track.width, h };
}

// Last scroll bar state, shown by the F3 debug overlay (see raylib_game.c)
char uiScrollDebug[160] = "no scroll bar on this screen";

void UiScrollUpdate(UiScroll *sc, Rectangle track, float viewHeight, float maxScroll, float wheelStep)
{
    UiScrollUpdateEx(sc, track, viewHeight, maxScroll, wheelStep, true);
}

void UiScrollUpdateEx(UiScroll *sc, Rectangle track, float viewHeight, float maxScroll, float wheelStep, bool arrowKeys)
{
    scrollGrabbed = false;
    {
        Rectangle t = ScrollThumb(sc, track, viewHeight, maxScroll > 0 ? maxScroll : 1);
        snprintf(uiScrollDebug, sizeof(uiScrollDebug), "scroll: thumb x %.0f-%.0f y %.0f-%.0f  offset %.0f/%.0f  dragging %d",
                 track.x - SCROLL_HIT_PAD, track.x + track.width + SCROLL_HIT_PAD, t.y, t.y + t.height, sc->offset, maxScroll, sc->dragging);
    }
    if (maxScroll <= 0) { sc->offset = 0; sc->dragging = false; return; }

    sc->offset -= GetMouseWheelMove()*wheelStep;
    if (arrowKeys && IsKeyDown(KEY_DOWN)) sc->offset += 6.0f;
    if (arrowKeys && IsKeyDown(KEY_UP)) sc->offset -= 6.0f;

    Vector2 mouse = GetMousePosition();
    Rectangle thumb = ScrollThumb(sc, track, viewHeight, maxScroll);
    Rectangle hit = { track.x - SCROLL_HIT_PAD, track.y, track.width + 2*SCROLL_HIT_PAD, track.height };

    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && CheckCollisionPointRec(mouse, hit))
    {
        sc->dragging = true;
        // Grabbing the thumb keeps it under the mouse; clicking the track jumps the thumb there
        if (mouse.y >= thumb.y && mouse.y <= thumb.y + thumb.height) sc->grab = mouse.y - thumb.y;
        else sc->grab = thumb.height/2.0f;
    }

    if (sc->dragging)
    {
        float range = track.height - thumb.height;
        if (range > 0) sc->offset = (mouse.y - sc->grab - track.y)/range*maxScroll;
        if (!IsMouseButtonDown(MOUSE_BUTTON_LEFT)) sc->dragging = false;
        scrollGrabbed = true;       // includes the release frame
    }

    if (sc->offset < 0) sc->offset = 0;
    if (sc->offset > maxScroll) sc->offset = maxScroll;
}

void UiScrollDraw(const UiScroll *sc, Rectangle track, float viewHeight, float maxScroll)
{
    if (maxScroll <= 0) return;

    Rectangle thumb = ScrollThumb(sc, track, viewHeight, maxScroll);
    Rectangle hit = { track.x - SCROLL_HIT_PAD, track.y, track.width + 2*SCROLL_HIT_PAD, track.height };
    bool hot = sc->dragging || CheckCollisionPointRec(GetMousePosition(), hit);

    DrawRectangleRounded(track, 1.0f, 4, UI_PANEL2);
    DrawRectangleRounded(thumb, 1.0f, 4, hot ? UI_ACCENT : UI_MUTED);
}

void UiButtonDraw(Rectangle bounds, const char *text)
{
    UiButtonDrawEx(bounds, text, UI_ACCENT, true);
}

void UiButtonDrawEx(Rectangle bounds, const char *text, Color fill, bool enabled)
{
    bool hover = enabled && CheckCollisionPointRec(GetMousePosition(), bounds);
    bool down = hover && IsMouseButtonDown(MOUSE_BUTTON_LEFT);
    float h = UiHover(bounds, hover);               // eased 0..1: colour, glow and size glide instead of jumping

    // UI_MUTED means "secondary": a dark button with an outline instead of a coloured one
    bool secondary = UiSameColor(fill, UI_MUTED);
    Color color = secondary ? UI_PANEL2 : fill;
    Color textColor = secondary ? UI_TEXT : WHITE;
    if (down) color = ColorBrightness(color, -0.2f);
    else color = ColorBrightness(color, h*(secondary ? 0.12f : 0.15f));
    if (!enabled) { color = Fade(color, 0.45f); textColor = Fade(textColor, 0.5f); }

    // Grows a little while hovered and presses in while held
    float grow = down ? -1.0f : h*2.0f;
    Rectangle face = { bounds.x - grow, bounds.y - grow, bounds.width + 2*grow, bounds.height + 2*grow };

    // A soft glow under coloured buttons when hovered
    if (h > 0.01f && !secondary) DrawRectangleRounded((Rectangle){ face.x - 3, face.y - 3, face.width + 6, face.height + 6 }, 0.3f, 8, Fade(fill, 0.25f*h));
    DrawRectangleRounded(face, 0.25f, 8, color);
    if (secondary) DrawRectangleRoundedLinesEx(face, 0.25f, 8, 1.0f, UiMix(UI_BORDER, UI_MUTED, h));

    const float size = 20.0f;
    Vector2 textSize = UiMeasureText(text, size);
    Vector2 pos = { bounds.x + (bounds.width - textSize.x)/2.0f, bounds.y + (bounds.height - textSize.y)/2.0f };
    UiText(text, pos, size, textColor);
}

static const char *rainCode[] = {
    "int *p = &x;", "*p = 42;", "struct Node {", "    int data;", "    struct Node *next;", "};",
    "Node *n = malloc(sizeof(Node));", "n->next = head;", "head = n;", "for (int i = 0; i < n; i++)",
    "    sum += a[i];", "free(ptr);", "ptr = NULL;", "char s[] = \"hello\";", "strcpy(dst, src);",
    "int fact(int n) {", "    if (n <= 1) return 1;", "    return n * fact(n - 1);", "}", "printf(\"%d\\n\", x);",
    "void swap(int *a, int *b) {", "    int t = *a;", "    *a = *b; *b = t;", "while (cur != NULL)",
    "    cur = cur->next;", "int m[3][3] = {0};", "Box operator+(const Box &b);", "~LinkedList() { clear(); }",
    "#include <stdio.h>", "return 0;", "if (head == NULL) return;", "delete[] arr;",
};

void UiDrawCodeRain(Rectangle area, float speed)
{
    const int count = (int)(sizeof(rainCode)/sizeof(rainCode[0]));
    const float lh = 19.0f, size = 13.0f;
    float t = (float)GetTime()*speed;
    int first = (int)(t/lh);
    float frac = fmodf(t, lh);
    unsigned char cls[64];

    for (int k = 0; k*lh < area.height + lh; k++)
    {
        int idx = (first + k + (int)area.x/37)%count;      // the x offset makes each area show different lines
        const char *line = rainCode[idx];
        float y = area.y + area.height - (k*lh + frac);
        // Fade in at the bottom and out at the top
        float pos = (y - area.y)/area.height;
        float alpha = 0.35f*sinf(pos*PI);
        if (alpha <= 0.01f) continue;

        int n = (int)strlen(line);
        if (n > 63) n = 63;
        CodeClassify(line, n, cls);
        float cw = MeasureTextEx(monoFont, "M", size, 0).x;
        char run[64];
        int i = 0;
        while (i < n)
        {
            int j = i;
            while (j < n && cls[j] == cls[i]) j++;
            memcpy(run, line + i, (size_t)(j - i));
            run[j - i] = '\0';
            DrawTextEx(monoFont, run, (Vector2){ area.x + i*cw, y }, size, 0, Fade(CodeClassColor(cls[i]), alpha));
            i = j;
        }
    }
}

bool UiSameColor(Color a, Color b) { return a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a; }

//----------------------------------------------------------------------------------
// Motion helpers
//----------------------------------------------------------------------------------
Color UiMix(Color a, Color b, float t)
{
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;
    return (Color){ (unsigned char)(a.r + (b.r - a.r)*t + 0.5f), (unsigned char)(a.g + (b.g - a.g)*t + 0.5f),
                    (unsigned char)(a.b + (b.b - a.b)*t + 0.5f), (unsigned char)(a.a + (b.a - a.a)*t + 0.5f) };
}

float UiEaseOut(float t)
{
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;
    float u = 1.0f - t;
    return 1.0f - u*u*u;
}

float UiEaseIn(float t)
{
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;
    return t*t*t;
}

// One slot per hovered thing, found again by its rectangle. A slot that has not been drawn for a
// while is reused, so scrolling lists (whose rectangles keep moving) never run out.
#define HOVER_SLOTS 64
typedef struct HoverSlot { int x, y, w, h; float value; double used; } HoverSlot;
static HoverSlot hoverSlots[HOVER_SLOTS];

float UiHover(Rectangle bounds, bool hovered)
{
    int x = (int)(bounds.x + 0.5f), y = (int)(bounds.y + 0.5f), w = (int)(bounds.width + 0.5f), h = (int)(bounds.height + 0.5f);
    double now = GetTime();

    HoverSlot *slot = NULL, *oldest = &hoverSlots[0];
    for (int i = 0; i < HOVER_SLOTS; i++)
    {
        HoverSlot *s = &hoverSlots[i];
        if (s->used > 0.0 && s->x == x && s->y == y && s->w == w && s->h == h) { slot = s; break; }
        if (s->used < oldest->used) oldest = s;
    }
    if (slot == NULL)
    {
        slot = oldest;
        *slot = (HoverSlot){ x, y, w, h, 0.0f, 0.0 };
    }
    else if (now - slot->used > 0.5) slot->value = 0.0f;     // not drawn for a while: start from rest

    float dt = GetFrameTime();
    if (dt > 0.1f) dt = 0.1f;
    float target = hovered ? 1.0f : 0.0f;
    slot->value += (target - slot->value)*(1.0f - expf(-dt*16.0f));      // about 60 ms to cover most of the way
    if (fabsf(target - slot->value) < 0.003f) slot->value = target;

    slot->used = now > 0.0 ? now : 0.001;
    return slot->value;
}

static double screenStart = 0.0;

void UiMarkScreenStart(void) { screenStart = GetTime(); }

float UiEnter(float delay, float duration)
{
    if (duration <= 0.0f) return 1.0f;
    return UiEaseOut((float)((GetTime() - screenStart - delay)/duration));
}

void UiFadeIn(Rectangle area, float t)
{
    if (t >= 0.999f) return;
    DrawRectangleRounded((Rectangle){ area.x - 4, area.y - 4, area.width + 8, area.height + 8 }, 0.12f, 8, Fade(UI_BG, 1.0f - t));
}

// A soft round glow: stacked see-through circles, brightest in the middle
// (DrawCircleGradient changed its arguments between raylib versions, so it is not used)
static void SoftGlow(float cx, float cy, float radius, Color c, float alpha)
{
    const int rings = 14;
    for (int i = 0; i < rings; i++)
    {
        float r = radius*(1.0f - (float)i/rings);
        DrawCircle((int)cx, (int)cy, r, Fade(c, alpha/rings*1.6f));
    }
}

void UiDrawBackground(float width, float height)
{
    DrawRectangle(0, 0, (int)width, (int)height, UI_BG);
    // Dot grid drifting slowly up and left, with a faint glow in one corner
    float t = (float)GetTime();
    float off = fmodf(t*6.0f, 32.0f);
    for (float y = -off; y < height; y += 32.0f)
        for (float x = -off*0.5f; x < width; x += 32.0f)
            DrawRectangle((int)x, (int)y, 2, 2, (Color){ 30, 37, 46, 255 });
    SoftGlow(width*0.85f, -40, height*0.7f, UI_ACCENT2, 0.10f);
    SoftGlow(width*0.05f, height + 60, height*0.6f, UI_ACCENT, 0.08f);
}

void UiDrawHeader(const char *title)
{
    UiText(title, (Vector2){ 60, 30 }, 36, UI_TEXT);
    DrawRectangleGradientH(60, 78, 840, 3, UI_ACCENT, UI_ACCENT2);
}

void UiDrawLoggedInAs(void)
{
    const char *name = GetCurrentUser();
    if (name[0] == '\0') return;
    UiTextFit(TextFormat("Logged in as %s", name), (Vector2){ 60, 86 }, 16, 840, UI_MUTED);
}

void UiDrawTextCentered(const char *text, float centerX, float y, float fontSize, Color color)
{
    Vector2 size = UiMeasureText(text, fontSize);
    UiText(text, (Vector2){ centerX - size.x/2.0f, y }, fontSize, color);
}

// Shared by drawing and measuring. clip=false ignores bounds.height.
static float WrapText(const char *text, Rectangle bounds, float fontSize, Color color, bool draw, bool clip)
{
    const float lineHeight = fontSize*1.4f;

    float x = bounds.x;
    float y = bounds.y;
    const char *p = text;
    char token[128];

    while (*p)
    {
        if (*p == '\n')
        {
            x = bounds.x;
            y += lineHeight;
            p++;
            continue;
        }

        // A token is any leading spaces followed by one word
        int n = 0;
        while (*p == ' ' && n < (int)sizeof(token) - 1) token[n++] = *p++;
        while (*p && *p != ' ' && *p != '\n' && n < (int)sizeof(token) - 1) token[n++] = *p++;
        token[n] = '\0';

        const char *word = token;
        float width = UiMeasureText(word, fontSize).x;

        // Soft wrap: start a new line and drop the leading spaces
        if (x > bounds.x && (x - bounds.x) + width > bounds.width)
        {
            x = bounds.x;
            y += lineHeight;
            while (*word == ' ') word++;
            width = UiMeasureText(word, fontSize).x;
        }

        if (clip && y + lineHeight > bounds.y + bounds.height) break;   // out of room

        if (draw) UiText(word, (Vector2){ x, y }, fontSize, color);
        x += width;
    }

    return y + lineHeight;
}

float UiDrawWrappedText(const char *text, Rectangle bounds, float fontSize, Color color)
{
    return WrapText(text, bounds, fontSize, color, true, true);
}

float UiMeasureWrappedText(const char *text, float width, float fontSize)
{
    return WrapText(text, (Rectangle){ 0, 0, width, 0 }, fontSize, WHITE, false, false);
}

//----------------------------------------------------------------------------------
// Text box
//----------------------------------------------------------------------------------
//----------------------------------------------------------------------------------
// Single-line text boxes with a movable cursor (based on TextField from the bug tracker)
//----------------------------------------------------------------------------------
// Only one box has focus at a time, so the cursor ("caret") is shared. It belongs to the buffer
// it was last used with; when a different buffer gets focus the caret starts at its end.
// UiTextBox* use the UI font, UiCodeBox* the code font (the exam's typed answers).
#define BOX_TEXT_MAX 256

static int caret = 0;                   // 0 = before the first character
static const char *caretOwner = NULL;   // buffer the caret belongs to
static double caretStamp = 0.0;         // last move or edit; the caret blinks from then

static bool KeyRepeats(int key) { return IsKeyPressed(key) || IsKeyPressedRepeat(key); }

static float BoxPad(bool mono) { return mono ? 12.0f : 10.0f; }
static float BoxFontSize(bool mono) { return mono ? (float)CODE_FONT_SIZE : 20.0f; }

// The text as drawn: stars for passwords
static int BoxShown(const char *buf, bool masked, char *out)
{
    int n = (int)strlen(buf);
    if (n > BOX_TEXT_MAX - 1) n = BOX_TEXT_MAX - 1;
    if (masked) memset(out, '*', (size_t)n); else memcpy(out, buf, (size_t)n);
    out[n] = '\0';
    return n;
}

// Distance from the start of the text to where character k is drawn
static float BoxPenX(const char *shown, int k, bool mono)
{
    if (k <= 0) return 0.0f;
    char prefix[BOX_TEXT_MAX];
    memcpy(prefix, shown, (size_t)k);
    prefix[k] = '\0';
    if (mono) return MeasureTextEx(monoFont, prefix, CODE_FONT_SIZE, 0).x;
    float s = BoxFontSize(false)*UI_FONT_SCALE;
    return UiMeasureText(prefix, BoxFontSize(false)).x + s*UI_FONT_SPACING;   // DrawTextEx adds spacing after every character
}

// How far the text is shifted left so the caret stays inside the box
static float BoxShift(const char *shown, int pos, Rectangle bounds, bool mono)
{
    float room = bounds.width - 2*BoxPad(mono) - 2;
    float x = BoxPenX(shown, pos, mono);
    return (x > room) ? x - room : 0.0f;
}

static bool BoxInsert(char *buf, int bufSize, int c)
{
    int len = (int)strlen(buf);
    if (c < 32 || c > 126 || len >= bufSize - 1) return false;     // the fonts only have plain ASCII
    memmove(buf + caret + 1, buf + caret, (size_t)(len - caret + 1));
    buf[caret++] = (char)c;
    return true;
}

static bool IsWordChar(char c) { return c != ' '; }

static void BoxUpdate(Rectangle bounds, char *buf, int bufSize, bool masked, bool mono)
{
    int len = (int)strlen(buf);
    if (caretOwner != buf) { caretOwner = buf; caret = len; caretStamp = GetTime(); }
    if (caret > len) caret = len;               // the text was cleared or shortened elsewhere

    bool ctrl = IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL);
    bool moved = false;

    // Clicking in the box puts the caret at the nearest gap between characters
    if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT) && CheckCollisionPointRec(GetMousePosition(), bounds))
    {
        char shown[BOX_TEXT_MAX];
        int n = BoxShown(buf, masked, shown);
        float x = GetMousePosition().x - (bounds.x + BoxPad(mono)) + BoxShift(shown, caret, bounds, mono);
        int best = 0;
        float bestDist = 1e9f;
        for (int k = 0; k <= n; k++)
        {
            float d = fabsf(BoxPenX(shown, k, mono) - x);
            if (d < bestDist) { bestDist = d; best = k; }
        }
        caret = best;
        moved = true;
    }

    // Typing and pasting insert at the caret
    for (int key = GetCharPressed(); key > 0; key = GetCharPressed()) if (BoxInsert(buf, bufSize, key)) moved = true;
    if (ctrl && IsKeyPressed(KEY_V))
    {
        const char *clip = GetClipboardText();
        if (clip != NULL) for (; *clip; clip++) if (BoxInsert(buf, bufSize, (unsigned char)*clip)) moved = true;
    }
    len = (int)strlen(buf);

    // Backspace deletes before the caret, Delete after it (Ctrl: a whole word)
    if (KeyRepeats(KEY_BACKSPACE) && caret > 0)
    {
        int from = caret - 1;
        if (ctrl) { while (from > 0 && !IsWordChar(buf[from])) from--; while (from > 0 && IsWordChar(buf[from - 1])) from--; }
        memmove(buf + from, buf + caret, (size_t)(len - caret + 1));
        caret = from;
        moved = true;
    }
    len = (int)strlen(buf);
    if (KeyRepeats(KEY_DELETE) && caret < len)
    {
        int to = caret + 1;
        if (ctrl) { while (to < len && IsWordChar(buf[to])) to++; while (to < len && !IsWordChar(buf[to])) to++; }
        memmove(buf + caret, buf + to, (size_t)(len - to + 1));
        moved = true;
    }
    len = (int)strlen(buf);

    // Moving the caret: arrows (Ctrl jumps a word), Home and End
    if (KeyRepeats(KEY_LEFT) && caret > 0)
    {
        caret--;
        if (ctrl) { while (caret > 0 && !IsWordChar(buf[caret])) caret--; while (caret > 0 && IsWordChar(buf[caret - 1])) caret--; }
        moved = true;
    }
    if (KeyRepeats(KEY_RIGHT) && caret < len)
    {
        caret++;
        if (ctrl) { while (caret < len && IsWordChar(buf[caret])) caret++; while (caret < len && !IsWordChar(buf[caret])) caret++; }
        moved = true;
    }
    if (IsKeyPressed(KEY_HOME)) { caret = 0; moved = true; }
    if (IsKeyPressed(KEY_END)) { caret = len; moved = true; }

    if (moved) caretStamp = GetTime();          // keep the caret visible right after it moves
}

static void BoxDraw(Rectangle bounds, const char *buf, bool active, bool masked, bool mono)
{
    DrawRectangleRounded(bounds, 0.15f, 6, UI_INPUT);
    DrawRectangleRoundedLinesEx(bounds, 0.15f, 6, active ? 2.0f : 1.0f, active ? UI_ACCENT : UI_BORDER);

    char shown[BOX_TEXT_MAX];
    int n = BoxShown(buf, masked, shown);
    int pos = (active && caretOwner == buf) ? caret : n;
    if (pos > n) pos = n;

    float size = BoxFontSize(mono);
    float shift = BoxShift(shown, pos, bounds, mono);
    Vector2 at = { bounds.x + BoxPad(mono) - shift, bounds.y + (bounds.height - size)/2.0f };

    UiBeginScissor((Rectangle){ bounds.x + 2, bounds.y + 2, bounds.width - 4, bounds.height - 4 });
    if (mono) DrawTextEx(monoFont, shown, at, size, 0, UI_TEXT);
    else UiText(shown, at, size, UI_TEXT);

    if (active && ((int)((GetTime() - caretStamp)*2.0) % 2 == 0))
    {
        float cx = at.x + BoxPenX(shown, pos, mono);
        DrawRectangle((int)cx, (int)at.y, 2, (int)size, UI_TEXT);
    }
    EndScissorMode();
}

void UiTextBoxUpdate(Rectangle bounds, char *buf, int bufSize, bool masked) { BoxUpdate(bounds, buf, bufSize, masked, false); }

void UiTextBoxDraw(Rectangle bounds, const char *label, const char *buf, bool active, bool masked)
{
    UiText(label, (Vector2){ bounds.x, bounds.y - 22 }, 16, UI_MUTED);
    BoxDraw(bounds, buf, active, masked, false);
}

void UiCodeBoxUpdate(Rectangle bounds, char *buf, int bufSize) { BoxUpdate(bounds, buf, bufSize, false, true); }
void UiCodeBoxDraw(Rectangle bounds, const char *buf, bool active) { BoxDraw(bounds, buf, active, false, true); }
