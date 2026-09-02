#include <va/va.h>
#include <va/va_enc_jpeg.h>
#include <cstring>
#include <vector>

struct SurfEncodeParams {
    int width;
    int height;
    int quality;
};

class JpegEncoder {
public:
    JpegEncoder();
    ~JpegEncoder();

    int Init(VADisplay dpy);
    int Encode(const std::vector<VASurfaceID>  &surface, //only support NV12 surface
            const std::vector<SurfEncodeParams> &encodeParmas,
            std::vector<std::vector<unsigned char>> &out_buffer//jpg data will be store to this buffer
                );

    int id;
private:
    VADisplay     vadpy{};
    VAContextID   vactx{};
    VAConfigID    vacfg{};
    std::vector<VAHuffmanTableBufferJPEGBaseline> vahts;
    VABufferID    vahtId = VA_INVALID_ID;
};
