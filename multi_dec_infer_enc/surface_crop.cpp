#include <iostream>
#include <fstream>
#include <string.h>
#include "surface_crop.h"

using namespace std;

#define CHECK_VASTATUS(va_status,func)                                      \
  if (va_status != VA_STATUS_SUCCESS) {                                     \
      cout<<"error "<< __func__<<" line:"<< __LINE__<<" error code "<<va_status <<endl;                 \
      return -1;                                                            \
  }


SurfaceCrop::SurfaceCrop() {
    
}

SurfaceCrop::~SurfaceCrop() {
    for (auto &surf_id : mSurfs) {
        vaDestroySurfaces(mVADisp, &surf_id, 1);
        surf_id = -1;
    }

    vaDestroyConfig(mVADisp, mConfigId);
    vaDestroyContext(mVADisp, mCtxId);
}

int SurfaceCrop::Init(void *vaDisp) {
    VAStatus va_status;
    mVADisp = (VADisplay)vaDisp;

     /* Check whether VPP is supported by driver */
    VAEntrypoint entrypoints[5];
    int32_t num_entrypoints;
    num_entrypoints = vaMaxNumEntrypoints(mVADisp);
    va_status = vaQueryConfigEntrypoints(mVADisp,
                                         VAProfileNone,
                                         entrypoints,
                                         &num_entrypoints);
    CHECK_VASTATUS(va_status, "vaQueryConfigEntrypoints");

    int j = 0;
    for (j = 0; j < num_entrypoints; j++) {
        if (entrypoints[j] == VAEntrypointVideoProc)
            break;
    }

    if (j == num_entrypoints) {
        cout<<"VPP is not supported by driver"<<endl;
        return -1;
    }
 

    VAContextID context_id = 0;
    VAConfigID  config_id = 0;
    VAConfigAttrib attrib;
    attrib.type = VAConfigAttribRTFormat;
    attrib.value = VA_RT_FORMAT_YUV420;

    va_status = vaCreateConfig(vaDisp,
                               VAProfileNone,
                               VAEntrypointVideoProc,
                               &attrib,
                               1,
                               &config_id);
    CHECK_VASTATUS(va_status, "vaCreateConfig");

    va_status = vaCreateContext(vaDisp,
                                config_id,
                                1920,
                                1080,
                                VA_PROGRESSIVE,
                                0,
                                0,
                                &context_id);
    CHECK_VASTATUS(va_status, "vaCreateContext");

    mCtxId = context_id;
    mConfigId = config_id;
    return 0;
}

int SurfaceCrop::CreateOutputs(int w, int h, int num) {
    if (!mVADisp) {
        std::cout<<__FILE__<<" "<<__func__<<" mVADisp is null.  Call init firstly!"<<std::endl;
        return -1;
    }
    VAStatus va_status;
    VASurfaceAttrib    surface_attrib;
    surface_attrib.type =  VASurfaceAttribPixelFormat;
    surface_attrib.flags = VA_SURFACE_ATTRIB_SETTABLE;
    surface_attrib.value.type = VAGenericValueTypeInteger;
    surface_attrib.value.value.i = VA_FOURCC_NV12;

    VASurfaceID *surfaces = ( VASurfaceID *) malloc(sizeof(VASurfaceID) * num);
    va_status = vaCreateSurfaces(mVADisp,
            VA_RT_FORMAT_YUV420,
            w,
            h,
            surfaces,
            num,
            &surface_attrib,
            1);
    CHECK_VASTATUS(va_status, "vaCreateSurfaces");
    for (int i = 0; i < num; i++)
    {
        mSurfs.push_back(surfaces[i]);
    }
    free(surfaces);
    return 0;
}


int SurfaceCrop::SetCrop(vector<VARectangle> &inRegion, vector<VARectangle> &outRegion) {
    mIn.clear();
    mOut.clear();
    if (inRegion.size() != outRegion.size()) {
        cout<<__func__<<" Input region number isn't equal to the number of output regions"<<endl;
    }
    //cout<<"Set crop with "<<inRegion.size()<<" regions"<<endl;
    for (auto &r : inRegion)
        mIn.push_back(r);

    for (auto &r : outRegion)
        mOut.push_back(r);

   return 0;
}

int SurfaceCrop::ProcessSurface(VASurfaceID surf, vector<VASurfaceID> &outSurf) {

    VAStatus va_status;
    int cropnum = mIn.size();
    for (int i = 0; i < cropnum; i++) {
        VAProcPipelineParameterBuffer pipeline_param = {0};
        VARectangle surface_region, output_region;
        VABufferID pipeline_param_buf_id = VA_INVALID_ID;
        /* Fill pipeline buffer */
        pipeline_param.surface = surf; 

        surface_region = mIn[i];
        output_region = mOut[i];
        pipeline_param.surface_region = &surface_region;
        pipeline_param.output_region = &output_region;
        pipeline_param.output_background_color =0xff808080;
        va_status = vaCreateBuffer(mVADisp,
                mCtxId,
                VAProcPipelineParameterBufferType,
                sizeof(pipeline_param),
                1,
                &pipeline_param,
                &pipeline_param_buf_id);
        CHECK_VASTATUS(va_status, "vaCreateBuffer");

        va_status = vaBeginPicture(mVADisp,
                mCtxId,
                mSurfs[i]);
        CHECK_VASTATUS(va_status, "vaBeginPicture");

        va_status = vaRenderPicture(mVADisp,
                mCtxId,
                &pipeline_param_buf_id,
                1);
        CHECK_VASTATUS(va_status, "vaRenderPicture");

        va_status = vaEndPicture(mVADisp, mCtxId);
        CHECK_VASTATUS(va_status, "vaEndPicture");

        if (pipeline_param_buf_id != VA_INVALID_ID) {
            vaDestroyBuffer(mVADisp, pipeline_param_buf_id);
            CHECK_VASTATUS(va_status, "vaDestroyBuffer");
        }
    }

    for (int i = 0; i < cropnum; i++) {
        va_status = vaSyncSurface(mVADisp, mSurfs[i]);
        CHECK_VASTATUS(va_status, "vaSyncSurface");
        outSurf.push_back(mSurfs[i]);
    }
    return 0;
}

