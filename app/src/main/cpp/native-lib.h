//
// Created by alex on 26/12/16.
//

#include <pthread.h>
#include <stdlib.h>

#ifndef TESTAUDIO_NATIVE_LIB_H
#define TESTAUDIO_NATIVE_LIB_H

typedef struct threadLock_{
    pthread_mutex_t m;
    pthread_cond_t  c;
    unsigned char   s;
} threadLock;


typedef struct opensl_stream {

    // engine interfaces
    SLObjectItf engineObject;
    SLEngineItf engineEngine;

    // output mix interfaces
    SLObjectItf outputMixObject;

    // buffer queue player interfaces
    SLObjectItf bqPlayerObject;
    SLPlayItf bqPlayerPlay;
    SLAndroidSimpleBufferQueueItf bqPlayerBufferQueue;
    SLEffectSendItf bqPlayerEffectSend;

    // recorder interfaces
    SLObjectItf recorderObject;
    SLRecordItf recorderRecord;
    SLAndroidSimpleBufferQueueItf recorderBufferQueue;

    // buffer indexes
    int currentInputIndex;
    int currentOutputIndex;

    // current buffer half (0, 1)
    int currentOutputBuffer;
    int currentInputBuffer;

    // buffers
    short *outputBuffer[2];
    short *inputBuffer[2];

    // size of buffers
    int outBufSamples;
    int inBufSamples;

    // locks
    void*  inlock;
    void*  outlock;

    double time;
    int inchannels;
    int outchannels;
    int   sample_rate;

} opensl_stream_t;

struct wavfile
{
    char        id[4];          // should always contain "RIFF"
    int     totallength;    // total file length minus 8
    char        wavefmt[8];     // should be "WAVEfmt "
    int     format;         // 16 for PCM format
    short     pcm;            // 1 for PCM format
    short     channels;       // channels
    int     frequency;      // sampling frequency, 16000 in this case
    int     bytes_per_second;
    short     bytes_by_capture;
    short     bits_per_sample;
    char        data[4];        // should always contain "data"
    int     bytes_in_data;
};


#endif //TESTAUDIO_NATIVE_LIB_H
