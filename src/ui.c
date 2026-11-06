#include "ui.h"
#include "screens.h"    // for the shared fonts
#include "codepanel.h"  // CODE_FONT_SIZE

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

bool UiTextInputUpdate(char *buf, int size)
{
    int len = (int)strlen(buf);

    int key = GetCharPressed();
    while (key > 0)
    {
        if (key >= 32 && key <= 126 && len < size - 1)
        {
            buf[len++] = (char)key;
            buf[len] = '\0';
        }
        key = GetCharPressed();
    }

    if ((IsKeyPressed(KEY_BACKSPACE) || IsKeyPressedRepeat(KEY_BACKSPACE)) && len > 0) buf[len - 1] = '\0';

    return IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_KP_ENTER);
}

void UiTextInputDraw(Rectangle bounds, const char *buf, bool active)
{
    DrawRectangleRounded(bounds, 0.2f, 8, WHITE);
    DrawRectangleLinesEx(bounds, 2.0f, active ? UI_ACCENT : UI_MUTED);

    Vector2 size = MeasureTextEx(monoFont, buf, CODE_FONT_SIZE, 0);
    Vector2 pos = { bounds.x + 12, bounds.y + (bounds.height - size.y)/2.0f };
    if (buf[0] == '\0') size.y = (float)CODE_FONT_SIZE;
    pos.y = bounds.y + (bounds.height - (float)CODE_FONT_SIZE)/2.0f;

    DrawTextEx(monoFont, buf, pos, CODE_FONT_SIZE, 0, UI_TEXT);

    if (active && ((int)(GetTime()*2.0) % 2 == 0))
    {
        float cx = pos.x + size.x + 2;
        DrawRectangle((int)cx, (int)pos.y, 2, CODE_FONT_SIZE, UI_TEXT);
    }
}
