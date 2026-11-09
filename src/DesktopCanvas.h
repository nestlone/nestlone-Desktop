#pragma once
#include <windows.h>
#include "BoxModel.h"

namespace nestlone {
enum class CanvasCommand { Toggle, NewBox, ClearWidgets, RefreshLayout, Reload, Exit };
bool CreateCanvas(HINSTANCE instance, HWND parent, Layout* layout);
void DestroyCanvas();
void HandleCanvasCommand(CanvasCommand command);
// Explorer recreates the desktop host after a shell restart.  Rebind without
// leaving the native ListView hidden while the replacement surface is absent.
void CanvasNotifyDesktopHostChanged();
bool CanvasVisible();
bool CanvasCreateNote(HWND settings);
bool CanvasShowNote(const std::wstring& id);
void CanvasSetOpacity(int opacity);
}
