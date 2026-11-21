#ifndef __imagepro_amcam_H__
#define __imagepro_amcam_H__

#if defined(_WIN32)
#pragma pack(push, 8)
#endif

#ifdef __cplusplus
extern "C"
{
#endif

IMAGEPRO_API(HRESULT) imagepro_stitch_pull(HImageproStitch handle, HAmcam h, int bFeed, void* pImageData, int bits, int rowPitch, AmcamFrameInfoV2* pInfo);
IMAGEPRO_API(HRESULT) imagepro_stitch_pullV3(HImageproStitch handle, HAmcam h, int bFeed, void* pImageData, int bits, int rowPitch, AmcamFrameInfoV3* pInfo);
IMAGEPRO_API(HRESULT) imagepro_stitch_pullV4(HImageproStitch handle, HAmcam h, int bFeed, void* pImageData, int bits, int rowPitch, AmcamFrameInfoV4* pInfo);

IMAGEPRO_API(HRESULT) imagepro_edf_pull(HImageproEdf handle, HAmcam h, int bFeed, void* pImageData, int bits, int rowPitch, AmcamFrameInfoV2* pInfo);
IMAGEPRO_API(HRESULT) imagepro_edf_pullV3(HImageproEdf handle, HAmcam h, int bFeed, void* pImageData, int bits, int rowPitch, AmcamFrameInfoV3* pInfo);
IMAGEPRO_API(HRESULT) imagepro_edf_pullV4(HImageproEdf handle, HAmcam h, int bFeed, void* pImageData, int bits, int rowPitch, AmcamFrameInfoV4* pInfo);

#ifdef __cplusplus
}
#endif

#if defined(_WIN32)
#pragma pack(pop)
#endif
#endif
