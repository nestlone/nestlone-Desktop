#pragma once

#include <windows.h>

namespace nestlone {

// Creates the shared translucent "stacked desktop boxes" mark at the requested size.
// The returned icon is owned by the caller and must be destroyed with DestroyIcon.
HICON CreateNestloneIcon(int size);

}
