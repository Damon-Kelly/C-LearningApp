/**********************************************************************************************
*
*   C Learning App - memory picture for trace pages
*
*   Draws each variable as a labelled box (name, type, address, value). Pointers get an
*   arrow to the cell they point at. Cells that are new or changed since the previous
*   step are outlined in orange.
*
**********************************************************************************************/

#ifndef MEMVIEW_H
#define MEMVIEW_H

#include "raylib.h"
#include "content.h"

// step may be NULL (nothing in memory yet). prev may be NULL (no earlier step).
void DrawMemory(Rectangle area, const Step *step, const Step *prev);

#endif // MEMVIEW_H
