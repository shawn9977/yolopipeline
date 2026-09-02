
#include <va/va_drm.h>
#include <fcntl.h>
#include <unistd.h>
#include <fstream>
#include <sstream>
#include <iomanip>

#include <vpl/mfxstructures.h>
#include "vpldecenc.h"

//#define MEASURE_TIME
#define CHECK_DEC_ID(id) \
    if (decoders_.find(id) == decoders_.end()) { \
        std::cout<<"error "<<__func__<<" line"<<__LINE__<<" Decoder id "<<id<<" does not exist!"<<std::endl;\
        return -1;\
    }
#define CHECK_ENC_ID(id) \
    if (encoders_.find(id) == encoders_.end()) { \
        std::cout<<"error "<<__func__<<" line"<<__LINE__<<" Encoder id "<<id<<" does not exist!"<<std::endl;\
        return -1;\
    }

#ifndef ALIGN16
#define ALIGN16(SZ) (((SZ + 15) >> 4) << 4) // round up to a multiple of 16
#endif

// Destructor
VplDecEnc::~VplDecEnc() = default;

int VplDecEnc::InitDevice(mfxU32 nAdapterNum,  mfxHDL &handle) {
    std::string renderPath = "/dev/dri/renderD";
    renderPath += std::to_string(128 + nAdapterNum);

    std::cout<<"Open GPU device "<<renderPath<<std::endl;
    vaDRMfd = open(renderPath.c_str(), O_RDWR);
    if (vaDRMfd < 0)
        return MFX_ERR_DEVICE_FAILED;

    mfxHDL vaDisplay = vaGetDisplayDRM(vaDRMfd);
    if (!vaDisplay)
        return MFX_ERR_DEVICE_FAILED;

    int va_ver_major = 0, va_ver_minor = 0;
    VAStatus vaSts = vaInitialize(vaDisplay, &va_ver_major, &va_ver_minor);
    if (vaSts != VA_STATUS_SUCCESS)
        return MFX_ERR_DEVICE_FAILED;

    handle     = (mfxHDL)vaDisplay;

    return MFX_ERR_NONE;
}

// Initialize/Deinitialize
int VplDecEnc::Init(int deviceID) {

    if (getenv("VERBOSE")) {
        verbose = true;
    }
    if (getenv("LOOP")) {
        loop_count = atoi(getenv("LOOP"));
    }

    if (getenv("NO_YOLO")) {
        disableYolo = true;
    }
    mfxStatus sts                   = MFX_ERR_NONE;
    loader = MFXLoad();

    sts = (mfxStatus)InitDevice(deviceID, rootVaDpy);
    if (sts != MFX_ERR_NONE) {
        std::cout<<__func__<<" InitDevice return error" <<std::endl;
        return -1;
    }
    mfxConfig cfg[1];
    mfxVariant cfgVal[1];
        // Implementation used must be the type requested from command line
    cfg[0] = MFXCreateConfig(loader);
    if (!cfg[0]) {
        std::cout<<"__func__"<<"MFXCreateConfig failed"<<std::endl;
        return -1;
    }
    cfgVal[0].Type     = MFX_VARIANT_TYPE_U32;
    cfgVal[0].Data.U32 = MFX_IMPL_TYPE_HARDWARE;
    sts = MFXSetConfigFilterProperty(cfg[0], (mfxU8 *)"mfxImplDescription.Impl", cfgVal[0]);
    if (sts != MFX_ERR_NONE) {
        std::cout<<"__func__"<<" MFXSetConfigFilterProperty retunr error "<<sts<<std::endl;
    }
    return sts;
}

void VplDecEnc::Deinit() {
    // empty
    if (loader) {
         MFXUnload(loader);
         loader = nullptr;
    }

}

int VplDecEnc::CreateInferenceModel(const char* yoloIrFilename, const char* fastreidIrFilename) {
    
    if (getenv("FRAME_SKIP")) {
        frame_skip = atoi(getenv("FRAME_SKIP"));
    }
    std::cout<<"Inference skip frame number is "<<frame_skip<<std::endl;

    if (disableYolo) {
        std::cout<<" Yolo inference is disabled "<<std::endl;
        return 0;
    }

    yoloWrapper.SetVaDisplay(rootVaDpy);
    yoloWrapper.CreateYoloModel(yoloIrFilename);
    if (fastreidWrapper.enableFastreid) {
        fastreidWrapper.CreateFastReIDModel(fastreidIrFilename);
    fastreidWrapper.SetVaDisplay(rootVaDpy);
    }

    return 0;
}
// Read encoded stream from file
mfxStatus VplDecEnc::ReadFileStream(int id, mfxBitstream &bs, FILE *f) {
    mfxU8 *p0 = bs.Data;
    mfxU8 *p1 = bs.Data + bs.DataOffset;
    if (bs.DataLength + bs.DataOffset > bs.MaxLength) {
        return MFX_ERR_NOT_ENOUGH_BUFFER;
    }
    for (mfxU32 i = 0; i < bs.DataLength; i++) {
        *(p0++) = *(p1++);
    }
    bs.DataOffset = 0;
    auto readsize = (mfxU32)fread(bs.Data + bs.DataLength, 1, 500000, f);
    bs.DataLength += readsize;
    //bs.DataLength += (mfxU32)fread(bs.Data + bs.DataLength, 1, bs.MaxLength - bs.DataLength, f);
    if (readsize < 500000 && loop_count-- > 0) {
        fseek(f, 0, SEEK_SET);
        std::cout<<"Pipeline "<<id<<". Reach end of input file, reset to beginning"<<std::endl;
    }
    if (bs.DataLength == 0) {
        std::cout<<"pipeline "<<id<<" reach end of video stream file"<<std::endl;
        return MFX_ERR_MORE_DATA;
    }

    return MFX_ERR_NONE;
}

