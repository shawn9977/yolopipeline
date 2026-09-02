

#ifndef _BIT_STREAM__READER__
#define _BIT_STREAM__READER__

#include <stddef.h>
#include <stdint.h>

class BitstreamReader {
public :
    BitstreamReader();
    virtual ~BitstreamReader();

    virtual int       Open(const char *fileNameOrUri) = 0;
    virtual int       Read(char *buffer, size_t bytesNum) = 0;
    virtual void      Reset() = 0;
    virtual void      Close() = 0;
protected:
    bool mInitialized;
};

#endif //_BIT_STREAM__READER__
