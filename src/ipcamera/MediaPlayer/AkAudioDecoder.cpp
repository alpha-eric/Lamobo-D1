/*
 * @FILENAME: AkAudioDecoder.cpp
 * @BRIEF AkAudioDecoder class 
 *        This file provides AkAudioDecoder class definition.
 *        Copyright (C) 2010 Anyka (Guang zhou) Software Technology Co., LTD
 * @AUTHOR Du Hongguang
 * @DATA 2012-08-13
 * @VERSION 1.0
 */
#include <unistd.h>
#include <linux/sched.h>
#include <stdio.h>

#include "AkAudioDecoder.h"

const int MAX_DECODE_OUT_DATA = 256;
/*
 * 对于解码返回0的情况，需要解码10次才能确认解码完成
 */
const int MAX_AUDIO_DECODE_TIME = 10;

extern void dmx_wakeUpFromBuffWait();
extern volatile bool DecodeComplete;

namespace akmedia {

    /*
     * construct
     * param:
     * audioSink
     * return
     */
    AkAudioDecoder::AkAudioDecoder(CAudioSink *audioSink)
    {
        outSizePerDecode = 0;
        outSampleRate = 0;
        outChannels = 0;
        outBitsPerSample = 0;
        decodeOutBuff = NULL;
        outputBuffLen = 0;

        adAudioSink = audioSink;
        pAudioCodec = NULL;
        bPause = true;
        bExit = false;
        bSrcEnd = false;
        adTid = 0;

        currentPosition = 0;
        totalBufferSize = 0;

        pthread_mutex_init(&adMutex, NULL);
        pthread_cond_init(&adCondition, NULL);
    }

    /*
     * deconstruct
     * param:
     * return
     */
    AkAudioDecoder::~AkAudioDecoder()
    {
        if (decodeOutBuff != NULL)
        {
            free(decodeOutBuff);
            decodeOutBuff = NULL;
        }
        pthread_mutex_destroy(&adMutex);
        pthread_cond_destroy(&adCondition);
    }

    /*
     * setAudioSink
     * param:
     * audioSink
     * return
     */
    void AkAudioDecoder::setAudioSink(CAudioSink* audioSink)
    {
        bool isOpen = adAudioSink->isOpened();

        if (isOpen)
        {
            adAudioSink->close();
            audioSink->open(outSampleRate, outChannels, outBitsPerSample);
        }

        adAudioSink = audioSink;
        adAudioSink->setStartTime(currentPosition);
    }

    /*
     * getBufferDataSize
     * param:
     * return
     * data size left in input buffer
     */
    T_U32 AkAudioDecoder::getBufferDataSize()
    {
        return totalBufferSize - getBufferSize();
    }

    /*
     * getBufferSize
     * param:
     * return
     * input buffer size can be used
     */
    T_U32 AkAudioDecoder::getBufferSize()
    {
        //here use the old interface _SD_Buffer_Check
        T_AUDIO_BUFFER_CONTROL buffer_ctr;
        T_AUDIO_BUF_STATE buffer_state = _SD_Buffer_Check(pAudioCodec, &buffer_ctr);
        //normal
        if (_SD_BUFFER_WRITABLE == buffer_state)
        {
            return buffer_ctr.free_len;
        }
        //turn around
        else if (_SD_BUFFER_WRITABLE_TWICE == buffer_state)
        {
            return buffer_ctr.start_len + buffer_ctr.free_len;
        }
        //default
        else
        {
            return totalBufferSize;
        }
    }

    /*
     * getBuffer
     * param:
     * size
     * return
     * input buffer write pointer
     */
    T_pDATA  AkAudioDecoder::getBuffer(T_U32 size)
    {
        T_pDATA retBuff = (T_pDATA)_SD_Buffer_GetAddr(pAudioCodec, size);
        return retBuff;
    }

    /*
     * pushBuffer
     * param:
     * size
     * return
     * -1 mean error, else success
     */
    int  AkAudioDecoder::pushBuffer(T_U32 size)
    {
        if (_SD_Buffer_UpdateAddr(pAudioCodec, size) == AK_FALSE)
        {
            return -1;
        }
        return 0;
    }

    /*
     * cleanAllBuffer
     * param:
     * return
     * -1 mean error, else success
     */
    int  AkAudioDecoder::cleanAllBuffer ()
    {
        T_S32 ret = _SD_Buffer_Clear(pAudioCodec);
        if (ret == AK_FALSE)
        {
            return -1;
        }
        return 0;
    }

