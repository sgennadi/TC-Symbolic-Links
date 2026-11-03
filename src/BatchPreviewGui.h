#pragma once
#include "LinkBatchPlan.h"
#include <windows.h>
#include <vector>
namespace tcs::batch {
bool ConfirmPreview(HWND owner, const std::vector<Row>& rows, bool forCreation);
}