// Read encoded stream from file
mfxStatus VplDecEnc::ReadRTSPStream(int id, mfxBitstream &bs, VplDecoder &decoder) {
    mfxU8 *p0 = bs.Data;
    mfxU8 *p1 = bs.Data + bs.DataOffset;
    if (bs.DataLength + bs.DataOffset > bs.MaxLength) {
        return MFX_ERR_NOT_ENOUGH_BUFFER;
    }
    for (mfxU32 i = 0; i < bs.DataLength; i++) {
        *(p0++) = *(p1++);
    }
    bs.DataOffset = 0;
 
    auto ret = decoder.rtspReader->Read((char *)bs.Data + bs.DataLength, bs.MaxLength - bs.DataLength);
    if (ret > 0) {
        bs.DataLength += (mfxU32)ret;
        if (verbose) {
            std::cout<<"pipeline "<<id<<" rtsp read "<<ret <<"bytes"<<std::endl;
        }
        return MFX_ERR_NONE;
    } else {
        std::cout<<"Read RTSP stream failed "<<ret<<std::endl;
        return MFX_ERR_MORE_DATA;
    }

}

mfxStatus VplDecEnc::ReadVideoStream(int id, mfxBitstream &bs, VplDecoder &decoder) {
    if (decoder.isRtsp) {
        return ReadRTSPStream(id, bs, decoder);
    }
    else {
        return ReadFileStream(id, bs, decoder.inputfile);
    }
}



// Register components
int VplDecEnc::AddDecoder(int id, int codec) {
    if (decoders_.find(id) != decoders_.end()) {
        std::cout<<"error "<<__func__<<" line"<<__LINE__<<" decode id "<<id<<" alreay exist!"<<std::endl;
        return -1;
    }

    if (codec != MFX_CODEC_HEVC && codec != MFX_CODEC_AVC) {
        std::cout<<"error "<<__func__<<" line"<<__LINE__<<" invalid codec id"<<std::endl;
    }
    decoders_[id] = VplDecoder{};
    auto &decoder = decoders_[id];
    decoder.codec = codec;
    mfxStatus sts                   = MFX_ERR_NONE;
    sts = MFXCreateSession(loader, 0, &decoder.session);
    CHECK_STS(sts,
            "Cannot create session -- no implementations meet selection criteria");
    decoder.bitstream.MaxLength = BITSTREAM_BUFFER_SIZE;
    decoder.bitstream.Data      = (mfxU8 *)calloc(decoder.bitstream.MaxLength, sizeof(mfxU8));

    decoder.bitstream.CodecId = codec;

    sts = MFXVideoCORE_SetHandle(decoder.session, MFX_HANDLE_VA_DISPLAY, rootVaDpy);
    CHECK_STS(sts,
        "MFXVideoCORE_SetHandle error");
    int res = 0;
    if (!disableYolo) {
        res = yoloWrapper.CreateYoloInferRequest(id);

        if (res != 0) {
            std::cout<<"error "<<__func__<<" line"<<__LINE__<<" invalid codec id"<<std::endl;
        }
    }

    if (fastreidWrapper.enableFastreid) {
        res = fastreidWrapper.CreateFastReIDInferRequest(id);
        if (res != 0) {
            std::cout<<"error "<<__func__<<" line"<<__LINE__<<" invalid codec id"<<std::endl;
        }
    }

    decoder.needCrop = false;
    if (fastreidWrapper.enableFastreid) {
        decoder.needCrop = true;
    }
    else {
        decoder.needCrop = false;
        std::cout<<"disable crop"<<std::endl;
    }
    if (decoder.needCrop) {
        int r = decoder.fastreidPreProcessor.Init(rootVaDpy);
        CHECK_STS(r, "fastreidPreProcessor.Init error sts ");
        r = decoder.fastreidPreProcessor.CreateOutputs(FASTREID_INPUT_WIDTH, FASTREID_INPUT_HEIGHT, 5);
        CHECK_STS(r, " fastreidPreProcessor.CreateOutputs failed ");
    }
    return 0;
}

//for debug
int VplDecEnc::saveOutSurface(VADisplay dpy, VASurfaceID surf, int framenum) {
    VAImage img {};
    auto st = vaDeriveImage(dpy, surf, &img);
    if (st != VA_STATUS_SUCCESS) {
        vaDestroySurfaces(dpy, &surf, 1);
        printf("%s vaDeriveImage return error %d\n", __func__, st);
        return st;
    }

    void* mapped = nullptr;
    st = vaMapBuffer(dpy, img.buf, &mapped);
    if (st != VA_STATUS_SUCCESS) {
        vaDestroyImage(dpy, img.image_id);
        vaDestroySurfaces(dpy, &surf, 1);
        printf("%s vaMapImage return error %d\n", __func__, st);
        return st;
    }

    // 3) Locate planes
    // img.offsets[0], img.pitches[0] -> Y plane
    // img.offsets[1], img.pitches[1] -> interleaved UV plane (NV12)
    uint8_t* base = static_cast<uint8_t*>(mapped);
    uint8_t* Y    = base + img.offsets[0];
    uint8_t* UV   = base + img.offsets[1];

    const uint32_t pitchY  = img.pitches[0];
    const uint32_t pitchUV = img.pitches[1];
    const uint32_t width   = img.width;   // should be 1920

    const uint32_t height  = img.height;  // should be 1080

    char filename[64] = "";
    snprintf(filename,sizeof(filename), "/tmp/out%d_%dx%d.nv12", framenum, pitchY, height);
    FILE *fileout = fopen(filename, "wb+");
    if (fileout) {
        fwrite(Y, pitchY * height * 3 /2, 1, fileout);
        fclose(fileout);

    }
    else {
        printf("open /tmp/out.nv12 failed \n");
    }

       // 5) Unmap & release the VAImage wrapper
    st = vaUnmapBuffer(dpy, img.buf);
    if (st != VA_STATUS_SUCCESS) {
        vaDestroyImage(dpy, img.image_id);
        printf(" %s vaDestroyImage error %d\n", __func__, st);
        return st;
    }
    st = vaDestroyImage(dpy, img.image_id);
    if (st != VA_STATUS_SUCCESS) {
        printf(" %s vaDestroyImage error %d\n", __func__, st);
    }
    return st;


}


