#pragma once
#include <windows.h>
#include "BoxModel.h"

namespace nestlone {
enum class CanvasCommand { Toggle, NewBox, NewNote, NewWeather, ClearWidgets, Reload, Exit };
bool CreateCanvas(HINSTANCE instance, HWND parent, Layout* layout);
void DestroyCanvas();
void HandleCanvasCommand(CanvasCommand command);
// Explorer recreates the desktop host after a shell restart.  Rebind without
// leaving the native ListView hidden while the replacement surface is absent.
void CanvasNotifyDesktopHostChanged();
bool CanvasVisible();
void CanvasSetOpacity(int opacity);
}