    /*
     * init
     * param:
     * media_info
     * file_len
     * return
     * -1 mean error, else success
     */
    int AkAudioDecoder::init(T_MEDIALIB_DMX_INFO* media_info, T_U32 file_len)
    {
        T_AUDIO_DECODE_INPUT audio_input;

        memset(&audio_input, 0, sizeof(T_AUDIO_DECODE_INPUT));
        audio_input.cb_fun.Free = libFree;
        audio_input.cb_fun.Malloc = libMalloc;
        audio_input.cb_fun.printf = libPrintf;
        audio_input.cb_fun.delay = lnx_delay;

        audio_input.m_info.m_Type = media_info->m_AudioType;
        if (_SD_MEDIA_TYPE_MIDI == audio_input.m_info.m_Type)
        {
            printf("MIDI File L = %lu.\n", file_len);
            // file in network may get file length failed.
            // now the audio decode lib support the max file length of midi format is 64 * 1024.
            if (0 == file_len)
                file_len = 64 * 1024;
            audio_input.m_info.m_Private.m_midi.nFileSize = file_len;
        }
        else if (_SD_MEDIA_TYPE_AAC == audio_input.m_info.m_Type)
        {
            //no use setting now
            //may be use in the future.
            audio_input.m_info.m_Private.m_aac.cmmb_adts_flag = 0;
        }

        audio_input.m_info.m_SampleRate = media_info->m_nSamplesPerSec;
        audio_input.m_info.m_Channels = media_info->m_nChannels;
        audio_input.m_info.m_BitsPerSample = media_info->m_wBitsPerSample;

        //must do this
        audio_input.m_info.m_szData = media_info->m_szData;
        audio_input.m_info.m_szDataLen = media_info->m_cbSize;


        memset(&audio_output, 0, sizeof(T_AUDIO_DECODE_OUT));

        pAudioCodec = _SD_Decode_Open(&audio_input, &audio_output);

        if (pAudioCodec == AK_NULL)
        {
            printf("audio decode init failed\n");
            return -1;
        }


        if (_SD_MEDIA_TYPE_MIDI == audio_input.m_info.m_Type)
        {
            _SD_SetInbufMinLen(pAudioCodec, file_len);
        }


        totalBufferSize = getBufferSize();

        return 0;
    }

    /*
     * deinit
     * param:
     * NONE
     * return
     * -1 mean error, else success
     */
    int AkAudioDecoder::deinit( )
    {
        if(adTid != 0)
            pthread_join(adTid, NULL);
        if (pAudioCodec != NULL)
        {
            _SD_Buffer_Clear(pAudioCodec);
            _SD_Decode_Close(pAudioCodec);
            pAudioCodec = NULL;
        }
        
        return 0;
    }

    /*
     * decodeFirstFrame
     * param:
     * outBuff
     * buffSize
     * isEnd
     * return
     * -1 mean error, 0 mean data not enough, else success
     */
    T_S32 AkAudioDecoder::decodeFirstFrame(T_U8 *outBuff, T_U32 buffSize, bool isEnd)
    {
        //check buffer mode
        if(isEnd)
        {
            _SD_SetBufferMode(pAudioCodec, _SD_BM_ENDING);
        }

        audio_output.m_ulSize = buffSize;
        audio_output.m_pBuffer = outBuff;

        T_S32 nSize  = 0;
        int count = 0;

        //decode 10 time until return large than 0
        do
        {
            nSize = _SD_Decode(pAudioCodec, &audio_output);
            count++;
        }while (nSize == 0 && count < MAX_AUDIO_DECODE_TIME);

        //reset to normal buffer mode
        if (isEnd)
        {
            _SD_SetBufferMode(pAudioCodec, _SD_BM_NORMAL);
        }

        audio_output.m_pBuffer = NULL;
        audio_output.m_ulSize = 0;

        if (nSize > 0)
        {
            outSizePerDecode = nSize;
            //output size may be change later, so if it smaller than 256 bytes, set to 256
            if ((int)outSizePerDecode < MAX_DECODE_OUT_DATA)
            {
                outSizePerDecode = MAX_DECODE_OUT_DATA;
            }

            //get info
            outSampleRate = audio_output.m_SampleRate;
            outChannels = audio_output.m_Channels;
            outBitsPerSample = 16;
        }

        return nSize;
    }