int VplDecEnc::DecodeScaleOneFrame(int id, VplSurface &surface, bool skipScaling) {

    CHECK_DEC_ID(id);
    auto &decoder = decoders_[id];

    mfxSurfaceArray *outSurfaces          = nullptr;
    auto &decodeParams = decoder.decodeParams;
    auto &session = decoder.session;
    auto &bitstream = decoder.bitstream;
    mfxStatus sts  = MFX_ERR_NONE;
    mfxU32 skipChannels[1];
    mfxU32 skipNum;
    if (!decoder.started) {
        sts = ReadVideoStream(id, bitstream, decoder);

        CHECK_STS(sts,  "Error reading bitstream");

        decodeParams.mfx.CodecId = decoder.codec;
        decodeParams.IOPattern   = MFX_IOPATTERN_OUT_VIDEO_MEMORY;
        decodeParams.AsyncDepth = 2;
        sts                      = MFXVideoDECODE_DecodeHeader(session, &bitstream, &decodeParams);
        CHECK_STS(sts,  "Error decoding header");

        LetterboxParams &params = decoder.letterboxParams;
        int src_width =  decodeParams.mfx.FrameInfo.CropW;
        int src_height = decodeParams.mfx.FrameInfo.CropH;
        float scale_w = (float)INPUT_WIDTH / src_width;
        float scale_h = (float)INPUT_HEIGHT / src_height;
        params.scale = std::min(scale_w, scale_h);

        params.origin_width = src_width;
        params.origin_height = src_height;
        params.new_width = (int)(src_width * params.scale);
        params.new_height = (int)(src_height * params.scale);
        params.pad_x = (INPUT_WIDTH - params.new_width) / 2;
        params.pad_y = (INPUT_HEIGHT - params.new_height) / 2;

        ppParam[0] = mfxVideoChannelParam{};
        ppParam[0].VPP.FourCC        = decodeParams.mfx.FrameInfo.FourCC;
        ppParam[0].VPP.ChromaFormat  = MFX_CHROMAFORMAT_YUV420;
        ppParam[0].VPP.PicStruct     = MFX_PICSTRUCT_PROGRESSIVE;
        ppParam[0].VPP.FrameRateExtN = decodeParams.mfx.FrameInfo.FrameRateExtN;
        ppParam[0].VPP.FrameRateExtD = decodeParams.mfx.FrameInfo.FrameRateExtD;
        ppParam[0].VPP.CropX         = params.pad_x;
        ppParam[0].VPP.CropY         = params.pad_y;
        ppParam[0].VPP.CropW         = params.new_width;
        ppParam[0].VPP.CropH         = params.new_height;//INPUT_HEIGHT;
        ppParam[0].VPP.Width         = ALIGN16(INPUT_WIDTH);
        ppParam[0].VPP.Height        = ALIGN16(INPUT_HEIGHT);
        ppParam[0].VPP.ChannelId     = 1;
        ppParam[0].Protected         = 0;
        ppParam[0].IOPattern = MFX_IOPATTERN_IN_VIDEO_MEMORY | MFX_IOPATTERN_OUT_VIDEO_MEMORY;
        ppParam[0].ExtParam  = NULL;
        mfxVideoChannelParam *ppParamP = &ppParam[0];

        decoder.width = decodeParams.mfx.FrameInfo.Width;
        decoder.height = decodeParams.mfx.FrameInfo.Height;

        // input parameters finished, now initialize decode
        sts = MFXVideoDECODE_VPP_Init(session, &decodeParams, &ppParamP, 1);
        CHECK_STS(sts, "Error initializing decode");

        std::cout<<"pipeline "<< id << " Decoder and scaler"<<id<<" init success.";
        std::cout<<"scaler out ("<<params.pad_x<<","<<params.pad_y<<","<<params.new_width;
        std::cout<<","<<params.new_height<<") "<<std::endl;
        decoder.started = true;
        decoder.isStillGoing = true;
    }

    while (decoder.isStillGoing == true) {

        mfxU32* skip = nullptr;
        if (skipScaling) {
            skipChannels[0] = 1;
            skip = &skipChannels[0];
            skipNum = 1;
        }
        else {
            skipNum = 0;
        }
        auto sts = MFXVideoDECODE_VPP_DecodeFrameAsync(session,
                                              (decoder.isDraining) ? NULL : &bitstream,
                                              skip,
                                              skipNum,
                                              &outSurfaces);

        switch (sts) {
            case MFX_ERR_NONE:
             if (outSurfaces == nullptr) {
                    printf("ERROR - empty array of surfaces.\n");
                    decoder.isStillGoing = false;
                    continue;
                }

                for (mfxU32 i = 0; i < outSurfaces->NumSurfaces; i++) {
                    auto aSurf = outSurfaces->Surfaces[i];
                    do {
                        sts = aSurf->FrameInterface->Synchronize(aSurf, WAIT_100_MILLISECONDS);
                        if (!(MFX_ERR_NONE == sts || MFX_WRN_IN_EXECUTION == sts)) {
                            std::cout<<"pipeline "<<id<<" "<<__func__<<"ERROR - FrameInterface->Synchronizee failed";
                        }
                        if (sts == MFX_ERR_NONE) {
                            if (aSurf->Info.ChannelId == 0) { // decoder output
                                surface.surface = aSurf;
                                surface.framenum = decoder.framenum;
                                surface.surfArray = outSurfaces; //save the handle for future release
                            }
                            else { // VPP filter output
                                surface.smallSurface = aSurf;
                                surface.framenum = decoder.framenum;
                            }
                        }

                    } while (sts == MFX_WRN_IN_EXECUTION);
                }
                return 0;

                break;
            case MFX_ERR_MORE_DATA:
                // The function requires more bitstream at input before decoding can
                // proceed
                if (decoder.isDraining)
                    decoder.isStillGoing = false;
                sts = ReadVideoStream(id, bitstream, decoder);
                if (sts != MFX_ERR_NONE) {
                    decoder.isDraining = true;
                }

                break;
            case MFX_WRN_VIDEO_PARAM_CHANGED:
                //if the resolution doesn't change, just ignore the warning.
                std::cout<<"decoder "<<id<<" warning "<< sts <<std::endl;
                sts = ReadVideoStream(id, bitstream, decoder);
                if (sts != MFX_ERR_NONE) {
                    decoder.isDraining = true;
                }
                break;
 
            default:
                std::cout<<"decoder "<<id<<" unknown status "<< sts <<std::endl;
                decoder.isStillGoing = false;
                break;
        }
    }

    return 0;
}
int VplDecEnc::DecodeOneFrame(int id, VplSurface &surface) {

    CHECK_DEC_ID(id);
    auto &decoder = decoders_[id];

    mfxFrameSurface1 *decSurfaceOut = NULL;
    auto &decodeParams = decoder.decodeParams;
    auto &session = decoder.session;
    auto &bitstream = decoder.bitstream;
    mfxStatus sts  = MFX_ERR_NONE;
    if (!decoder.started) {
        sts = ReadVideoStream(id, bitstream, decoder);

        CHECK_STS(sts,  "Error reading bitstream");

        decodeParams.mfx.CodecId = decoder.codec;
        decodeParams.IOPattern   = MFX_IOPATTERN_OUT_VIDEO_MEMORY;
        decodeParams.AsyncDepth = 2;
        sts                      = MFXVideoDECODE_DecodeHeader(session, &bitstream, &decodeParams);
        CHECK_STS(sts,  "Error decoding header");

#if 1
        mfxExtDecVideoProcessing *postProc = new mfxExtDecVideoProcessing;
        mfxExtBuffer **extBuf = new (mfxExtBuffer*);
        postProc->Header.BufferId = MFX_EXTBUFF_DEC_VIDEO_PROCESSING;
        postProc->Header.BufferSz = sizeof(mfxExtDecVideoProcessing);
        postProc->In.CropX = 0;
        postProc->In.CropY = 0;
        postProc->In.CropW = 1920;
        postProc->In.CropH = 1080;

        postProc->Out.FourCC       = decodeParams.mfx.FrameInfo.FourCC;
        postProc->Out.ChromaFormat = decodeParams.mfx.FrameInfo.ChromaFormat;
        postProc->Out.CropX        = 0;
        postProc->Out.CropY        = 0;
        postProc->Out.CropW = 1920;
        postProc->Out.CropH = 1080;
        postProc->Out.Width  = ALIGN16(postProc->Out.CropW);
        postProc->Out.Height = ALIGN16(postProc->Out.CropH);

        extBuf[0] = (mfxExtBuffer*)postProc;

        decodeParams.NumExtParam = 1;
        decodeParams.ExtParam = extBuf;
        decoder.postProc = postProc;
        decoder.extBuf = extBuf;
#endif
        // input parameters finished, now initialize decode
        sts = MFXVideoDECODE_Init(session, &decodeParams);
        CHECK_STS(sts, "Error initializing decode");

        std::cout<<"Decoder "<<id<<" init success"<<std::endl;
        decoder.started = true;
        decoder.isStillGoing = true;
    }
 
    while (decoder.isStillGoing == true) {

        auto sts = MFXVideoDECODE_DecodeFrameAsync(session,
                                              (decoder.isDraining) ? NULL : &bitstream,
                                              NULL,
                                              &decSurfaceOut,
                                              &decoder.syncp);

        switch (sts) {
            case MFX_ERR_NONE:
                do {
                    sts = decSurfaceOut->FrameInterface->Synchronize(decSurfaceOut,
                                                                     WAIT_100_MILLISECONDS);
                    if (MFX_ERR_NONE == sts) {
                        surface.surface = decSurfaceOut;
                        surface.framenum = decoder.framenum;
                        return 0;
                    }

                    //if (sts != MFX_WRN_IN_EXECUTION) {
                    //    sts = decSurfaceOut->FrameInterface->Release(decSurfaceOut);
                    //}

                } while (sts == MFX_WRN_IN_EXECUTION);
                break;
            case MFX_ERR_MORE_DATA:
                // The function requires more bitstream at input before decoding can
                // proceed
                if (decoder.isDraining)
                    decoder.isStillGoing = false;
                sts = ReadVideoStream(id, bitstream, decoder);
                if (sts != MFX_ERR_NONE) {
                    decoder.isDraining = true;
                }

                break;
            case MFX_WRN_VIDEO_PARAM_CHANGED:
                //if the resolution doesn't change, just ignore the warning.
                std::cout<<"decoder "<<id<<" warning "<< sts <<std::endl;
                sts = ReadVideoStream(id, bitstream, decoder);
                if (sts != MFX_ERR_NONE) {
                    decoder.isDraining = true;
                }
                break;
 
            default:
                std::cout<<"decoder "<<id<<" unknown status "<< sts <<std::endl;
                decoder.isStillGoing = false;
                break;
        }
    }

    return 0;
}

