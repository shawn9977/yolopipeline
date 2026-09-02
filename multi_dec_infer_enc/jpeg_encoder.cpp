#include "iostream"
#include "jpeg_encoder.h"


#define CHK_VA_RES(res) \
    if ((res) != 0) {\
        std::cout<<__func__<<" LINE"<<__LINE__<<" libva return error "<<res<<std::endl; \
        return res; \
    }

const uint8_t DefaultLuminanceDCBits[16] =
{
  0x00, 0x01, 0x05, 0x01, 0x01, 0x01, 0x01, 0x01,
  0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};


const uint8_t DefaultLuminanceDCValues[256] =
{
  0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
  0x08, 0x09, 0x0a, 0x0b
};


const uint8_t DefaultChrominanceDCBits[16] =
{
  0x00, 0x03, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
  0x01, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00
};


const uint8_t DefaultChrominanceDCValues[256] =
{
  0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
  0x08, 0x09, 0x0a, 0x0b
};


const uint8_t DefaultLuminanceACBits[16] =
{
  0x00, 0x02, 0x01, 0x03, 0x03, 0x02, 0x04, 0x03,
  0x05, 0x05, 0x04, 0x04, 0x00, 0x00, 0x01, 0x7d
};


const uint8_t DefaultLuminanceACValues[256] =
{
  0x01, 0x02, 0x03, 0x00, 0x04, 0x11, 0x05, 0x12,
  0x21, 0x31, 0x41, 0x06, 0x13, 0x51, 0x61, 0x07,
  0x22, 0x71, 0x14, 0x32, 0x81, 0x91, 0xa1, 0x08,
  0x23, 0x42, 0xb1, 0xc1, 0x15, 0x52, 0xd1, 0xf0,
  0x24, 0x33, 0x62, 0x72, 0x82, 0x09, 0x0a, 0x16,
  0x17, 0x18, 0x19, 0x1a, 0x25, 0x26, 0x27, 0x28,
  0x29, 0x2a, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39,
  0x3a, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48, 0x49,
  0x4a, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58, 0x59,
  0x5a, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69,
  0x6a, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78, 0x79,
  0x7a, 0x83, 0x84, 0x85, 0x86, 0x87, 0x88, 0x89,
  0x8a, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97, 0x98,
  0x99, 0x9a, 0xa2, 0xa3, 0xa4, 0xa5, 0xa6, 0xa7,
  0xa8, 0xa9, 0xaa, 0xb2, 0xb3, 0xb4, 0xb5, 0xb6,
  0xb7, 0xb8, 0xb9, 0xba, 0xc2, 0xc3, 0xc4, 0xc5,
  0xc6, 0xc7, 0xc8, 0xc9, 0xca, 0xd2, 0xd3, 0xd4,
  0xd5, 0xd6, 0xd7, 0xd8, 0xd9, 0xda, 0xe1, 0xe2,
  0xe3, 0xe4, 0xe5, 0xe6, 0xe7, 0xe8, 0xe9, 0xea,
  0xf1, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6, 0xf7, 0xf8,
  0xf9, 0xfa
};


const uint8_t DefaultChrominanceACBits[16] =
{
  0x00, 0x02, 0x01, 0x02, 0x04, 0x04, 0x03, 0x04,
  0x07, 0x05, 0x04, 0x04, 0x00, 0x01, 0x02, 0x77
};


const uint8_t DefaultChrominanceACValues[256] =
{
  0x00, 0x01, 0x02, 0x03, 0x11, 0x04, 0x05, 0x21,
  0x31, 0x06, 0x12, 0x41, 0x51, 0x07, 0x61, 0x71,
  0x13, 0x22, 0x32, 0x81, 0x08, 0x14, 0x42, 0x91,
  0xa1, 0xb1, 0xc1, 0x09, 0x23, 0x33, 0x52, 0xf0,
  0x15, 0x62, 0x72, 0xd1, 0x0a, 0x16, 0x24, 0x34,
  0xe1, 0x25, 0xf1, 0x17, 0x18, 0x19, 0x1a, 0x26,
  0x27, 0x28, 0x29, 0x2a, 0x35, 0x36, 0x37, 0x38,
  0x39, 0x3a, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48,
  0x49, 0x4a, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58,
  0x59, 0x5a, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68,
  0x69, 0x6a, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78,
  0x79, 0x7a, 0x82, 0x83, 0x84, 0x85, 0x86, 0x87,
  0x88, 0x89, 0x8a, 0x92, 0x93, 0x94, 0x95, 0x96,
  0x97, 0x98, 0x99, 0x9a, 0xa2, 0xa3, 0xa4, 0xa5,
  0xa6, 0xa7, 0xa8, 0xa9, 0xaa, 0xb2, 0xb3, 0xb4,
  0xb5, 0xb6, 0xb7, 0xb8, 0xb9, 0xba, 0xc2, 0xc3,
  0xc4, 0xc5, 0xc6, 0xc7, 0xc8, 0xc9, 0xca, 0xd2,
  0xd3, 0xd4, 0xd5, 0xd6, 0xd7, 0xd8, 0xd9, 0xda,
  0xe2, 0xe3, 0xe4, 0xe5, 0xe6, 0xe7, 0xe8, 0xe9,
  0xea, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6, 0xf7, 0xf8,
  0xf9, 0xfa
};



