#ifndef __amcam_labview_h__
#define __amcam_labview_h__

#include "extcode.h"

#ifdef AMCAM_LABVIEW_EXPORTS
#define AMCAM_LABVIEW_API(x) __declspec(dllexport)    x   __cdecl
#else
#define AMCAM_LABVIEW_API(x) __declspec(dllimport)    x   __cdecl
#include "amcam.h"
#endif

#ifdef __cplusplus
extern "C" {
#endif

AMCAM_LABVIEW_API(HRESULT) Start(HAmcam h, LVUserEventRef* rwer);

#ifdef __cplusplus
}
#endif

#endif