#include <windows.h>

#pragma comment(linker, "/export:GetFileVersionInfoSizeA=version_real.GetFileVersionInfoSizeA")
#pragma comment(linker, "/export:GetFileVersionInfoA=version_real.GetFileVersionInfoA")
#pragma comment(linker, "/export:VerQueryValueA=version_real.VerQueryValueA")
#pragma comment(linker, "/export:GetFileVersionInfoSizeW=version_real.GetFileVersionInfoSizeW")
#pragma comment(linker, "/export:GetFileVersionInfoW=version_real.GetFileVersionInfoW")
#pragma comment(linker, "/export:VerQueryValueW=version_real.VerQueryValueW")
#pragma comment(linker, "/export:GetFileVersionInfoExW=version_real.GetFileVersionInfoExW")
#pragma comment(linker, "/export:GetFileVersionInfoSizeExW=version_real.GetFileVersionInfoSizeExW")
