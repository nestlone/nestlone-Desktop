#pragma once

#include <windows.h>
#include "BoxModel.h"

namespace nestlone {

void ShowSettings(HINSTANCE instance, HWND owner, Layout* layout);
bool ProcessSettingsMessage(MSG& message);
void CloseSettings();

}