JpegEncoder::JpegEncoder() = default;

JpegEncoder::~JpegEncoder() {

    if (vahtId != VA_INVALID_ID) vaDestroyBuffer(vadpy, vahtId);
    if (vactx) vaDestroyContext(vadpy, vactx);
    if (vacfg) vaDestroyConfig(vadpy, vacfg);

}

/*
void jpegenc_qmatrix_init(VAQMatrixBufferJPEG *quantization_param)
{
    int i = 0;
    quantization_param->load_lum_quantiser_matrix = 1;

    //LibVA expects the QM in zigzag order
    for (i = 0; i < NUM_QUANT_ELEMENTS; i++) {
        quantization_param->lum_quantiser_matrix[i] = jpeg_luma_quant[jpeg_zigzag[i]];
    }


    if (yuvComp.fourcc_val == VA_FOURCC_Y800) {
        quantization_param->load_chroma_quantiser_matrix = 0;
    } else {
        quantization_param->load_chroma_quantiser_matrix = 1;
        for (i = 0; i < NUM_QUANT_ELEMENTS; i++) {
            quantization_param->chroma_quantiser_matrix[i] = jpeg_chroma_quant[jpeg_zigzag[i]];
        }
    }

}
*/

int JpegEncoder::Init(VADisplay dpy) {
    vadpy = dpy;

    VAConfigAttrib attr[2];
    attr[0] = VAConfigAttrib{};
    attr[1] = VAConfigAttrib{};
    attr[1].type = VAConfigAttribEncJPEG;
    attr[0].type = VAConfigAttribRTFormat;
    vaGetConfigAttributes(dpy, VAProfileJPEGBaseline,
                          VAEntrypointEncPicture, &attr[0], 2);

    VAConfigAttribValEncJPEG jpeg_attrib_val;
    jpeg_attrib_val.value = attr[1].value;

    /* Set JPEG profile attribs */
    jpeg_attrib_val.bits.arithmatic_coding_mode = 0;
    jpeg_attrib_val.bits.progressive_dct_mode = 0;
    jpeg_attrib_val.bits.non_interleaved_mode = 1;
    jpeg_attrib_val.bits.differential_mode = 0;

    attr[1].value = jpeg_attrib_val.value;

    int res = vaCreateConfig(dpy,
                   VAProfileJPEGBaseline,
                   VAEntrypointEncPicture,
                   &attr[0], 2, &vacfg);
    CHK_VA_RES(res);
    res = vaCreateContext(dpy,
                    vacfg,
                    1920, 1080, 
                    VA_PROGRESSIVE,
                    nullptr, 0,
                    &vactx);

    CHK_VA_RES(res);

    vahts.resize(1);
    vahts[0].load_huffman_table[0] = 1;
    vahts[0].load_huffman_table[1] = 1;


    for(int i = 0; i < 16; i++)
        vahts[0].huffman_table[0].num_dc_codes[i] = DefaultLuminanceDCBits[i];
    for(int i = 0; i < 12; i++)
        vahts[0].huffman_table[0].dc_values[i] = DefaultLuminanceDCValues[i];
    for(int i = 0; i < 16; i++)
        vahts[0].huffman_table[0].num_ac_codes[i] = DefaultLuminanceACBits[i];
    for(int i = 0; i < 162; i++)
        vahts[0].huffman_table[0].ac_values[i] = DefaultLuminanceACValues[i];
    for(int i = 0; i < 16; i++)
        vahts[0].huffman_table[1].num_dc_codes[i] = DefaultChrominanceDCBits[i];
    for(int i = 0; i < 12; i++)
        vahts[0].huffman_table[1].dc_values[i] = DefaultChrominanceDCValues[i];
    for(int i = 0; i < 16; i++)
        vahts[0].huffman_table[1].num_ac_codes[i] = DefaultChrominanceACBits[i];
    for(int i = 0; i < 162; i++)
        vahts[0].huffman_table[1].ac_values[i] = DefaultChrominanceACValues[i];

    res = vaCreateBuffer(dpy, vactx, VAHuffmanTableBufferType, sizeof(VAHuffmanTableBufferJPEGBaseline), 1, &vahts[0], &vahtId);

    CHK_VA_RES(res);
    return 0; 
}

