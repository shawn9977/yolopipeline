
#ifndef _BIT_STREAM_FILE_READER__
#define _BIT_STREAM_FILE_READER__

#include "bitstream_reader.h"
#include <stdio.h>

class BitstreamFileReader : public BitstreamReader {
public :
    BitstreamFileReader();
    virtual ~BitstreamFileReader();

    BitstreamFileReader(BitstreamFileReader const&) = delete;
    BitstreamFileReader& operator=(BitstreamFileReader const&) = delete;

    virtual int       Open(const char *fileName);
    virtual int       Read(char *buffer, size_t bytesNum);
    virtual void      Reset();
    virtual void      Close();
private:
   FILE *mSource;
};

#endif //_BIT_STREAM_FILE_READER__