    /*
     * openAudioSink
     * param:
     * outputRate
     * return
     * -1 mean error, else success
     */
    int AkAudioDecoder::openAudioSink(int outputRate)
    {
        //no video
        if (outputRate <= 0)
        {
            outputBuffLen = outSizePerDecode * 5;
        }
        else
        {
            // half of video frame time
            outputBuffLen = (outSampleRate * outChannels * outBitsPerSample) / 8;
            outputBuffLen = (outputBuffLen + outputRate -1) / outputRate;
        }

        if (outputBuffLen <= 0)
        {
            printf("Isn't decode first frame, outSampleRate, outChannels and outBitsPerSample are wrong!!\n");
            return -1;
        }

        //avoid the output buffer length smaller than MAX_DECODE_OUT_DATA byte.
        if ((int)outputBuffLen < MAX_DECODE_OUT_DATA)
        {
            outputBuffLen = MAX_DECODE_OUT_DATA;
        }

        decodeOutBuff = (T_U8 *)malloc(sizeof(T_U8) * outputBuffLen);
        
        if (NULL == decodeOutBuff)
        {
            printf("malloc decodeOutBuff failed!!\n");
            return -1;
        }

        //open audio sink
        if (adAudioSink->open(outSampleRate, outChannels, outBitsPerSample) < 0)
        {
            printf("open audioSink failed!!\n");
            return -1;
        }
        return 0;
    }

    /*
     * prepare
     * param:
     * return
     * -1 mean error, else success
     */
    int AkAudioDecoder::prepare()
    {
        if(adTid != 0)
        {
            printf("Audio Decoder thread had created. Can't run again.\n");
            return -1;
        }
        //create decode thread  
        pthread_mutex_lock(&adMutex);

        pthread_attr_t attr; 
        struct sched_param priparam;

        pthread_attr_init(&attr);
        // pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
        pthread_attr_setschedpolicy(&attr, SCHED_RR);
        priparam.sched_priority = 1;
        pthread_attr_setschedparam(&attr, &priparam);
        int result = pthread_create(&adTid, &attr,(pthread_entry)decodeThread, this);
        pthread_attr_destroy(&attr);
        
        if (result != 0) {
            printf("create audioDecoder thread failed.\n");
            pthread_mutex_unlock(&adMutex);
            return -1;
        }

        //pthread_cond_wait(&adCondition, &adMutex);

        pthread_mutex_unlock(&adMutex);

        return 0;
    }

    /*
     * start
     * param:
     * return
     * -1 mean error, else success
     */
    int  AkAudioDecoder::start ()
    {
        pthread_mutex_lock(&adMutex);
        bPause = false;
        pthread_cond_signal(&adCondition);
        pthread_mutex_unlock(&adMutex);
        
        //start audio sink
        adAudioSink->start();

        return 0;
    }

    /*
     * pause
     * param:
     * return
     * -1 mean error, else success
     */
    int  AkAudioDecoder::pause()
    {
        pthread_mutex_lock(&adMutex);
        bPause = true;
        pthread_mutex_unlock(&adMutex);
        
        adAudioSink->pause();

        return 0;
    }

    /*
     * stop
     * param:
     * return
     * -1 mean error, else success
     */
    int  AkAudioDecoder::stop()
    {
        pthread_mutex_lock(&adMutex);
        bPause = false;
        bExit= true;
        pthread_cond_signal(&adCondition);
        pthread_mutex_unlock(&adMutex);
        return 0;
    }

    /*
     * srcEndNotify
     * param:
     * return
     */
    void AkAudioDecoder::srcEndNotify()
    {
        pthread_mutex_lock(&adMutex);
        bSrcEnd = true;
        pthread_mutex_unlock(&adMutex);
    }

    /*
     * complete
     * param:
     * return
     */
    void  AkAudioDecoder::complete ()
    {
        pthread_mutex_lock(&adMutex);
        bPause = true;
        pthread_mutex_unlock(&adMutex);
    }

    /*
     * seekTo
     * param:
     * seekInfo
     * isEnd
     * return
     * -1 mean error, else success
     */
    int AkAudioDecoder::seekTo(T_AUDIO_SEEK_INFO *seekInfo)
    {
        if (AK_NULL == seekInfo)
        {
            printf("seekInfo is NULL\n");
            return -1;
        }

        pthread_mutex_lock(&adMutex);
        //audio lib seek
        _SD_Decode_Seek(pAudioCodec, seekInfo);
        //clean buffer
        cleanAllBuffer();

        //set syn time
        currentPosition = seekInfo->real_time;
        adAudioSink->setStartTime(currentPosition);
        
		if(adAudioSink && adAudioSink->isOpened())
		{
            adAudioSink->cancel();
            adAudioSink->start();
		    adAudioSink->setSynTimeValid(true);
		}
		
        pthread_mutex_unlock(&adMutex);
		
        return 0;
    }

