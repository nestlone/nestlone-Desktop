#pragma once
#include <windows.h>
#include "BoxModel.h"

namespace nestlone {
enum class CanvasCommand { Toggle, NewBox, NewNote, NewWeather, ClearWidgets, Reload, Exit };
bool CreateCanvas(HINSTANCE instance, HWND parent, Layout* layout);
void DestroyCanvas();
void HandleCanvasCommand(CanvasCommand command);
bool CanvasVisible();
void CanvasSetOpacity(int opacity);
}
