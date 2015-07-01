/*
 * @FILENAME: AkAudioDecoder.h
 * @BRIEF AkAudioDecoder class header file
 *        This file provides AkAudioDecoder class definition.
 *        Copyright (C) 2010 Anyka (Guang zhou) Software Technology Co., LTD
 * @AUTHOR Du Hongguang
 * @DATA 2012-08-13
 * @VERSION 1.0
 */
#ifndef AKAUDIODECODER_H
#define AKAUDIODECODER_H

#include <pthread.h>
#include "platform_cb.h"
#include "sdcodec.h"
#include "media_demuxer_lib.h"
#include "AudioSink.h"
//#include "AkVideoDecoder.h"

namespace akmedia {

    class AkAudioDecoder
    {
        public:

            /*
             * construct
             * param:
             * audioSink: point of CAudioSink class
             * return
             */
            AkAudioDecoder(CAudioSink *audioSink);

            /*
             * deconstruct
             * param:
             * return
             */
            ~AkAudioDecoder();

            /*
             * init
             * param:
             * media_info: point of T_MEDIALIB_DMX_INFO
             * file_len: file length
             * return
             * -1 mean error, else success
             */
            int init(T_MEDIALIB_DMX_INFO* media_info, T_U32 file_len);

			/*
		     * deinit
		     * param:
			 * NONE
		     * return
		     * -1 mean error, else success
		     */
		    int deinit();
            /*
             * decodeFirstFrame
             * param:
             * outBuff
             * buffSize
             * isEnd
             * return
             * -1 mean error, 0 mean data not enough, else success
             */
            T_S32 decodeFirstFrame(T_U8 *outBuff, T_U32 buffSize, bool isEnd);

            /*
             * openAudioSink
             * param:
             * outputRate
             * return
             * -1 mean error, else success
             */
            int openAudioSink(int outputRate);

            /*
             * prepare
             * param:
             * return
             * -1 mean error, else success
             */
            int prepare();

            /*
             * start
             * param:
             * return
             * -1 mean error, else success
             */
            int start();

            /*
             * pause
             * param:
             * return
             * -1 mean error, else success
             */
            int pause();

            /*
             * stop
             * param:
             * return
             * -1 mean error, else success
             */
            int stop();

            /*
             * srcEndNotify
             * param:
             * return
             */
            void srcEndNotify();

            /*
             * complete
             * param:
             * return
             */
            void complete();

            /*
             * seekTo
             * param:
             * seekInfo
             * return
             * -1 mean error, else success
             */
            int seekTo(T_AUDIO_SEEK_INFO *seekInfo);

            /*
             * getBufferDataSize
             * param:
             * return
             * data size left in input buffer
             */
            T_U32 getBufferDataSize();

            /*
             * getBuffer
             * param:
             * size
             * return
             * input buffer write pointer
             */
            T_pDATA  getBuffer(T_U32 size);

            /*
             * pushBuffer
             * param:
             * size
             * return
             * -1 mean error, else success
             */
            int pushBuffer(T_U32 size);

            /*
             * setAudioSink
             * param:
             * audioSink
             * return
             */
            void setAudioSink(CAudioSink* audioSink);

            /*
             * isPlaying
             * param:
             * return
             * true: playing, false: not playing
             */
            inline bool isPlaying()
            {
                return !(bPause || bExit);
            }

            /*
             * getCurrentPosition
             * param:
             * msec
             * return
             * -1 mean error, else success
             */
            inline int getCurrentPosition(int *msec)
            {
                *msec = currentPosition;
                return 0;
            }

        private:
            T_U32              getBufferSize();
            int                cleanAllBuffer ();

            static  int        decodeThread(void*);
            int                audioDecode();

            T_U32              outSampleRate;
            T_U16              outChannels;
            T_U16              outBitsPerSample;

            CAudioSink*        adAudioSink;
            T_VOID*            pAudioCodec;
            T_AUDIO_DECODE_OUT audio_output;
            T_U8*              decodeOutBuff;
            T_U32              outputBuffLen;
            T_U32              outSizePerDecode;

            //for synchronization with main thread
            pthread_mutex_t    adMutex;
            pthread_cond_t     adCondition;

            volatile bool      bPause;
            volatile bool      bExit;

            volatile bool      bSrcEnd;
            pthread_t          adTid;

            volatile int       currentPosition;

            T_U32              totalBufferSize;
			int                errorTime;
    };

}

#endif