// Register components. Decoder must be removed at last.
int VplDecEnc::RemoveDecoder(int id) {
    CHECK_DEC_ID(id);
    auto &decoder = decoders_[id];

    MFXVideoDECODE_Close(decoder.session);
    MFXClose(decoder.session);
    if (decoder.isRtsp) {
        decoder.rtspReader->Close();
    }
    if (decoder.inputfile) {
        fclose(decoder.inputfile);
        decoder.inputfile = nullptr;
    }
    if (decoder.bitstream.Data) {
        free(decoder.bitstream.Data);
        decoder.bitstream.Data = nullptr;
    }

    if (decoder.postProc) {
        delete decoder.postProc;
        decoder.postProc = nullptr;
    }

    if (decoder.extBuf) {
        delete decoder.extBuf;
        decoder.postProc = nullptr;
    }
    decoders_.erase(id);
    return 0;
}

int VplDecEnc::SetEncodeOutput(int id,  const char *outfileName) {
    CHECK_ENC_ID(id);
    auto &encoder = encoders_[id];
    FILE *f = fopen(outfileName, "wb");
    if (!f) {
        std::cout<<__func__<< "encoder id "<<id<<" open output file " << outfileName<<" failed"<<std::endl;
        return -1;
    }

    encoder.outputfile = f;
    return 0;

}

int VplDecEnc::InitEncoder(int id) {
    auto &encoder = encoders_[id];
    auto &encodeParams = encoder.encodeParams;

    auto sts = MFXVideoENCODE_Query(encoder.session, &encodeParams, &encodeParams);
    if (sts == MFX_WRN_INCOMPATIBLE_VIDEO_PARAM)
        sts = MFX_ERR_NONE;
    CHECK_STS(sts, "MFXVideoENCODE_Query failed");

    // Initialize encoder
    sts = MFXVideoENCODE_Init(encoder.session, &encodeParams);
    CHECK_STS(sts, "MFXVideoENCODE_Init failed");

    encoder.bitstream.MaxLength = BITSTREAM_BUFFER_SIZE;
    encoder.bitstream.Data = (mfxU8 *)calloc(encoder.bitstream.MaxLength, sizeof(mfxU8));
    encoder.inited = true;
    return 0;

}

