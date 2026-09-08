"""Shared Xbox XDK lifting/HLE selection policy; no game addresses."""

# XAPI helpers that are pure guest-side bookkeeping (TLS/KTHREAD fields, object-attribute
# formatting): more faithful to recompile them than to reimplement them.
DEFAULT_LIFT = {"SetLastError", "GetLastError", "XapiSetLastNTError", "XapiFormatObjectAttributes",
                "XapiMapLetterToDirectory", "XapiSelectCachePartition", "XGetSectionSize", "GetTimeZoneInformation",
                "RaiseException", "UnhandledExceptionFilter", "XAutoPowerDownResetTimer", "XRegisterThreadNotifyRoutine",
                # the real XAPI start-up chain and thread/sync wrappers: they only need the kernel underneath
                "mainCRTStartup", "mainXapiStartup", "XapiInitProcess", "XapiBootToDash", "CreateThread", "CreateEventA",
                "CreateMutexA", "SetEvent", "ResetEvent", "SwitchToThread", "SetThreadPriority", "GetExitCodeThread",
                "XapiThreadStartup", "MU_Init", "XapiInitDefaultHeap", "XapiHeapAlloc"}
# Original XAPI signing can be enabled explicitly with --lift
# XCalculateSignatureBegin XCalculateSignatureUpdate XCalculateSignatureEnd.
# Keep it experimental until legacy unsigned profiles and checkpoint restore work.


# Libraries that are replaced wholesale by the Vita runtime, and the XAPI entry points that talk to
# hardware (USB input, launch data, debug output) and therefore stay HLE even though XAPILIB is lifted.
HLE_LIBS = {"D3D8", "D3DX", "DSOUND", "XNETS", "XONLINE", "XGRAPHC", "XACTENG"}
HLE_KEEP = {"XGetLaunchInfo", "XInitDevices", "XGetDeviceChanges", "XInputOpen", "XInputClose", "XInputGetState",
            "XInputSetState", "XInputPoll", "XLaunchNewImageA", "OutputDebugStringA", "XCalculateSignatureBegin",
            "XCalculateSignatureUpdate", "XCalculateSignatureEnd", "MU_Init", "XMountMUA", "XUnmountMU", "XMountUtilityDrive",
            "XSetProcessQuantumLength", "XGetDevices"}

