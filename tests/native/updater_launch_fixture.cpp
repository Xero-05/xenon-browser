#include <windows.h>
#include <fstream>

// Unshipped, synthetic process used to prove that prepared setup stays suspended
// until the caller releases its running marker. It installs nothing.
int WINAPI wWinMain(HINSTANCE,HINSTANCE,LPWSTR,int){
  const auto marker=OpenMutexW(SYNCHRONIZE,FALSE,L"Local\\XenonUpdateFixtureRunning");
  const auto error=GetLastError();
  const bool absent=!marker&&error==ERROR_FILE_NOT_FOUND;
  if(marker)CloseHandle(marker);
  std::ofstream output("launch-result.txt",std::ios::binary);
  output<<(absent?"running-marker-absent":"running-marker-present-or-inaccessible");
  return output.good()?0:1;
}