int VplDecEnc::AddEncoder(int id, int codec, int width, int height, int targetKbps) {
    if (encoders_.find(id) != encoders_.end()) {
        std::cout<<__func__<< "encoder id "<<id<<" already exist"<<std::endl;
        return -1;
    }

    if (decoders_.find(id) ==  decoders_.end() ) {
        std::cout<<__func__<<" the decoder id "<<id<<" does not exist! make sure create decoder firstly"<<std::endl;
        return -1;
    }

    encoders_[id] = VplEncoder {};
    auto &encoder = encoders_[id];
    encoder.session = decoders_[id].session;
    auto &encodeParams = encoder.encodeParams;
    // Initialize encode parameters
    encodeParams.mfx.CodecId                 = codec;
    encodeParams.mfx.TargetUsage             = MFX_TARGETUSAGE_BEST_SPEED;
    encodeParams.mfx.TargetKbps              = targetKbps;
    encodeParams.mfx.RateControlMethod       = MFX_RATECONTROL_CBR;
    encodeParams.mfx.FrameInfo.FrameRateExtN = 30;
    encodeParams.mfx.FrameInfo.FrameRateExtD = 1;
    encodeParams.mfx.FrameInfo.FourCC        = MFX_FOURCC_NV12;
    encodeParams.mfx.FrameInfo.ChromaFormat  = MFX_CHROMAFORMAT_YUV420;
    encodeParams.mfx.FrameInfo.CropW         = width;
    encodeParams.mfx.FrameInfo.CropH         = height;
    encodeParams.mfx.FrameInfo.Width         = ALIGN16(width);
    encodeParams.mfx.FrameInfo.Height        = ALIGN16(height);
    encodeParams.mfx.NumRefFrame             = 1;
    encodeParams.mfx.GopPicSize              = 60;
//encodeParams.mfx.GopRefDist = 1;
    //encodeParams.mfx.EncodedOrder = 1;
    encodeParams.AsyncDepth = 1;

    encodeParams.IOPattern = MFX_IOPATTERN_IN_VIDEO_MEMORY;

    encoder.oclRender.reset(new OpenCLFilterVA());
    auto res = encoder.oclRender->OCLInit(rootVaDpy);
    CHECK_STS(res, "OpenCLFilterVA OCLInit failed");
    std::cout<<__func__<<" add encoder "<<id<<std::endl;
    return 0;
}

void WriteEncodedStream(mfxBitstream &bs, FILE *f) {
    fwrite(bs.Data + bs.DataOffset, 1, bs.DataLength, f);
    bs.DataLength = 0;
    return;
}

int VplDecEnc::EncodeOneFrame(int id, mfxFrameSurface1 *surface) {
    CHECK_ENC_ID(id);
    CHECK_DEC_ID(id);
    auto &decoder = decoders_[id];
    auto &encoder = encoders_[id];

    if (!encoder.inited) {
        if (InitEncoder(id)) {
            std::cout<<__func__<<" Init encoder "<<id<<" failed"<<std::endl;
            return -1;
        }
    }

    if (!encoder.isStillGoing) {
        std::cout<<__func__<<" encode stopped!"<<std::endl;
        return 0;
    }

    auto sts = MFXVideoENCODE_EncodeFrameAsync(encoder.session,
            NULL,
            (encoder.isDraining == true) ? NULL : surface,
            &encoder.bitstream,
            &encoder.syncp);

    if (!encoder.isDraining) {
        surface->FrameInterface->Release(surface);
    }
    switch (sts) {
        case MFX_ERR_NONE:
            // MFX_ERR_NONE and syncp indicate output is available
            if (encoder.syncp) {
                // Encode output is not available on CPU until sync operation
                // completes
                do {
                    sts = MFXVideoCORE_SyncOperation(encoder.session, encoder.syncp, WAIT_100_MILLISECONDS);
                    if (MFX_ERR_NONE == sts) {
                        WriteEncodedStream(encoder.bitstream, encoder.outputfile);
                        encoder.framenum++;
                        if (verbose) {
                            std::cout<<"pipeline "<<id<<" encode frame "<<encoder.framenum<<std::endl;
                        }
                        break;
                    }
                } while (sts == MFX_WRN_IN_EXECUTION);
            }
            break;
        case MFX_ERR_MORE_DATA:
            std::cout<<__func__<<" encode id "<<id<<" MFXVideoCORE_SyncOperation return MFX_ERR_MORE_DATA"<<std::endl;
            // The function requires more data to generate any output
            if (encoder.isDraining == true)
                encoder.isStillGoing = false;
            break;
        default:
            encoder.isStillGoing = false;
            std::cout<<__func__<<" encode id "<<id<<" MFXVideoCORE_SyncOperation return error "<<sts<<std::endl;
            break;
    }
    return 0;

}

int VplDecEnc::GetOneFrameForEncode(int id, mfxFrameSurface1 **surface) {
    CHECK_ENC_ID(id);
    auto &encoder = encoders_[id];

    if (!encoder.inited) {
        if (InitEncoder(id)) {
            std::cout<<__func__<<" Init encoder "<<id<<" failed"<<std::endl;
            return -1;
        }
    }

    auto sts = MFXMemory_GetSurfaceForEncode(encoder.session, surface);
    CHECK_STS(sts, "MFXMemory_GetSurfaceForEncode failed");
    return 0;
}