    /*
     * decodeThread
     * param:
     * p: calss AkAudioDecoder
     * return
     * -1 mean error, else success
     */
    int  AkAudioDecoder::decodeThread(void* p)
    {
        return ((AkAudioDecoder*)p)->audioDecode();
    }
    
    /*
     * audioDecode
     * param:
     * return
     * -1 mean error, else success
    */
   
    int  AkAudioDecoder::audioDecode()
    {
        printf("Audio Decoder thread run.\n");
        
        // Notify Audio Decoder thread run.
        // pthread_cond_signal(&adCondition);

        while (1)
        {
            pthread_mutex_lock(&adMutex);

            if (bPause)
            {
                pthread_cond_wait(&adCondition, &adMutex);
            }

            if (bExit)
            {   
                pthread_mutex_unlock(&adMutex);
                break;
            }

            //decode
            T_U32 inputDataLen = 0;
            T_U32 outputDataLen = 0;
            T_S32 nSize = 0;
            bool isRemainsProcess = false;
            int count = 0;
            do
            {
                // compute the buffer address and length
                audio_output.m_pBuffer = decodeOutBuff + outputDataLen;
                audio_output.m_ulSize = outputBuffLen - outputDataLen;
                //get buffer data size first
                //for check the data is that push by demux
                //when decode return 0 for 10 time.
                //if data has push ,no need to wait
                inputDataLen = getBufferDataSize();
                //decode
                nSize  = _SD_Decode(pAudioCodec, &audio_output);

                //entry remains data process
                if (nSize == 0 && bSrcEnd && !isRemainsProcess)
                {
                    isRemainsProcess = true;
                    //The left data may be not enough one frame, but remain need to decode.
                    _SD_SetBufferMode(pAudioCodec, _SD_BM_ENDING);
                    continue;
                }

                // normal
                if (nSize > 0)
                {
                    // the output size may be change in decode stage, fix the size here
                    if (nSize > (int)outSizePerDecode)
                    {
                        printf("outSizePerDecode %lu is smaller than nSize %ld\n", outSizePerDecode, nSize);
                        outSizePerDecode = nSize;
                    }
                    errorTime = 0;
                    outputDataLen += nSize;
                }
                else if (isRemainsProcess) //complete
                {
                    // audio decode lib have a bug, if source is read to end, and decode return 0, continue decode until 10 time.
                    if (nSize == 0 && count < 10)
                    {
                        count++;
                        continue;
                    }
                    nSize = 0;
                    break;
                }
                else if (nSize == 0 && count < MAX_AUDIO_DECODE_TIME) //continue decode may be decode sucess
                {
                    count++;
                    continue;
                }
                else // error
                {
                    break;
                }
            }
            while(outputDataLen + outSizePerDecode < outputBuffLen);

            //reset buffer mode
            if (isRemainsProcess)
            {
                _SD_SetBufferMode(pAudioCodec, _SD_BM_NORMAL);
            }

            if (nSize < 0)   //ERROR handle
            {
                errorTime++;
                if (errorTime > 5)
                {
                    //error
                    printf("audioDecode thread decode error\n");
                    bPause = true;
                    pthread_mutex_unlock(&adMutex);
                    continue;
                }
            }
            else if (0 == nSize && bSrcEnd && 0 == outputDataLen)   // complete handle
            {
                printf("audioDecode thread complete\n");
            
                adAudioSink->stop();
                adAudioSink->setSynTimeValid(false);
                bPause = true;
				DecodeComplete = true;
                pthread_mutex_unlock(&adMutex);
                continue;
            }
            else if (!bSrcEnd && 0 == nSize && 0 == outputDataLen)   //not enough one frame
            {
                //ask the demuxer to provide data
                dmx_wakeUpFromBuffWait();
                pthread_mutex_unlock(&adMutex);
                continue;
            }

            //render
            if (outputDataLen > 0)
            {
                adAudioSink->render(decodeOutBuff, outputDataLen);
                currentPosition = adAudioSink->getSyncTime();
                dmx_wakeUpFromBuffWait();
            }

            pthread_mutex_unlock(&adMutex);
        }

        //exit the thread
        pthread_mutex_lock(&adMutex);
        adTid= 0;
        pthread_cond_signal(&adCondition);
        pthread_mutex_unlock(&adMutex);

        printf("Audio Decoder thread exit!\n");
        return 0;
    }

}