int JpegEncoder::Encode(const std::vector<VASurfaceID>  &surfaces, //only support NV12 surface
            const std::vector<SurfEncodeParams> &encodeParams,
            std::vector<std::vector<unsigned char>> &out_buffers)
{
    if (surfaces.size() != encodeParams.size()) {
        std::cout<<"Pipeline "<<id<<" "<<__func__<<" surface size "<<surfaces.size()<<" must be equal to encodeParmas size" << encodeParams.size()<<std::endl;
        return -1;
    } 

    VAEncPictureParameterBufferJPEG pic{};
    std::vector<VABufferID> coded_bufs(surfaces.size());
    int res = 0;
    for (int i = 0; i < surfaces.size(); i++) {
        auto &encodeParam = encodeParams[i];
        unsigned int coded_buf_size = encodeParam.width * encodeParam.height * 3 / 2;
        // Create coded buffer
        VABufferID coded_buf_id;
        res = vaCreateBuffer(vadpy, vactx,
                VAEncCodedBufferType,
                coded_buf_size, 1,
                nullptr, &coded_buf_id);

        CHK_VA_RES(res);

        pic.picture_width  = encodeParam.width;
        pic.picture_height = encodeParam.height;
        pic.quality        = encodeParam.quality;
        pic.pic_flags.bits.profile = 0;  // baseline
        pic.sample_bit_depth = 8;

        pic.num_components = 3;
        pic.component_id[0] = 1; // Y
        pic.component_id[1] = 2; // Cb
        pic.component_id[2] = 3; // Cr

        pic.num_scan = 1;
        pic.quantiser_table_selector[0] = 0;
        pic.quantiser_table_selector[1] = 0;
        pic.quantiser_table_selector[2] = 0;

        pic.coded_buf = coded_buf_id;
        VABufferID pic_buf{};
        res = vaCreateBuffer(vadpy, vactx,
                VAEncPictureParameterBufferType,
                sizeof(pic), 1, &pic, &pic_buf);

        CHK_VA_RES(res);
    
        coded_bufs[i] = coded_buf_id;
        res = vaBeginPicture(vadpy, vactx, surfaces[i]);
        CHK_VA_RES(res);

        res = vaRenderPicture(vadpy, vactx, &pic_buf, 1);
        CHK_VA_RES(res);
        res = vaRenderPicture(vadpy, vactx, (VABufferID *)&vahtId, 1);
        CHK_VA_RES(res);

        VAEncSliceParameterBufferJPEG slice_param{};
        slice_param.restart_interval = 0;

        slice_param.num_components = 3;

        slice_param.components[0].component_selector = 1;
        slice_param.components[0].dc_table_selector = 0;
        slice_param.components[0].ac_table_selector = 0;

        slice_param.components[1].component_selector = 2;
        slice_param.components[1].dc_table_selector = 1;
        slice_param.components[1].ac_table_selector = 1;

        slice_param.components[2].component_selector = 3;
        slice_param.components[2].dc_table_selector = 1;
        slice_param.components[2].ac_table_selector = 1;
        VABufferID slice_param_buf_id{};
        res = vaCreateBuffer(vadpy, vactx, VAEncSliceParameterBufferType,
                               sizeof(slice_param), 1, &slice_param, &slice_param_buf_id);

        CHK_VA_RES(res);
        res = vaRenderPicture(vadpy, vactx, &slice_param_buf_id, 1);
        CHK_VA_RES(res);

        res = vaEndPicture(vadpy, vactx);
        CHK_VA_RES(res);

        vaDestroyBuffer(vadpy, pic_buf);
        vaDestroyBuffer(vadpy, slice_param_buf_id);
        res = vaSyncSurface(vadpy, surfaces[i]);
        CHK_VA_RES(res);
    }

    VACodedBufferSegment* seg{};
    for (int i = 0; i < surfaces.size(); i++) {
        // Map coded buffer
        auto &coded_buf = coded_bufs[i];
        res = vaMapBuffer(vadpy, coded_buf, (void**)&seg);
        CHK_VA_RES(res);
        if (seg->size > 0) {
            auto pbuf = (unsigned char *)seg->buf;
            out_buffers.emplace_back(pbuf, pbuf + seg->size);
            //std::cout<<"save "<<seg->size<<" jpg data"<<std::endl;
        }

        vaUnmapBuffer(vadpy, coded_buf);
        vaDestroyBuffer(vadpy, coded_buf);
    }
    return 0;
}