int VplDecEnc::CopyOneFrame(int id, mfxFrameSurface1 *decSurfaceOut,
    mfxFrameSurface1 *encSurfaceIn,bool sync) {
    CHECK_ENC_ID(id);
    CHECK_DEC_ID(id);
    auto &decoder = decoders_[id];
    auto &encoder = encoders_[id];

    VASurfaceID decSurfId = VA_INVALID_ID, encSurfId = VA_INVALID_ID;
    {
        mfxHDL lresource;
        mfxResourceType lresourceType;

        auto sts = decSurfaceOut->FrameInterface->GetNativeHandle(decSurfaceOut, &lresource, &lresourceType);
        CHECK_STS(sts, "GetNativeHandle fail");
        decSurfId = *(VASurfaceID *)lresource;
        if (decSurfId == VA_INVALID_ID) {
            std::cout<<"pipeline "<<id<<" "<<__func__<<" Get dec surface native handle return invalid id "<<std::endl;
            return -1;
        }
        sts = encSurfaceIn->FrameInterface->GetNativeHandle(encSurfaceIn, &lresource, &lresourceType);
        CHECK_STS(sts, "GetNativeHandle fail");
        encSurfId = *(VASurfaceID *)lresource;
        if (encSurfId == VA_INVALID_ID) {
            std::cout<<"pipeline "<<id<<" "<<__func__<<" Get enc surface native handle return invalid id "<<std::endl;
            return -1;
        }

    }

    VACopyObject src_obj, dst_obj;
    VACopyOption option;
    src_obj.obj_type = VACopyObjectSurface;
    src_obj.object.surface_id = decSurfId;
    dst_obj.obj_type = VACopyObjectSurface;
    dst_obj.object.surface_id = encSurfId;
    option.bits.va_copy_mode = 2;//power save method

    auto res = vaCopy(rootVaDpy, &dst_obj, &src_obj, option);
    CHECK_STS(res, "vaCopy failed");
    res = vaSyncSurface(rootVaDpy, encSurfId);
    CHECK_STS(res, "vaSyncSurface");
    return res;
}

int VplDecEnc::RenderResult(int id, VplEncoder &encoder, mfxFrameSurface1 *encSurfaceIn, 
    std::vector<BoundingBox> &results) {
    
    std::vector<BoxRect> boxes;
    for (auto &res : results) {
        boxes.emplace_back(BoxRect{(uint)res.left, (uint)res.top, (uint)res.w, (uint)res.h});
    }

    bool truncated = false;
    //Even if boxes is empty, SetBoundingBox shall be called to clear previous inference result
    encoder.oclRender->SetBoundingBox(boxes, truncated);
    if (truncated) {
        std::cout<<"Warning number of objects "<<results.size()<<" exceed the maxinum "<<MAX_BOX_NUM<<std::endl; 
    }

    if (boxes.empty()) {
        return 0;
    }

    mfxHDL lresource;
    mfxResourceType lresourceType;
    auto sts = encSurfaceIn->FrameInterface->GetNativeHandle(encSurfaceIn, &lresource, &lresourceType);
    CHECK_STS(sts, "GetNativeHandle fail");
    VASurfaceID encSurfId = *(VASurfaceID *)lresource;
    if (encSurfId == VA_INVALID_ID) {
        std::cout<<"pipeline "<<id<<" "<<__func__<<" Get enc surface native handle return invalid id "<<std::endl;
        return -1;
    }
    int res = encoder.oclRender->ProcessSurface(
                encoder.encodeParams.mfx.FrameInfo.CropW, 
                encoder.encodeParams.mfx.FrameInfo.CropH, encSurfId);
    CHECK_STS(res, "encoder.oclRender->Render failed");
    return res;  
}
    
