
#ifndef _BIT_STREAM_RTSP_READER__
#define _BIT_STREAM_RTSP_READER__

#include "bitstream_reader.h"
#include <thread>

class RTSPClientExt;
class BufferSink;

class BitstreamRTSPReader : public BitstreamReader {
public :
    BitstreamRTSPReader();
    virtual ~BitstreamRTSPReader();

    virtual int       Open(const char *uri);
    virtual int       Read(char *buffer, size_t bytesNum);
    virtual void      Reset();
    virtual void      Close();
public:
    static bool mLive555Initialized;
    static std::thread *mLive555ThreadId;
private:
    RTSPClientExt *mClient;
    BufferSink *mSink;
};

#endif //_BIT_STREAM_RTSP_READER__
