#include "ui.h"
#include "screens.h"    // for the shared fonts
#include "codepanel.h"  // CODE_FONT_SIZE

#include <string.h>

#include <string.h>

bool UiButtonClicked(Rectangle bounds)
{
    return CheckCollisionPointRec(GetMousePosition(), bounds) && IsMouseButtonReleased(MOUSE_BUTTON_LEFT);
}

void UiButtonDraw(Rectangle bounds, const char *text)
{
    UiButtonDrawEx(bounds, text, UI_ACCENT, true);
}

void UiButtonDrawEx(Rectangle bounds, const char *text, Color fill, bool enabled)
{
    bool hover = enabled && CheckCollisionPointRec(GetMousePosition(), bounds);
    bool down = hover && IsMouseButtonDown(MOUSE_BUTTON_LEFT);

    Color color = fill;
    if (down) color = ColorBrightness(fill, -0.2f);
    else if (hover) color = ColorBrightness(fill, 0.15f);

    DrawRectangleRounded(bounds, 0.25f, 8, color);

    const float size = 20.0f;
    const float spacing = size/10.0f;
    Vector2 textSize = MeasureTextEx(font, text, size, spacing);
    Vector2 pos = { bounds.x + (bounds.width - textSize.x)/2.0f, bounds.y + (bounds.height - textSize.y)/2.0f };
    DrawTextEx(font, text, pos, size, spacing, WHITE);
}

void UiDrawHeader(const char *title)
{
    DrawTextEx(font, title, (Vector2){ 60, 30 }, 36, 3.6f, UI_TEXT);
    DrawRectangle(60, 78, 840, 3, UI_ACCENT);
}

void UiDrawTextCentered(const char *text, float centerX, float y, float fontSize, Color color)
{
    const float spacing = fontSize/10.0f;
    Vector2 size = MeasureTextEx(font, text, fontSize, spacing);
    DrawTextEx(font, text, (Vector2){ centerX - size.x/2.0f, y }, fontSize, spacing, color);
}

// Shared by drawing and measuring. clip=false ignores bounds.height.
static float WrapText(const char *text, Rectangle bounds, float fontSize, Color color, bool draw, bool clip)
{
    const float spacing = fontSize/10.0f;
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
        float width = MeasureTextEx(font, word, fontSize, spacing).x;

        // Soft wrap: start a new line and drop the leading spaces
        if (x > bounds.x && (x - bounds.x) + width > bounds.width)
        {
            x = bounds.x;
            y += lineHeight;
            while (*word == ' ') word++;
            width = MeasureTextEx(font, word, fontSize, spacing).x;
        }

        if (clip && y + lineHeight > bounds.y + bounds.height) break;   // out of room

        if (draw) DrawTextEx(font, word, (Vector2){ x, y }, fontSize, spacing, color);
        x += width + spacing;
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
static void AppendChar(char *buf, int bufSize, int c)
{
    int len = (int)strlen(buf);
    if (c >= 32 && c <= 126 && len < bufSize - 1)   // the default font only has plain ASCII
    {
        buf[len] = (char)c;
        buf[len + 1] = '\0';
    }
}

void UiTextBoxUpdate(char *buf, int bufSize)
{
    int key = GetCharPressed();
    while (key > 0)
    {
        AppendChar(buf, bufSize, key);
        key = GetCharPressed();
    }

    int len = (int)strlen(buf);
    if ((IsKeyPressed(KEY_BACKSPACE) || IsKeyPressedRepeat(KEY_BACKSPACE)) && len > 0) buf[len - 1] = '\0';

    bool ctrl = IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL);
    if (ctrl && IsKeyPressed(KEY_V))
    {
        const char *clip = GetClipboardText();
        if (clip != NULL) for (; *clip; clip++) AppendChar(buf, bufSize, (unsigned char)*clip);
    }
}

void UiTextBoxDraw(Rectangle bounds, const char *label, const char *buf, bool active, bool masked)
{
    DrawTextEx(font, label, (Vector2){ bounds.x, bounds.y - 20 }, 16, 1.6f, UI_MUTED);

    DrawRectangleRec(bounds, WHITE);
    DrawRectangleLinesEx(bounds, active ? 2.0f : 1.0f, active ? UI_ACCENT : (Color){ 200, 204, 216, 255 });

    char shown[128];
    int n = (int)strlen(buf);
    if (n > (int)sizeof(shown) - 1) n = (int)sizeof(shown) - 1;
    if (masked) memset(shown, '*', (size_t)n);
    else memcpy(shown, buf, (size_t)n);
    shown[n] = '\0';

    const float size = 20.0f;
    const float spacing = 2.0f;

    // Long text scrolls: drop characters from the front until the end fits
    const char *start = shown;
    while (*start && MeasureTextEx(font, start, size, spacing).x > bounds.width - 24) start++;

    Vector2 pos = { bounds.x + 10, bounds.y + (bounds.height - size)/2.0f };
    DrawTextEx(font, start, pos, size, spacing, UI_TEXT);

    if (active && ((int)(GetTime()*2.0) % 2 == 0))
    {
        float w = MeasureTextEx(font, start, size, spacing).x;
        DrawRectangle((int)(pos.x + w + 2), (int)pos.y, 2, (int)size, UI_TEXT);
    }
}
