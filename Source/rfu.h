#pragma once

#define RFU_VERSION "4.4.4"
#define RFU_GITHUB_REPO "axstin/rbxfpsunlocker"

bool CheckForUpdates();
void RFU_SetFPSCap(double value);
void RFU_OnUIUnlockMethodChange();
void RFU_OnUIClose();
