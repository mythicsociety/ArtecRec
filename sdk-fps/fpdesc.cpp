// fpdesc — print the connected scanner's default real-time frame-processing settings (FrameProcessorDesc):
// object size filter, triangle step, edge-length threshold, hole interpolation and its max length, max angle.
#include <artec/sdk/base/Log.h>
#include <artec/sdk/capturing/IScanner.h>
#include <artec/sdk/capturing/IArrayScannerId.h>
#include <windows.h>
#include <cstdio>
using namespace artec::sdk::base; using namespace artec::sdk::capturing;
int wmain() {
  setOutputLevel(VerboseLevel_Error); IArrayScannerId* list = nullptr; ErrorCode ec = enumerateScanners(&list); if (ec != ErrorCode_OK || !list || list->getSize() < 1) { wprintf(L"no scanner (0x%08X)\n", (unsigned)ec); return 1; }
  IScanner* sc = nullptr; ec = createScanner(&sc, &list->getPointer()[0]); if (ec != ErrorCode_OK || !sc) { wprintf(L"createScanner 0x%08X (in use by another program?)\n", (unsigned)ec); return 1; }
  FrameProcessorDesc d; ec = sc->initFrameProcessorDesc(&d);
  wprintf(L"default FrameProcessorDesc (0x%08X): minimumObjectSize %d, trianglesStep %d, edgeLengthThreshold %.2f mm, interpolate %d, maxInterpolatedLength %.2f mm, maxAngle %.1f deg\n", (unsigned)ec, d.minimumObjectSize, d.trianglesStep, d.edgeLengthThreshold, (int)d.interpolate, d.maxInterpolatedLength, d.maxAngle);
  sc->release(); list->release(); return 0;
}
