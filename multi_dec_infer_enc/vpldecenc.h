
#pragma once

#include <string>
#include <unordered_map>
#include <vector>
#include <utility>
#include <iostream>
#include <chrono>
#include <thread>

#include "vpl/mfxvideo.h"
#include "vpl/mfxdispatcher.h"
#include "openvino_yolo.h"
#include "openvino_fastreid.h"

#include "rtsp/include/bitstream_rtsp_reader.h"
#include "surface_crop.h"
#include "jpeg_encoder.h"
#include "ocl_draw/opencl_filter_va.h"

#define CHECK_STS(sts, f)       \
    if (0 != (sts)) {            \
        std::cout<<"pipeline "<<id<<" "<<f<<" return error "<<sts<<std::endl; \
        return -1;          \
    }

#define BITSTREAM_BUFFER_SIZE (2 * 1024 * 1024) 
#define WAIT_100_MILLISECONDS 100

#define RTSP_URL_PREFIX "rtsp:"
class FpsRuler {
    public:
        void Reset() {
            last = std::chrono::steady_clock::now();
        }
        void PrintFPS(int frame_num, int id) {
            auto now = std::chrono::steady_clock::now();
            const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - last);
            if (elapsed.count() >= 1000) {
                const double fps = (frame_num - frame_num_start) * 1000.0 / elapsed.count();
                std::cout<<" pipeline "<<id<<". fps "<<fps<<std::endl;
                last = now;
                frame_num_start = frame_num;
            }
        }

    private:
        std::chrono::time_point<std::chrono::steady_clock> last;
        int frame_num_start = 0;

};

class VplDecEnc {
public:
    VplDecEnc() = default;
    ~VplDecEnc();

    struct VplSurface {
        mfxFrameSurface1 *surface; //decode output
        mfxFrameSurface1 *smallSurface; //size equal to inference input
        int framenum;
        mfxSurfaceArray *surfArray; //surfArray handle
    };

    // Initialize/Deinitialize pipeline resources (devices, sessions, etc.)
    // deviceID=0, dri node: /dev/dri/renderD128  
    int Init(int deviceID);
    int InitDevice(mfxU32 nAdapterNum,  mfxHDL &pHandle);
    void Deinit();

    // Optional: add an inference model (e.g., OpenVINO IR file) to each pipeline. 
    // Must be called before AddDecoder
    int CreateInferenceModel(const char* yoloIRFilename, const char* fastreidIRFilename);

    static int RunPipeline(VplDecEnc &vplDecEnc, int id);

    // Register components. decoder must be inserted before encoder.
    // encoder must be removed before decoder with same id. They shares same VPL session
    int AddDecoder(int id, int codec);
    int RemoveDecoder(int id);

    int AddEncoder(int id, int codec, int width, int height, int targetKbps);
    int RemoveEncoder(int id);

    int AddJpgEncoder(int id, unsigned int quality = 90);
    int RemoveJpgEncoder(int id);

    // Configure IO and start
    int SetDecodeInput(int id, const char* filename);
    int SetEncodeOutput(int id,  const char *outfileName);
    int StartPipeline(int id);

    int DecodeOneFrame(int id, VplSurface &surface);
    int DecodeScaleOneFrame(int id, VplSurface &surface, bool needScaling);
    int EncodeOneFrame(int id, mfxFrameSurface1 *surface);
    int GetOneFrameForEncode(int id, mfxFrameSurface1 **surface);
    //By default, sync operation is not performed in CopyOneFrame.
    int CopyOneFrame(int id, mfxFrameSurface1 *decSurfaceOut,  mfxFrameSurface1 *encSurfaceIn, bool sync = false);
    int saveOutSurface(VADisplay dpy, VASurfaceID surf, int framenum);

    VplDecEnc(const VplDecEnc&) = delete;
    VplDecEnc& operator=(const VplDecEnc&) = delete;
    VplDecEnc(VplDecEnc&&) = default;
    VplDecEnc& operator=(VplDecEnc&&) = default;

   
    struct VplDecoder {
        int codec{}; //only support MFX_CODEC_AVC and MFX_CODEC_HEVC
        int id{};
        
        bool inited{false}; //session and decoder created or not
        bool started{false}; //bitstream header parsed or not
        bool isStillGoing{false}; //in running or not. set to false to stop decode thread
        bool isDraining{false}; //read end of input or not
        mfxSession session              = NULL;
        mfxSyncPoint syncp              = {};
        mfxU32 framenum                 = 0;
        FILE *inputfile = nullptr;
        mfxVideoParam decodeParams      = {};
        mfxBitstream bitstream          = {};
        FpsRuler fpsRuler;

        mfxExtDecVideoProcessing *postProc = nullptr;
        mfxExtBuffer **extBuf = nullptr;
        std::shared_ptr<BitstreamRTSPReader> rtspReader;
        bool isRtsp = false;
        VADisplay vaDpy = nullptr;
        int width = -1;
        int height = -1;
        LetterboxParams letterboxParams{};

        bool needCrop = false;
        SurfaceCrop fastreidPreProcessor;
        std::vector<VASurfaceID> fastreidInputSurfs;
    };

     struct VplEncoder {
        int codec{}; //only support MFX_CODEC_AVC and MFX_CODEC_HEVC
        int id{};
        
        bool inited{false}; // encoder created or not
        bool started{false}; 
        bool isStillGoing{false}; //in running or not. set to false to stop decode thread
        bool isDraining{false}; //read end of input or not
        mfxSession session              = NULL;
        mfxSyncPoint syncp              = {};
        mfxU32 framenum                 = 0;
        FILE *outputfile = nullptr;
        mfxVideoParam encodeParams      = {};
        mfxBitstream bitstream          = {};
        FpsRuler fpsRuler;
        std::unique_ptr<OpenCLFilterVA>  oclRender;
    };

    int RenderResult(int id, VplEncoder &encoder, mfxFrameSurface1 *encSurfaceIn, std::vector<BoundingBox> &results);

    mfxVideoChannelParam ppParam[1];
    mfxStatus ReadRTSPStream(int id, mfxBitstream &bs, VplDecoder &decoder);
    mfxStatus ReadFileStream(int id, mfxBitstream &bs, FILE *f);
    mfxStatus ReadVideoStream(int id, mfxBitstream &bs, VplDecoder &decoder);

    static  int saveDataToFile(const std::vector<unsigned char>& data_vec,
            const std::string& path_prefix,
            int pipeline,
            int frame_num,
            int index);

    // --- Storage for registered components ---
    std::unordered_map<int, VplDecoder>    decoders_;
    std::unordered_map<int, VplEncoder>    encoders_;
    std::unordered_map<int, JpegEncoder>    jpgencoders_;

    // Global state
    bool mInitialized_{false};
    OpenvinoYolo yoloWrapper;
    OpenvinoFastReID fastreidWrapper;
    bool disableYolo = false; //For debug
    int frame_skip = 2;

    bool verbose = false;

    VADisplay rootVaDpy = nullptr;

private:
   int InitEncoder(int id);
   int scalingSurfacesForFastreid(VplDecoder &decoder,
           const VplSurface inSurf,
           const std::vector<BoundingBox> &result,
           std::vector<VASurfaceID> &outSurfs);

   mfxLoader loader = NULL;
   unsigned int desiredImplIndex = 0;
   int vaDRMfd = -1;
   mfxConfig  mfxc1, mfxc2;
   int loop_count = 0;

};
