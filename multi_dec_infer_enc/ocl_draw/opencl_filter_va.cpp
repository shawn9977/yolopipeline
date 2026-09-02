/******************************************************************************\
Copyright (c) 2005-2020, Intel Corporation
All rights reserved.

Redistribution and use in source and binary forms, with or without modification, are permitted provided that the following conditions are met:

1. Redistributions of source code must retain the above copyright notice, this list of conditions and the following disclaimer.

2. Redistributions in binary form must reproduce the above copyright notice, this list of conditions and the following disclaimer in the documentation and/or other materials provided with the distribution.

3. Neither the name of the copyright holder nor the names of its contributors may be used to endorse or promote products derived from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

This sample was distributed or derived from the Intel's Media Samples package.
The original version of this sample may be obtained from https://software.intel.com/en-us/intel-media-server-studio
or https://software.intel.com/en-us/media-client-solutions-support.
\**********************************************************************************/

#include "opencl_filter_va.h"

#include <CL/cl_va_api_media_sharing_intel.h>

using std::endl;

DECL_CL_EXT_FUNC(clGetDeviceIDsFromVA_APIMediaAdapterINTEL);
DECL_CL_EXT_FUNC(clCreateFromVA_APIMediaSurfaceINTEL);
DECL_CL_EXT_FUNC(clEnqueueAcquireVA_APIMediaSurfacesINTEL);
DECL_CL_EXT_FUNC(clEnqueueReleaseVA_APIMediaSurfacesINTEL);

static const char kOclProcessSource[] = R"CLC(
#define LINE_THICK 4

// Chosen outline color (BT.601 approx for pure red)
const float Yc = 82 / 255.0f;
const float Uc = 90 / 255.0f;
const float Vc = 240 / 255.0f;

__kernel void drawboxY(__write_only image2d_t YOut, __global uint *rect, uint num)
{
    uint idx = get_global_id(1) * get_global_size(0)  + get_global_id(0);

    if (idx >= (num * 4))
        return;

    uint lineidx = idx % 4;
    const uint thick = LINE_THICK;

    idx = idx / 4;
    idx = idx * 4;
    uint x = rect[idx + 0];
    uint y = rect[idx + 1];
    uint w = rect[idx + 2];
    uint h = rect[idx + 3];
    uint lf, tp;

    switch (lineidx)
    {
        case 0:
                lf = x;
                tp = y;
                w = w + thick;
                h = thick;
                break;
        case 1:
                lf = x;
                tp = y  +  thick;
                w = thick;
                h = h;
                break;
        case 2:
                lf = x + w;
                tp = y;
                w =  thick;
                break;
        case 3:
        default:
                lf = x + thick;
                tp = y + h;
                h =  thick;
                break;
    }

    uint i = 0;
    uint j = 0;
    float4 pixel = (Yc);
    for (j = 0; j < h; j++)
    for (i = 0; i < w; i++)
    {
        int2 dst;
        dst.x = lf + i;
        dst.y = tp + j;
        write_imagef(YOut, dst, pixel);
    }
}


__kernel void drawboxUV(__write_only image2d_t YOut, __global uint *rect, uint num)
{
    uint idx = get_global_id(1) * get_global_size(0)  + get_global_id(0);
    if (idx >= (num * 4))
        return;

    uint lineidx = idx % 4;
    const uint thick = LINE_THICK;

    idx = idx / 4;
    idx = idx * 4;
    uint x = rect[idx + 0];
    uint y = rect[idx + 1];
    uint w = rect[idx + 2];
    uint h = rect[idx + 3];
    uint lf, tp;


    switch (lineidx)
    {
        case 0:
                lf = x;
                tp = y;
                w = w + thick;
                h = thick;
                break;
        case 1:
                lf = x;
                tp = y  +  thick;
                w = thick;
                h = h;
                break;
        case 2:
                lf = x + w;
                tp = y;
                w =  thick;
                break;
        case 3:
        default:
                lf = x + thick;
                tp = y + h;
                h =  thick;
                break;
    }

    uint i = 0;
    uint j = 0;
    w = w / 2;
    h = h / 2;
    lf = lf / 2;
    tp = tp / 2;
    float4 pixel = (float4)(Uc, Vc, Uc, Vc);
    for (j = 0; j < h; j++)
    for (i = 0; i < w; i++)
    {
        int2 dst;
        dst.x = lf + i;
        dst.y = tp + j;
        write_imagef(YOut, dst, pixel);
    }
}
)CLC";