int VplDecEnc::RunPipeline(VplDecEnc &vplDecEnc, int id) {
    if (vplDecEnc.decoders_.find(id) == vplDecEnc.decoders_.end()) {
        std::cout<<__func__<<" Line"<<__LINE__<<" Invalid pipelie id"<<id<<std::endl;
        return -1;
    }
    auto &decoder = vplDecEnc.decoders_[id];
    VplSurface vplSurf = {};

    decoder.isStillGoing = true;
    decoder.fpsRuler.Reset();
    std::vector<BoundingBox> result;
    auto &yoloWrapper = vplDecEnc.yoloWrapper;
    auto &fastreidWrapper = vplDecEnc.fastreidWrapper;
    bool hasEncode = false;
    if (vplDecEnc.encoders_.find(id) == vplDecEnc.encoders_.end()) {
        std::cout<<__func__<<" Line"<<__LINE__<<" pipeline "<<id<<" encoder disabled"<<id<<std::endl;
    } else {
        vplDecEnc.encoders_[id].isStillGoing = true;
        hasEncode = true;
    }

#if 0
    //For test
    result.emplace_back(BoundingBox{128, 128, 256, 256});
#endif

    while (decoder.isStillGoing) {

       //run inference once every frame_skip frames, skip the rest
       bool skip = (decoder.framenum % vplDecEnc.frame_skip) != 0;
#ifdef MEASURE_TIME
       auto now = std::chrono::steady_clock::now();
#endif

        vplSurf = {};
        auto sts = vplDecEnc.DecodeScaleOneFrame(id, vplSurf, skip);

        CHECK_STS(sts, "DecodeOneFrame fail");
        if (!vplSurf.surface) {
            std::cout<<__func__<<" get null outputsurface !"<<std::endl;
            break;
        }

        if (vplDecEnc.verbose && decoder.id == 0) {
            std::cout<<"decoded frame "<<decoder.framenum<<std::endl;
        }

        if (!skip) {
            mfxHDL lresource;
            mfxResourceType lresourceType;

            sts = vplSurf.smallSurface->FrameInterface->GetNativeHandle(vplSurf.smallSurface, &lresource, &lresourceType);
            if (sts != MFX_ERR_NONE || !lresource) {
                std::cout<<__func__<<" Could not export to surface"<<std::endl;
                return sts;
            }

#if 0
            std::cout<<"Get decode scale out "<<*(VASurfaceID *)lresource<<std::endl;
            //For debug, save the small surface to /tmo/outxx.nv12
             if (id == 0 && decoder.framenum <= 10) {
                 vplDecEnc.saveOutSurface(vplDecEnc.rootVaDpy,*(VASurfaceID *)lresource, decoder.framenum);
             }
             else {
                 return -1;
             }
#endif
             if (!vplDecEnc.disableYolo) {
                 yoloWrapper.yoloInfer(id, decoder.width, decoder.height, 
                         *(VASurfaceID *)lresource, decoder.letterboxParams, result, decoder.framenum);
             }

#ifdef MEASURE_TIME
       if (id == 0)  {
           auto last = std::chrono::steady_clock::now();
           const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(last - now);
           std::cout<<"yolo "<<decoder.framenum<<" : "<<elapsed.count()<<std::endl;
       }
#endif


            vplSurf.smallSurface->FrameInterface->Release(vplSurf.smallSurface);

            if (result.size() > 0 && decoder.needCrop) {
                std::vector<VASurfaceID> surfs;

#ifdef MEASURE_TIME
       auto cropbegin = std::chrono::steady_clock::now();
#endif
                sts = vplDecEnc.scalingSurfacesForFastreid(decoder, vplSurf, result, surfs);

#ifdef MEASURE_TIME
                auto cropend = std::chrono::steady_clock::now();
                if (id == 0) {

                    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(cropend - cropbegin);
                    std::cout<<"crop "<<decoder.framenum<<" : "<<elapsed.count()<<std::endl;
                }
#endif
                if (sts != 0) {
                    std::cout<<"pipeline "<<id<<" "<<__func__<<" L"<<__LINE__<<" scalingSurfacesForFastreid return error "<<sts<<std::endl;
                    return sts;
                }
                if ( surfs.size() < 1)  {
                    std::cout<<"pipeline "<<id<<" "<<__func__<<" L"<<__LINE__<<" scalingSurfacesForFastreid return 0 surfaces "<<sts<<std::endl;
                }

                if (fastreidWrapper.enableFastreid) {
                    if (fastreidWrapper.enableBatchInfer) {
                        fastreidWrapper.fastReIDBatchInfer(id, surfs, decoder.framenum);
                    } else {
                        for (size_t i = 0; i < result.size(); i++) {
                            fastreidWrapper.fastReIDInfer(id, surfs[i], decoder.framenum);
                        }
                    }
                }

#ifdef MEASURE_TIME
                auto fastreidEnd = std::chrono::steady_clock::now();
                if (id == 0)  {
                    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(fastreidEnd - cropend);
                    std::cout<<"fastreid "<<decoder.framenum<<" : "<<elapsed.count()<<std::endl;
                }
#endif

                //just for debug. Save pipeline 0 crop surfaces to /tmp/
#if 0
                std::cout<<"Get "<< surfs.size() << " crop surfaces "<<std::endl;
                //For debug, save the small surface to /tmo/outxx_wxh.nv12
                if (id == 0 && decoder.framenum <= 2 && surfs.size() > 0 && surfs[0] >= 0) {
                    vplDecEnc.saveOutSurface(vplDecEnc.rootVaDpy, surfs[0], decoder.framenum);
                }
                else {
                    return -1;
                }
#endif
                //save cropped area to jpg file File path: jpg/pipexx_frameyy_zz.jpg
                if (vplDecEnc.jpgencoders_.find(id) != vplDecEnc.jpgencoders_.end()) {
                    //JPEG encode quality set to 90
                    std::vector<SurfEncodeParams> encodeParams(result.size(),
                            SurfEncodeParams {FASTREID_INPUT_WIDTH, FASTREID_INPUT_HEIGHT, 90});
                    std::vector<std::vector<unsigned char>> out_buffer;
                    vplDecEnc.jpgencoders_[id].Encode(surfs, encodeParams, out_buffer);  
                    if (vplDecEnc.verbose) {
                        std::cout<<"Pipeline "<<id<<" encode "<<out_buffer.size()<<" jpg pictures"<<std::endl;
                    }
		    //Saving to file will impact performance, only save the jpg in beginning 5 frames
                    if (decoder.framenum < 5) {
                        for (int i = 0; i < out_buffer.size(); i++) {
                            vplDecEnc.saveDataToFile(out_buffer[i], "jpg", id, decoder.framenum, i);
                        }
		    }
                }
            }
        }

        if (hasEncode) {

#ifdef MEASURE_TIME
            auto encodeBegin = std::chrono::steady_clock::now();
#endif
            auto &encoder = vplDecEnc.encoders_[id];
            mfxFrameSurface1 *encSurfaceIn = nullptr;
            sts = vplDecEnc.GetOneFrameForEncode(id, &encSurfaceIn);
            CHECK_STS(sts, "GetOneFrameForEncode fail");
            sts = vplDecEnc.CopyOneFrame(id, vplSurf.surface, encSurfaceIn,false);
            CHECK_STS(sts, "CopyOneFrame fail");
            if (result.size() > 0) {
                sts = vplDecEnc.RenderResult(id, encoder, encSurfaceIn, result);
                CHECK_STS(sts, "RenderResul fail");
            }
            
            sts = vplDecEnc.EncodeOneFrame(id, encSurfaceIn);
            CHECK_STS(sts, "EncodeOneFrame fail");

            encSurfaceIn->FrameInterface->Release(encSurfaceIn);

#ifdef MEASURE_TIME
            auto encodeEnd = std::chrono::steady_clock::now();
            if (id == 0)  {
                auto last = std::chrono::steady_clock::now();
                const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(encodeEnd - encodeBegin);
                std::cout<<"encode "<<decoder.framenum<<" : "<<elapsed.count()<<std::endl;
            }
#endif

        }
        //Must release the decode out surface
        vplSurf.surface->FrameInterface->Release(vplSurf.surface);

        if (vplSurf.surfArray) {
            vplSurf.surfArray->Release(vplSurf.surfArray);
            vplSurf.surfArray = nullptr;
        }

#ifdef MEASURE_TIME
       if (id == 0)  {
           auto last = std::chrono::steady_clock::now();
           const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(last - now);
           std::cout<<"frame "<<decoder.framenum<<" : "<<elapsed.count()<<std::endl;
       }
#endif

        decoder.framenum++;
        decoder.fpsRuler.PrintFPS(decoder.framenum, id);

    }
    std::cout<<"pipeline "<<id<<" ends "<<". total decoded frame "<<decoder.framenum<<std::endl;
    return 0;
}

int VplDecEnc::RemoveEncoder(int id) {
    CHECK_ENC_ID(id);
    auto &encoder = encoders_[id];

    MFXVideoENCODE_Close(encoder.session);
    if (encoder.outputfile) {
        fclose(encoder.outputfile);
        encoder.outputfile = nullptr;
    }
    if (encoder.bitstream.Data) {
        free(encoder.bitstream.Data);
        encoder.bitstream.Data = nullptr;
        encoder.bitstream.DataOffset = 0;
        encoder.bitstream.MaxLength = 0;
    }
    encoders_.erase(id);
    return 0;
}


