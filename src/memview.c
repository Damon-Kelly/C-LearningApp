#include "memview.h"
#include "screens.h"
#include "codepanel.h"      // CODE_FONT_SIZE
#include "ui.h"

#include <string.h>

#define BOX_LEFT_OFFSET  180.0f     // room for the name/type/address label
#define BOX_WIDTH        100.0f

#define CELL_BORDER      ((Color){  72,  80,  92, 255 })
#define CELL_CHANGED     ((Color){ 255, 159,  28, 255 })
#define CELL_FILL        ((Color){  13,  17,  23, 255 })
#define CELL_PTR_FILL    ((Color){  18,  32,  52, 255 })

static int FindCell(const Step *step, const char *name)
{
    for (int i = 0; i < step->cellCount; i++) if (strcmp(step->cells[i].name, name) == 0) return i;
    return -1;
}

static bool IsPointerValue(const char *value)
{
    return value[0] == '-' && value[1] == '>';
}

static void DrawArrowHead(Vector2 tip, Color color)
{
    // Pointing left: two short strokes back from the tip
    DrawLineEx(tip, (Vector2){ tip.x + 10, tip.y - 6 }, 2.5f, color);
    DrawLineEx(tip, (Vector2){ tip.x + 10, tip.y + 6 }, 2.5f, color);
}

void DrawMemory(Rectangle area, const Step *step, const Step *prev)
{
    // Card with a thin border
    DrawRectangleRounded(area, 0.03f, 6, UI_BORDER);
    DrawRectangleRounded((Rectangle){ area.x + 1, area.y + 1, area.width - 2, area.height - 2 }, 0.03f, 6, UI_PANEL);
    UiText("Memory", (Vector2){ area.x + 12, area.y + 8 }, 16, UI_MUTED);

    if (step == NULL || step->cellCount == 0)
    {
        UiDrawTextCentered("Nothing in memory yet", area.x + area.width/2.0f, area.y + area.height/2.0f - 10, 18, UI_MUTED);
        return;
    }

    // Rows share the available height, capped so a few cells don't look stretched
    float pitch = (area.height - 48.0f)/(float)step->cellCount;
    if (pitch > 58.0f) pitch = 58.0f;
    float rowH = pitch - 12.0f;

    float boxX = area.x + BOX_LEFT_OFFSET;
    float top = area.y + 36.0f;

    // Boxes and labels
    for (int i = 0; i < step->cellCount; i++)
    {
        const Cell *cell = &step->cells[i];
        float y = top + i*pitch;
        Rectangle box = { boxX, y, BOX_WIDTH, rowH };

        bool changed = true;
        if (prev != NULL)
        {
            int before = FindCell(prev, cell->name);
            if (before >= 0 && strcmp(prev->cells[before].value, cell->value) == 0) changed = false;
        }

        bool isPointer = IsPointerValue(cell->value);
        DrawRectangleRec(box, isPointer ? CELL_PTR_FILL : CELL_FILL);
        DrawRectangleLinesEx(box, changed ? 3.0f : 2.0f, changed ? CELL_CHANGED : CELL_BORDER);

        // Value text: pointers show the address they hold
        const char *shown = cell->value;
        if (isPointer)
        {
            int target = FindCell(step, cell->value + 2);
            shown = (target >= 0) ? TextFormat("%d", step->cells[target].addr) : "?";
        }
        else if (strcmp(cell->value, "null") == 0) shown = "NULL";

        Vector2 valueSize = MeasureTextEx(monoFont, shown, CODE_FONT_SIZE, 0);
        DrawTextEx(monoFont, shown, (Vector2){ box.x + (box.width - valueSize.x)/2.0f, box.y + (box.height - valueSize.y)/2.0f }, CODE_FONT_SIZE, 0, UI_TEXT);

        // Label to the left: name, then type and address (side by side when rows are short)
        const char *detail = TextFormat("%s @%d", cell->type, cell->addr);
        if (rowH >= 38.0f)
        {
            DrawTextEx(monoFont, cell->name, (Vector2){ area.x + 14, y + 2 }, CODE_FONT_SIZE, 0, UI_TEXT);
            UiText(detail, (Vector2){ area.x + 14, y + rowH - 16 }, 14, UI_MUTED);
        }
        else
        {
            Vector2 nameSize = MeasureTextEx(monoFont, cell->name, CODE_FONT_SIZE, 0);
            DrawTextEx(monoFont, cell->name, (Vector2){ area.x + 14, y + (rowH - nameSize.y)/2.0f }, CODE_FONT_SIZE, 0, UI_TEXT);

            // Long names (such as n1.nextPtr) leave no room for the type: show just the address then
            float room = BOX_LEFT_OFFSET - 14.0f - nameSize.x - 14.0f;
            if (UiMeasureText(detail, 14).x > room) detail = TextFormat("@%d", cell->addr);
            UiText(detail, (Vector2){ area.x + 14 + nameSize.x + 8, y + (rowH - 14.0f)/2.0f }, 14, UI_MUTED);
        }
    }

    // Arrows from pointer cells to their targets, drawn on the right-hand side
    for (int i = 0; i < step->cellCount; i++)
    {
        const Cell *cell = &step->cells[i];
        if (!IsPointerValue(cell->value)) continue;

        int target = FindCell(step, cell->value + 2);
        if (target < 0 || target == i) continue;

        float xr = boxX + BOX_WIDTH;
        float y1 = top + i*pitch + rowH/2.0f;
        float y2 = top + target*pitch + rowH/2.0f;
        float bend = xr + 26.0f + 14.0f*(float)(i % 4);

        DrawLineEx((Vector2){ xr, y1 }, (Vector2){ bend, y1 }, 2.5f, UI_ACCENT);
        DrawLineEx((Vector2){ bend, y1 }, (Vector2){ bend, y2 }, 2.5f, UI_ACCENT);
        DrawLineEx((Vector2){ bend, y2 }, (Vector2){ xr + 3, y2 }, 2.5f, UI_ACCENT);
        DrawArrowHead((Vector2){ xr + 3, y2 }, UI_ACCENT);
    }
}