OpenCLFilterVA::OpenCLFilterVA()
{
    m_requiredOclExtensions.push_back("cl_intel_va_api_media_sharing");

    m_vaDisplay = 0;
    m_rectNum = 0;

    for(size_t i = 0; i < c_shared_surfaces_num; i++)
    {
        m_SharedSurfaces[i] = VA_INVALID_ID;
    }

}

cl_int OpenCLFilterVA::OCLInit(void *device)
{
    if (!device)
    {
        std::cout<<"NULL pointer "<<__func__<<std::endl;
        return 0;
    }

    m_vaDisplay = (VADisplay*)device;

    cl_int error = AddKernel(kOclProcessSource, "drawboxY", "drawboxUV");
    if (error) {
        std::cout << __FILE__<<" line"<<__LINE__<< " AddKernel failed" << std::endl;
        return -1;
    }

    error = OpenCLFilterBase::OCLInit(device);
    if (error != CL_SUCCESS) {
        std::cout<<__FILE__<<" line"<<__LINE__<<" clCreateBuffer return error"<<error<<std::endl;
        return -1;
    }

    for (int i = 0; i < 2; i++)
    {

        m_clrect[i] = clCreateBuffer(m_clcontext,
                CL_MEM_READ_ONLY,
                sizeof(uint) * MAX_BOX_NUM * 4,  NULL,  
                &error);
        if (error != CL_SUCCESS) {
            std::cout<<__FILE__<<" line"<<__LINE__<<" clCreateBuffer return error"<<error<<std::endl;
            return -1;
        }
    }
    return 0;
}

cl_int OpenCLFilterVA::InitSurfaceSharingExtension()
{
    if ( !INIT_CL_EXT_FUNC(m_clplatform, clGetDeviceIDsFromVA_APIMediaAdapterINTEL)
      || !INIT_CL_EXT_FUNC(m_clplatform, clCreateFromVA_APIMediaSurfaceINTEL)
      || !INIT_CL_EXT_FUNC(m_clplatform, clEnqueueAcquireVA_APIMediaSurfacesINTEL)
      || !INIT_CL_EXT_FUNC(m_clplatform, clEnqueueReleaseVA_APIMediaSurfacesINTEL))
    {
        std::cout << "OpenCLFilter: Couldn't get all of the media sharing routines" << endl;
        return CL_INVALID_PLATFORM;
    }

    return CL_SUCCESS;
}

OpenCLFilterVA::~OpenCLFilterVA()
{
    for ( int i = 0; i < 2; i++)
    {
        if (m_clrect[i]) {
            clReleaseMemObject(m_clrect[i]);
            m_clrect[i] = nullptr;
        }
    }
}

cl_int OpenCLFilterVA::InitDevice()
{
    cl_int error = CL_SUCCESS;
    if (verbose) {
        std::cout << "OpenCLFilter: Try to init OCL device" << endl;
    }

    cl_uint nDevices = 0;
    error = lin_clGetDeviceIDsFromVA_APIMediaAdapterINTEL(m_clplatform, CL_VA_API_DISPLAY_INTEL,
                                        m_vaDisplay, CL_PREFERRED_DEVICES_FOR_VA_API_INTEL, 1, &m_cldevice, &nDevices);
    if(error) {
        std::cout << "OpenCLFilter: clGetDeviceIDsFromVA_APIMediaAdapterINTEL failed. Error code: " << error << endl;
        return error;
    }
    else if (verbose) {
        std::cout<< "OpenCLFilter: clGetDeviceIDsFromVA_APIMediaAdapterINTEL success!"<<endl;
    }

    if (!nDevices)
        return CL_INVALID_PLATFORM;


    // Initialize the shared context
    cl_context_properties props[] = { CL_CONTEXT_VA_API_DISPLAY_INTEL, (cl_context_properties) m_vaDisplay, CL_CONTEXT_INTEROP_USER_SYNC, 1, 0};
    m_clcontext = clCreateContext(props, 1, &m_cldevice, NULL, NULL, &error);

    if(error) {
        std::cout << "OpenCLFilter: clCreateContext failed. Error code: " << error << endl;
        return error;
    }

    if (verbose) {
        std::cout << "OpenCLFilter: OCL device inited" << endl;
    }

       return error;
}