// Configure IO and start
int VplDecEnc::SetDecodeInput(int id, const char* filename) {
    CHECK_DEC_ID(id);
    std::string inputName(filename);

    std::cout<<"pipeline "<<id<<" input path/url is "<<filename<<std::endl;
    auto &decoder = decoders_[id];
    if (inputName.rfind(RTSP_URL_PREFIX) == 0) {
        decoder.rtspReader = std::make_shared<BitstreamRTSPReader>();
        decoder.isRtsp = true;
        int retry = 5;
        while (retry-- > 0) {
            if (0 == decoder.rtspReader->Open(filename)) {
                std::cout<<"decoder "<<id<<" "<<__func__<<" Open RTSP url "<<filename<<" succesfull"<<std::endl;
                return 0;
            } else {
                if (retry > 0) {
                    std::cout<<"decoder "<<id<<" "<<__func__<<" Open RTSP url "<<filename<<" failed! Retry "<<retry<<std::endl;
                }
            }
        }
        return -1;
    } else {
        FILE *f = fopen(filename, "rb");
        if (!f) {
            std::cout<<"error "<<__func__<<" line"<<__LINE__<<" open file " << filename<< " failed!" <<std::endl;
            return -1;
        }
        decoder.inputfile = f;
    }
    return 0;
}

int VplDecEnc::StartPipeline(int /*id*/) {
    return 0;
}


int VplDecEnc::scalingSurfacesForFastreid(VplDecoder &decoder,
           const VplSurface inSurf,
           const std::vector<BoundingBox> &result,
           std::vector<VASurfaceID> &outSurfs) {
    int id = decoder.id;
    mfxHDL lresource;
    mfxResourceType lresourceType;

    auto sts = inSurf.surface->FrameInterface->GetNativeHandle(inSurf.surface, &lresource, &lresourceType);
    if (sts != MFX_ERR_NONE || !lresource) {
        std::cout<<"pipeline "<<id<<" "<<__func__<<" Could not export to surface"<<std::endl;
        return sts;
    }
    VASurfaceID inputSurfId = *(VASurfaceID *)lresource;

    int cur_surf_num = decoder.fastreidPreProcessor.GetOutputSurfaceNumber();
    if (cur_surf_num < result.size()) {
        int r = decoder.fastreidPreProcessor.CreateOutputs(FASTREID_INPUT_WIDTH, FASTREID_INPUT_HEIGHT, result.size() - cur_surf_num);
         CHECK_STS(r,
            " decoder.fastreidPreProcessor.CreateOutputs error");
    }

    std::vector<VARectangle> inRegion(result.size()), outRegion(result.size());

    int dw,dh;
    float r;
    for (int i = 0; i < result.size(); i++) {
        const BoundingBox &box = result[i];
        inRegion[i].x = std::max(0, box.left);
        inRegion[i].y = std::max(0, box.top);
        inRegion[i].width = std::min(box.w, decoder.width - inRegion[i].x);
        inRegion[i].height = std::min(box.h, decoder.height - inRegion[i].y);

        if (inRegion[i].width < 1 || inRegion[i].height < 1) {
            std::cout<<"pipeline  "<<id<<" "<< __func__<<" Invalid inference result : ("<<box.left<<","<<box.top<<","<<box.w<<","<<box.h<<")"<<std::endl;
            continue;
        }

        r = std::min((float)FASTREID_INPUT_WIDTH / inRegion[i].width, (float)FASTREID_INPUT_HEIGHT / inRegion[i].height);
        outRegion[i].width = std::min((int)round(inRegion[i].width * r), FASTREID_INPUT_WIDTH);
        outRegion[i].height = std::min((int)round(inRegion[i].height * r), FASTREID_INPUT_HEIGHT);

        dw = FASTREID_INPUT_WIDTH - outRegion[i].width;
        dh = FASTREID_INPUT_HEIGHT - outRegion[i].height;
        outRegion[i].y = std::max(0, dh / 2);
        outRegion[i].x = std::max(0, dw / 2);
    }

    int res = decoder.fastreidPreProcessor.SetCrop(inRegion, outRegion);
    if (res != 0) {
        std::cout<<"pipeline  "<<id<<" decoder.fastreidPreProcessor.SetCrop "<<inRegion.size()<<" failed "<<res<<std::endl;
        return res;
    }
    res = decoder.fastreidPreProcessor.ProcessSurface(inputSurfId, outSurfs);

    if (res != 0) {
        std::cout<<"pipeline  "<<id<<" decoder.fastreidPreProcessor.ProcessSurface "<<inRegion.size()<<" failed "<<res<<std::endl;
        return res;
    }
    return 0;
}

int VplDecEnc::AddJpgEncoder(int id, unsigned int quality) {
    if (jpgencoders_.find(id) != jpgencoders_.end()) {
        std::cout<<"error "<<__func__<<" line"<<__LINE__<<" jpeg encode id "<<id<<" alreay exist!"<<std::endl;
        return -1;
    }

    if (quality > 100) {
        std::cout<<"error "<<__func__<<" line"<<__LINE__<<" jpeg encode id "<<id<<" invalid quality value "<<quality<<". Set to 90"<<std::endl;
        quality = 90;
    }

    jpgencoders_[id] = JpegEncoder{};
    jpgencoders_[id].id = id;
    int res = jpgencoders_[id].Init(rootVaDpy);
    if (res != 0) {
        jpgencoders_.erase(id);
        return res;
    }
    return 0;
}

int VplDecEnc::RemoveJpgEncoder(int id) {
    if (jpgencoders_.find(id) == jpgencoders_.end()) {
        std::cout<<"error "<<__func__<<" line"<<__LINE__<<" jpeg encode id "<<id<<" not exist!"<<std::endl;
        return -1;
    }

    jpgencoders_.erase(id);
    return 0;
}

int VplDecEnc::saveDataToFile(const std::vector<unsigned char>& data_vec,
                   const std::string& path_prefix,
                   int pipeline,
                   int frame_num,
                   int index)
{
    if (data_vec.empty()) {
        return -1;  // nothing to write
    }

    std::ostringstream filename;
    filename << path_prefix << "/"
             << "pipe"  << std::setw(2) << std::setfill('0') << pipeline
             << "_frame" << std::setw(2) << std::setfill('0') << frame_num
             << "_" << std::setw(2) << std::setfill('0') << index
             << ".jpg";

    std::ofstream ofs(filename.str(), std::ios::binary);
    if (!ofs.is_open()) {
        return -2;  // failed to open file
    }

    ofs.write(reinterpret_cast<const char*>(data_vec.data()),
              static_cast<std::streamsize>(data_vec.size()));

    if (!ofs.good()) {
        return -3;  // write error
    }

    ofs.close();
    return 0;  // success
}
