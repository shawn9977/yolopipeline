#include "bitstream_rtsp_reader.h"
#include "buffer_sink.h"

#include <string.h>
#include <assert.h>
#include <chrono>

bool BitstreamRTSPReader::mLive555Initialized = false;
std::thread *BitstreamRTSPReader::mLive555ThreadId = nullptr;

BitstreamRTSPReader::BitstreamRTSPReader()
    : mClient(nullptr), mSink(nullptr) {
}

BitstreamRTSPReader::~BitstreamRTSPReader() {
    Close();
}

void BitstreamRTSPReader::Close() {
    if(mClient)
        closeStream(mClient, 1);

    mInitialized = false;
}

void BitstreamRTSPReader::Reset() {
    //RTSP can't reset
    return; 
}

int BitstreamRTSPReader::Open(const char *uri) {
    if(!mLive555Initialized) {
        BitstreamRTSPReader::mLive555ThreadId = new std::thread(start_rtsp_client); //it just have only one live555 scheduler run here 
	mLive555Initialized = true;
    }
    std::this_thread::sleep_for (std::chrono::milliseconds(100));
    mClient = openURL(uri);
    std::this_thread::sleep_for (std::chrono::milliseconds(100));
    if(!mClient) {
        printf("%s %s Open  failed!", __func__, uri);
        return -1;
    }

    if (!mClient->scs.session) {
        printf("%s Open %s failed! Please check if the URL is correct", __func__, uri);
        return -1;
    }

    StreamClientState& scs = ((RTSPClientExt*)mClient)->scs; // alias
    mSink = (BufferSink *)(scs.sink);

    mInitialized = true;

    return 0;
}

/**
 * read bitstreams from RTSP client buffer, if client buffer is empty, it will be blocked
 * until informed that new RTSP packet is received.
 *
 * @param buffer  destnation memory where to store data
 * @param bytesNum How many bytes want to read, nomally it's equal to the size of buffer
 * @return --- actual data bytes if success
 *         --- -1 if not RTSP stream is not connect
 *         --- -2 if RTSP stream is closed
 **/
int BitstreamRTSPReader::Read(char *buffer, size_t bytesNum) {
    UsageEnvironment& env = mClient->envir(); 
    StreamClientState& scs = ((RTSPClientExt*)mClient)->scs;
    if (mSink == NULL) { 
        printf("SVET app: %s RTST Stream is not connected successfully!", __func__);
    	return -1;
    }

    int ret = (int)mSink->readFrameData(buffer, bytesNum);

    return ret;
}