cl_mem OpenCLFilterVA::CreateSharedSurface(VASurfaceID mid, int nView, bool bIsReadOnly)
{
    VASurfaceID *surf = (VASurfaceID *)(&mid);

    cl_int error = CL_SUCCESS;
    cl_mem mem = lin_clCreateFromVA_APIMediaSurfaceINTEL(m_clcontext, bIsReadOnly ? CL_MEM_READ_ONLY : CL_MEM_READ_WRITE,
                                            surf, nView, &error);
    if (error) {
        std::cout << "clCreateFromVA_APIMediaSurfaceINTEL failed. Error code: " << error << endl;
        return 0;
    }
    else if (verbose) {
        std::cout << "clCreateFromVA_APIMediaSurfaceINTEL success " << endl;
    }
    return mem;
}


bool OpenCLFilterVA::EnqueueAcquireSurfaces(cl_mem* surfaces, int nSurfaces)
{
    cl_int error = lin_clEnqueueAcquireVA_APIMediaSurfacesINTEL(m_clqueue, nSurfaces, surfaces, 0, NULL, NULL);
    if (error) {
        std::cout << "clEnqueueAcquireVA_APIMediaSurfacesINTEL failed. Error code: " << error << endl;
        return false;
    }
    return true ;
}

bool OpenCLFilterVA::EnqueueReleaseSurfaces(cl_mem* surfaces, int nSurfaces)
{
    cl_int error = lin_clEnqueueReleaseVA_APIMediaSurfacesINTEL(m_clqueue, nSurfaces, surfaces, 0, NULL, NULL);
    if (error) {
        std::cout << "clEnqueueReleaseVA_APIMediaSurfacesINTEL failed. Error code: " << error << endl;
        return false;
    }
    return true;
}

cl_int OpenCLFilterVA::SetKernelArgs()
{
    cl_int error = CL_SUCCESS;

    // set kernelY parameters
    error = clSetKernelArg(m_kernels[m_activeKernel].clkernelY, 0, sizeof(cl_mem), &m_clbuffer[0]);
    if(error) {
        std::cout << "clSetKernelArg failed. Error code: " << error << endl;
        return error;
    }
    
    error = clSetKernelArg(m_kernels[m_activeKernel].clkernelY, 1, sizeof(cl_mem), &m_clrect[0]);
    if(error) {
        std::cout << "clSetKernelArg failed. Error code: " << error << endl;
        return error;
    }

    error = clSetKernelArg(m_kernels[m_activeKernel].clkernelY, 2, sizeof(cl_uint), &m_rectNum);
    if(error) {
        std::cout << "clSetKernelArg failed. Error code: " << error << endl;
        return error;
    }


    // set kernelUV parameters
    error = clSetKernelArg(m_kernels[m_activeKernel].clkernelUV, 0, sizeof(cl_mem), &m_clbuffer[1]);
    if(error) {
        std::cout << "clSetKernelArg failed. Error code: " << error << endl;
        return error;
    }
    
    error = clSetKernelArg(m_kernels[m_activeKernel].clkernelUV, 1, sizeof(cl_mem), &m_clrect[1]);
    if(error) {
        std::cout << "clSetKernelArg failed. Error code: " << error << endl;
        return error;
    }


    error = clSetKernelArg(m_kernels[m_activeKernel].clkernelUV, 2, sizeof(cl_uint), &m_rectNum);
    if(error) {
        std::cout << "clSetKernelArg failed. Error code: " << error << endl;
        return error;
    }
    //std::cout << "clSetKernelArg success" << endl;

    return error;
}

int  OpenCLFilterVA::SetBoundingBox(std::vector<BoxRect> &boxes, bool &truncated)
{
    truncated = false;

    if (boxes.size() > MAX_BOX_NUM)
    {
        truncated = true;
    }

    m_rectNum = static_cast<cl_uint>(boxes.size());

    if (boxes.empty())
    {
        return 0;
    }

    cl_int error = CL_SUCCESS;
    for (int i = 0; i < 2; i++)
    {
        //one box position is specified by x, y, w, h. 
        error = clEnqueueWriteBuffer(
                m_clqueue,
                m_clrect[i],
                CL_FALSE,    // non-blocking
                0,
                sizeof(uint) * m_rectNum * 4,
                boxes.data(),
                0,
                NULL,
                NULL 
                );

        if (error != CL_SUCCESS) {
            std::cout<<__FILE__<<" line"<<__LINE__<<" clCreateBuffer return error"<<error<<std::endl;
            return -1;

        }
    }

    return 0;
}

