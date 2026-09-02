
#include "buffer_sink.h"

int main()
{
     start_rtsp_client();
     RTSPClient *client = openURL("192.168.1.11");
     StreamClientState& scs = ((RTSPClientExt*)client)->scs; // alias
     char buf[1024];
     BufferSink *sink = (BufferSink *)(scs.subsession->sink);
     sink->readFrameData(buf, 1024);
     return 0;
}
