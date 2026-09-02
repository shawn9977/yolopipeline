
#include "bitstream_file_reader.h"

#include <string.h>
#include <assert.h>


BitstreamFileReader::BitstreamFileReader()
    :  mSource(nullptr) {
}

BitstreamFileReader::~BitstreamFileReader() {
    Close();
}

void BitstreamFileReader::Close() {
    if(mSource) {
        fclose(mSource);
	mSource = nullptr;
    }
}

void BitstreamFileReader::Reset() {
    if (!mInitialized)
        return;

    fseek(mSource, 0, SEEK_SET);
}

int BitstreamFileReader::Open(const char *fileName) {
    int sts = 0;
    assert (fileName != NULL);
    assert(strnlen(fileName, 256) != 0);

    //open file to read input stream
    mSource = fopen(fileName, "rb");
    if (!mSource)
    {
	    printf("Open %s failed!\n", fileName);
        return -1;
    }
    mInitialized = true;

    return 0;
}

int BitstreamFileReader::Read(char *buffer, size_t bytesNum) {
    assert (buffer != NULL);
    return fread(buffer, 1, bytesNum, mSource); 
}

