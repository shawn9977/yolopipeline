#pragma once

#include <va/va.h>
#include <vector>
#include <string>

class SurfaceCrop {
public:
    SurfaceCrop();
    virtual ~SurfaceCrop();

    int Init(void *);

    int CreateOutputs(int w, int h, int num);
    int SetCrop(std::vector<VARectangle> &inRegion, std::vector<VARectangle> &outRegion);
    int ProcessSurface(VASurfaceID surf, std::vector<VASurfaceID> &outSurf);
    int GetOutputSurfaceNumber() {return mSurfs.size();};
private:

    VADisplay mVADisp = nullptr;
    std::vector<VARectangle> mIn, mOut;
    std::vector<VASurfaceID> mSurfs;
    uint mOutNum = 0;
    VAContextID mCtxId;

    VAConfigID  mConfigId = 0;
};
