#include <jni.h>
#include <string>
#include <SLES/OpenSLES.h>
#include <SLES/OpenSLES_Android.h>
#include "native-lib.h"


#define CONV16BIT 32768
#define CONVMYFLT (1./32768.)


static void *createThreadLock(void);

static void waitThreadLock(void *lock);

static void notifyThreadLock(void *lock);

static void destroyThreadLock(void *lock);

static void bqPlayerCallback(SLAndroidSimpleBufferQueueItf bq, void *context);

static void bqRecorderCallback(SLAndroidSimpleBufferQueueItf bq, void *context);

static SLresult openSLRecOpen(opensl_stream_t *p);

static SLresult openSLPlayOpen(opensl_stream_t *p);

#define BUFFERFRAMES 1024
#define VECSAMPS_MONO 64
#define VECSAMPS_STEREO 128
#define SAMPLE_RATE 44100


#ifdef __cplusplus
extern "C" {
#endif


/*
 * creates the OpenSL ES audio engine
 */
static SLresult openSLCreateEngine(opensl_stream_t *p) {
    SLresult result;


    // create engine
    result = slCreateEngine(&(p->engineObject), 0, NULL, 0, NULL, NULL);
    if (result != SL_RESULT_SUCCESS) goto engine_end;

    // realize the engine
    result = (*p->engineObject)->Realize(p->engineObject, SL_BOOLEAN_FALSE);
    if (result != SL_RESULT_SUCCESS) goto engine_end;

    // get the engine interface, which is needed in order to create other objects
    result = (*p->engineObject)->GetInterface(p->engineObject, SL_IID_ENGINE, &(p->engineEngine));
    if (result != SL_RESULT_SUCCESS) goto engine_end;

    engine_end:
    return result;

}


// close the OpenSL IO and destroy the audio engine
static void openSLDestroyEngine(opensl_stream_t *p) {


    // destroy buffer queue audio player object, and invalidate all associated interfaces
    if (p->bqPlayerObject != NULL) {
        (*p->bqPlayerObject)->Destroy(p->bqPlayerObject);
        p->bqPlayerObject = NULL;
        p->bqPlayerPlay = NULL;
        p->bqPlayerBufferQueue = NULL;
        p->bqPlayerEffectSend = NULL;
    }

    // destroy audio recorder object, and invalidate all associated interfaces
    if (p->recorderObject != NULL) {
        (*p->recorderObject)->Destroy(p->recorderObject);
        p->recorderObject = NULL;
        p->recorderRecord = NULL;
        p->recorderBufferQueue = NULL;
    }

    // destroy output mix object, and invalidate all associated interfaces
    if (p->outputMixObject != NULL) {
        (*p->outputMixObject)->Destroy(p->outputMixObject);
        p->outputMixObject = NULL;
    }

    // destroy engine object, and invalidate all associated interfaces
    if (p->engineObject != NULL) {
        (*p->engineObject)->Destroy(p->engineObject);
        p->engineObject = NULL;
        p->engineEngine = NULL;
    }

}

// shut down the native audio system
void android_CloseAudioDevice(opensl_stream_t *p) {

    if (p == NULL)
        return;

    openSLDestroyEngine(p);

    if (p->inlock != NULL) {
        notifyThreadLock(p->inlock);
        destroyThreadLock(p->inlock);
        p->inlock = NULL;
    }

    if (p->outlock != NULL) {
        notifyThreadLock(p->outlock);
        destroyThreadLock(p->outlock);
        p->inlock = NULL;
    }

    if (p->outputBuffer[0] != NULL) {
        free(p->outputBuffer[0]);
        p->outputBuffer[0] = NULL;
    }

    if (p->outputBuffer[1] != NULL) {
        free(p->outputBuffer[1]);
        p->outputBuffer[1] = NULL;
    }

    if (p->inputBuffer[0] != NULL) {
        free(p->inputBuffer[0]);
        p->inputBuffer[0] = NULL;
    }

    if (p->inputBuffer[1] != NULL) {
        free(p->inputBuffer[1]);
        p->inputBuffer[1] = NULL;
    }

    free(p);
}

/*
  Open the audio device with a given sampling rate, input and
  output channels and IO buffer size in frames.
  Returns a handle to the OpenSL stream
*/
opensl_stream_t *android_OpenAudioDevice(
        int sample_rate,
        int inchannels,
        int outchannels,
        int bufferframes) {

    opensl_stream_t *p;
    p = (opensl_stream_t *) calloc(sizeof(opensl_stream_t), (size_t) 1);

    p->inchannels = inchannels;
    p->outchannels = outchannels;
    p->sample_rate = sample_rate;
    p->inlock = createThreadLock();
    p->outlock = createThreadLock();

    if ((p->outBufSamples = bufferframes * outchannels) != 0) {

        if ((p->outputBuffer[0] = (short *) calloc((size_t) p->outBufSamples, sizeof(short))) == NULL ||
            (p->outputBuffer[1] = (short *) calloc((size_t) p->outBufSamples, sizeof(short))) == NULL) {
            android_CloseAudioDevice(p);
            return NULL;
        }

    }

    if ((p->inBufSamples = bufferframes * inchannels) != 0) {
        if ((p->inputBuffer[0] = (short *) calloc((size_t) p->inBufSamples, sizeof(short))) == NULL ||
            (p->inputBuffer[1] = (short *) calloc((size_t) p->inBufSamples, sizeof(short))) == NULL) {
            android_CloseAudioDevice(p);
            return NULL;
        }
    }

    p->currentInputIndex = 0;
    p->currentOutputBuffer = 0;
    p->currentInputIndex = p->inBufSamples;
    p->currentInputBuffer = 0;

    if (openSLCreateEngine(p) != SL_RESULT_SUCCESS) {
        android_CloseAudioDevice(p);
        return NULL;
    }

    if (openSLRecOpen(p) != SL_RESULT_SUCCESS) {
        android_CloseAudioDevice(p);
        return NULL;
    }

    if (openSLPlayOpen(p) != SL_RESULT_SUCCESS) {
        android_CloseAudioDevice(p);
        return NULL;
    }

    notifyThreadLock(p->outlock);
    notifyThreadLock(p->inlock);

    p->time = 0.;
    return p;
}


/*
 * opens the OpenSL ES device for output
 * source : une buffer queue au format pcm, which is where we will send our audio
 * data samples.
 * sink : un output mix
 */
static SLresult openSLPlayOpen(opensl_stream_t *p) {
    SLresult result;
    SLuint32 sample_rate = (SLuint32) p->sample_rate;
    SLuint32 channels = (SLuint32) p->outchannels;

    if (channels) {
        // configure audio source
        SLDataLocator_AndroidSimpleBufferQueue loc_bufq =
                {SL_DATALOCATOR_ANDROIDSIMPLEBUFFERQUEUE, 2};

        switch (sample_rate) {

            case 8000:
                sample_rate = SL_SAMPLINGRATE_8;
                break;
            case 11025:
                sample_rate = SL_SAMPLINGRATE_11_025;
                break;
            case 16000:
                sample_rate = SL_SAMPLINGRATE_16;
                break;
            case 22050:
                sample_rate = SL_SAMPLINGRATE_22_05;
                break;
            case 24000:
                sample_rate = SL_SAMPLINGRATE_24;
                break;
            case 32000:
                sample_rate = SL_SAMPLINGRATE_32;
                break;
            case 44100:
                sample_rate = SL_SAMPLINGRATE_44_1;
                break;
            case 48000:
                sample_rate = SL_SAMPLINGRATE_48;
                break;
            case 64000:
                sample_rate = SL_SAMPLINGRATE_64;
                break;
            case 88200:
                sample_rate = SL_SAMPLINGRATE_88_2;
                break;
            case 96000:
                sample_rate = SL_SAMPLINGRATE_96;
                break;
            case 192000:
                sample_rate = SL_SAMPLINGRATE_192;
                break;
            default:
                return (SLresult) -1;
        }

        const SLInterfaceID ids[] = {SL_IID_VOLUME};
        const SLboolean req[] = {SL_BOOLEAN_FALSE};
        result = (*p->engineEngine)->CreateOutputMix(
                p->engineEngine, // the engine
                &(p->outputMixObject), // the objectif
                1, ids, req);

        if (result != SL_RESULT_SUCCESS) return result;

        // realize the output mix
        result = (*p->outputMixObject)->Realize(p->outputMixObject, SL_BOOLEAN_FALSE);

        int speakers;
        if (channels > 1)
            speakers = SL_SPEAKER_FRONT_LEFT | SL_SPEAKER_FRONT_RIGHT;
        else
            speakers = SL_SPEAKER_FRONT_CENTER;

        SLDataFormat_PCM format_pcm = {
                SL_DATAFORMAT_PCM,
                channels,
                sample_rate,
                SL_PCMSAMPLEFORMAT_FIXED_16,
                SL_PCMSAMPLEFORMAT_FIXED_16,
                (SLuint32) speakers,
                SL_BYTEORDER_LITTLEENDIAN};

        SLDataSource audioSrc = {&loc_bufq, &format_pcm};

        // configure audio sink
        SLDataLocator_OutputMix loc_outmix = {SL_DATALOCATOR_OUTPUTMIX, p->outputMixObject};
        SLDataSink audioSnk = {&loc_outmix, NULL};

        // create audio player
        const SLInterfaceID ids1[] = {SL_IID_ANDROIDSIMPLEBUFFERQUEUE};
        const SLboolean req1[] = {SL_BOOLEAN_TRUE};
        result = (*p->engineEngine)->CreateAudioPlayer(
                p->engineEngine,
                &(p->bqPlayerObject),
                &audioSrc,
                &audioSnk,
                1,
                ids1,
                req1);

        if (result != SL_RESULT_SUCCESS) goto end_openaudio;

        // realize the player
        result = (*p->bqPlayerObject)->Realize(p->bqPlayerObject, SL_BOOLEAN_FALSE);
        if (result != SL_RESULT_SUCCESS) goto end_openaudio;

        // get the play interface
        result = (*p->bqPlayerObject)->GetInterface(p->bqPlayerObject,
                                                    SL_IID_PLAY,
                                                    &(p->bqPlayerPlay));
        if (result != SL_RESULT_SUCCESS) goto end_openaudio;

        // get the buffer queue interface
        result = (*p->bqPlayerObject)->GetInterface(p->bqPlayerObject,
                                                    SL_IID_ANDROIDSIMPLEBUFFERQUEUE,
                                                    &(p->bqPlayerBufferQueue));
        if (result != SL_RESULT_SUCCESS) goto end_openaudio;

        // register callback on the buffer queue
        // The OpenSL API provides a callback mechanism for audio IO
        // the callback is only use to signal the application, indicating that the
        // buffer queue is ready to receive data.
        result = (*p->bqPlayerBufferQueue)->RegisterCallback(p->bqPlayerBufferQueue,
                                                             bqPlayerCallback,
                                                             p);
        if (result != SL_RESULT_SUCCESS) goto end_openaudio;

        // set the player's state to playing
        result = (*p->bqPlayerPlay)->SetPlayState(p->bqPlayerPlay, SL_PLAYSTATE_PLAYING);

        end_openaudio:
        return result;
    }
    return SL_RESULT_SUCCESS;
}


/* Open the OpenSL ES device for input
* source : l'entrée audio du device Android
* sink : la buffer queue
*/
static SLresult openSLRecOpen(opensl_stream_t *p) {

    SLresult result;
    SLuint32 sample_rate = (SLuint32) p->sample_rate;
    SLuint32 channels = (SLuint32) p->inchannels;

    if (channels) {

        switch (sample_rate) {

            case 8000:
                sample_rate = SL_SAMPLINGRATE_8;
                break;
            case 11025:
                sample_rate = SL_SAMPLINGRATE_11_025;
                break;
            case 16000:
                sample_rate = SL_SAMPLINGRATE_16;
                break;
            case 22050:
                sample_rate = SL_SAMPLINGRATE_22_05;
                break;
            case 24000:
                sample_rate = SL_SAMPLINGRATE_24;
                break;
            case 32000:
                sample_rate = SL_SAMPLINGRATE_32;
                break;
            case 44100:
                sample_rate = SL_SAMPLINGRATE_44_1;
                break;
            case 48000:
                sample_rate = SL_SAMPLINGRATE_48;
                break;
            case 64000:
                sample_rate = SL_SAMPLINGRATE_64;
                break;
            case 88200:
                sample_rate = SL_SAMPLINGRATE_88_2;
                break;
            case 96000:
                sample_rate = SL_SAMPLINGRATE_96;
                break;
            case 192000:
                sample_rate = SL_SAMPLINGRATE_192;
                break;
            default:
                return (SLresult) -1;
        }

        // configure audio source

#if 0
        SLEngineItf EngineItf;
        SLAudioIODeviceCapabilitiesItf AudioIODeviceCapabilitiesItf;
        SLAudioInputDescriptor AudioInputDescriptor;
        SLint32 numInputs = 0;
        SLuint32 InputDeviceIDs[3];
        SLuint32 mic_deviceID = 0;
        SLboolean mic_available = SL_BOOLEAN_FALSE;
        SLboolean required[3];
        SLInterfaceID iidArray[3];
        SLDeviceVolumeItf devicevolumeItf;
        SLDataSource audioSource;
        SLDataLocator_IODevice locator_mic;
        SLresult res;

        // Get the SL Engine Interface which is implicit
        res = (*p->engineObject)->GetInterface(p->engineObject,
                                               SL_IID_ENGINE,
                                               (void*)&EngineItf);

        // Get the Audio IO DEVICE CAPABILITIES interface, which is also implicit
        res = (*p->engineObject)->GetInterface(p->engineObject,
                                                        SL_IID_AUDIOIODEVICECAPABILITIES,
                                                        (void *) &AudioIODeviceCapabilitiesItf);

        numInputs = 3;
        res = (*AudioIODeviceCapabilitiesItf)->GetAvailableAudioInputs(AudioIODeviceCapabilitiesItf,
                                                                       &numInputs,
                                                                       InputDeviceIDs);

        // Search for either earpiece microphone or headset microphone input device - with a preference for the latter
        int i;
        for (i = 0; i < numInputs; i++) {
            res = (*AudioIODeviceCapabilitiesItf)->QueryAudioInputCapabilities(
                    AudioIODeviceCapabilitiesItf,
                    InputDeviceIDs[i],
                    &AudioInputDescriptor);

            if ((AudioInputDescriptor.deviceConnection == SL_DEVCONNECTION_ATTACHED_WIRED) &&
                (AudioInputDescriptor.deviceScope == SL_DEVSCOPE_USER) &&
                (AudioInputDescriptor.deviceLocation ==
                 SL_DEVLOCATION_HEADSET)) {
                mic_deviceID = InputDeviceIDs[i];
                mic_available = SL_BOOLEAN_TRUE;
                break;
            } else if ((AudioInputDescriptor.deviceConnection ==
                        SL_DEVCONNECTION_INTEGRATED) &&
                       (AudioInputDescriptor.deviceScope ==
                        SL_DEVSCOPE_USER) &&
                       (AudioInputDescriptor.deviceLocation ==
                        SL_DEVLOCATION_HANDSET)) {
                mic_deviceID = InputDeviceIDs[i];
                mic_available = SL_BOOLEAN_TRUE;
                break;
            }
        }

        // If neither of the preferred input audio devices is available, no point in continuing
        if (!mic_available) {
            exit(1);
        }

        // Initialize arrays required[] and iidArray[]
        for (i = 0; i < 3; i++) {
            required[i] = SL_BOOLEAN_FALSE;
            iidArray[i] = SL_IID_NULL;
        }

        // Get the optional DEVICE VOLUME interface from the engine
        res = (*p->engineObject)->GetInterface(p->engineObject,
                                               SL_IID_DEVICEVOLUME,
                                               (void *) &devicevolumeItf);

        // Set recording volume of the microphone to -3 dB
        res = (*devicevolumeItf)->SetVolume(devicevolumeItf, mic_deviceID, -300);


        // Setup the data source structure
        locator_mic.locatorType = SL_DATALOCATOR_IODEVICE;
        locator_mic.deviceType = SL_IODEVICE_AUDIOINPUT;
        locator_mic.deviceID = mic_deviceID;
        locator_mic.device= NULL;
        audioSource.pLocator = (void *)&locator_mic;
        audioSource.pFormat = NULL;

#else

        // get the record interface
        SLDataLocator_IODevice loc_dev = {SL_DATALOCATOR_IODEVICE,
                                          SL_IODEVICE_AUDIOINPUT,
                                          SL_DEFAULTDEVICEID_AUDIOINPUT,
                                          NULL};
        SLDataSource audioSource = {&loc_dev, NULL};

#endif

        // configure audio sink
        int speakers;
        if (channels > 1)
            speakers = SL_SPEAKER_FRONT_LEFT | SL_SPEAKER_FRONT_RIGHT;
        else
            speakers = SL_SPEAKER_FRONT_CENTER;

        SLDataLocator_AndroidSimpleBufferQueue loc_bq = {
                SL_DATALOCATOR_ANDROIDSIMPLEBUFFERQUEUE, 2};

        SLDataFormat_PCM format_pcm = {
                SL_DATAFORMAT_PCM,
                channels,
                sample_rate,
                SL_PCMSAMPLEFORMAT_FIXED_16,
                SL_PCMSAMPLEFORMAT_FIXED_16,
                (SLuint32) speakers,
                SL_BYTEORDER_LITTLEENDIAN};

        SLDataSink audioSnk = {&loc_bq, &format_pcm};

        // create audio recorder
        // (requires the RECORD_AUDIO permission)
        const SLInterfaceID id[1] = {SL_IID_ANDROIDSIMPLEBUFFERQUEUE};
        const SLboolean req[1] = {SL_BOOLEAN_TRUE};
        result = (*p->engineEngine)->CreateAudioRecorder(p->engineEngine,
                                                         &(p->recorderObject),
                                                         &audioSource,
                                                         &audioSnk,
                                                         1, id, req);
        if (SL_RESULT_SUCCESS != result) goto end_recopen;

        // realize the audio recorder
        result = (*p->recorderObject)->Realize(p->recorderObject, SL_BOOLEAN_FALSE);
        if (SL_RESULT_SUCCESS != result) goto end_recopen;

        // get the record interface
        result = (*p->recorderObject)->GetInterface(p->recorderObject,
                                                    SL_IID_RECORD,
                                                    &(p->recorderRecord));
        if (SL_RESULT_SUCCESS != result) goto end_recopen;

        // get the buffer queue interface
        result = (*p->recorderObject)->GetInterface(p->recorderObject,
                                                    SL_IID_ANDROIDSIMPLEBUFFERQUEUE,
                                                    &(p->recorderBufferQueue));
        if (SL_RESULT_SUCCESS != result) goto end_recopen;

        // register callback on the buffer queue
        result = (*p->recorderBufferQueue)->RegisterCallback(
                p->recorderBufferQueue,
                bqRecorderCallback,
                p);

        if (SL_RESULT_SUCCESS != result) goto end_recopen;

        // start recording
        result = (*p->recorderRecord)->SetRecordState(
                p->recorderRecord,
                SL_RECORDSTATE_RECORDING);

        end_recopen:
        return result;
    } else
        return SL_RESULT_SUCCESS;

}


// this callback handler is called every time a buffer finishes recording
void bqRecorderCallback(SLAndroidSimpleBufferQueueItf bq, void *context) {
    opensl_stream_t *p = (opensl_stream_t *) context;
    notifyThreadLock(p->inlock);
}


// this callback handler is called every time a buffer finishes playing
//  it notifies our main processing thread that the buffer queue is ready
void bqPlayerCallback(SLAndroidSimpleBufferQueueItf bq, void *context) {
    opensl_stream_t *p = (opensl_stream_t *) context;
    notifyThreadLock(p->outlock);
}


/*
Read a buffer from the OpenSL stream *p, of size samples.
Returns the number of samples read.
*/
int android_AudioIn(opensl_stream_t *p, float *buffer, int size) {
    short *inBuffer;
    int i, bufsamps = p->inBufSamples, index = p->currentInputIndex;
    if (bufsamps == 0) return 0;

    inBuffer = p->inputBuffer[p->currentInputBuffer];

    // processing loop that calls the audio input function to get a block of samples
    for (i = 0; i < size; i++) {
        if (index >= bufsamps) {
            waitThreadLock(p->inlock);
            (*p->recorderBufferQueue)->Enqueue(p->recorderBufferQueue,
                                               inBuffer,
                                               bufsamps * sizeof(short));
            p->currentInputBuffer = (p->currentInputBuffer ? 0 : 1);
            index = 0;
            inBuffer = p->inputBuffer[p->currentInputBuffer];
        }
        buffer[i] = (float) ((float) inBuffer[index++] * CONVMYFLT);
    }
    p->currentInputIndex = index;
    if (p->outchannels == 0)
        p->time += (double) size / (p->sample_rate * p->inchannels);
    return i;
}


/*
Write a buffer to the OpenSL stream *p, of size samples.
Returns the number of samples written.
*/
int android_AudioOut(opensl_stream_t *p, float *buffer, int size) {

    short *outBuffer;
    int i, bufsamps = p->outBufSamples, index = p->currentOutputIndex;
    if (bufsamps == 0) return 0;
    outBuffer = p->outputBuffer[p->currentOutputBuffer];

    for (i = 0; i < size; i++) {
        outBuffer[index++] = (short) (buffer[i] * CONV16BIT);
        if (index >= p->outBufSamples) {
            waitThreadLock(p->outlock);
            (*p->bqPlayerBufferQueue)->Enqueue(p->bqPlayerBufferQueue,
                                               outBuffer, bufsamps * sizeof(short));
            p->currentOutputBuffer = (p->currentOutputBuffer ? 0 : 1);
            index = 0;
            outBuffer = p->outputBuffer[p->currentOutputBuffer];
        }
    }
    p->currentOutputIndex = index;
    p->time += (double) size / (p->sample_rate * p->outchannels);
    return i;
}




/*

Java_com_example_alex_testaudio_MainActivity_shutdownEngine(JNIEnv* env, jclass clazz)
 */
// close the android audio device





//----------------------------------------------------------------------
// thread Locks
// to ensure synchronisation between callbacks and processing code
void *createThreadLock(void) {
    threadLock *p;
    p = (threadLock *) malloc(sizeof(threadLock));
    if (p == NULL)
        return NULL;
    memset(p, 0, sizeof(threadLock));
    if (pthread_mutex_init(&(p->m), (pthread_mutexattr_t *) NULL) != 0) {
        free((void *) p);
        return NULL;
    }
    if (pthread_cond_init(&(p->c), (pthread_condattr_t *) NULL) != 0) {
        pthread_mutex_destroy(&(p->m));
        free((void *) p);
        return NULL;
    }
    p->s = (unsigned char) 1;

    return p;
}


void waitThreadLock(void *lock) {
    threadLock *p;

    p = (threadLock *) lock;
    pthread_mutex_lock(&(p->m));
    while (!p->s) {
        pthread_cond_wait(&(p->c), &(p->m));
    }
    p->s = (unsigned char) 0;
    pthread_mutex_unlock(&(p->m));
}


void notifyThreadLock(void *lock) {
    threadLock *p;
    p = (threadLock *) lock;
    pthread_mutex_lock(&(p->m));
    p->s = (unsigned char) 1;
    pthread_cond_signal(&(p->c));
    pthread_mutex_unlock(&(p->m));
}


void destroyThreadLock(void *lock) {
    threadLock *p;
    p = (threadLock *) lock;
    if (p == NULL)
        return;
    notifyThreadLock(p);
    pthread_cond_destroy(&(p->c));
    pthread_mutex_destroy(&(p->m));
    free(p);
}


static int on;

JNIEXPORT void JNICALL
Java_com_example_alex_testaudio_MainActivity_startprocess() {
    opensl_stream_t *p;
    int samps, i, j;
    float inbuffer[VECSAMPS_MONO], outbuffer[VECSAMPS_STEREO];

    p = android_OpenAudioDevice(SAMPLE_RATE, 1, 2, BUFFERFRAMES);

    if (p == NULL) return;

    on = 1;
    double sumLevel = 0;

    while (on) {
        samps = android_AudioIn(p, inbuffer, VECSAMPS_MONO);
        sumLevel = 0;
        for (i = 0, j = 0; i < samps; i++, j += 2) {
            outbuffer[j] = outbuffer[j + 1] = inbuffer[i];
            sumLevel += inbuffer[i];
        }
        android_AudioOut(p, outbuffer, samps * 2);
    }

    android_CloseAudioDevice(p);
}


JNIEXPORT void JNICALL
Java_com_example_alex_testaudio_MainActivity_stopprocess() {
    on = 0;
}


#ifdef __cplusplus
}
#endif