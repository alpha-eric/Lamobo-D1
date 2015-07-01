/*
 * @file AudioSink.h
 * @brief declare CAudioSink class
 *
 * Copyright (C) 2012 Anyka (GuangZhou) Software Technology Co., Ltd.
 * @author Du Hongguang
 * @date 2012-7-18
 * @version 1.0
 */
 
#ifndef _AUDIO_SINK_H
#define _AUDIO_SINK_H

#include <pthread.h>
#include <alsa/asoundlib.h>
#include <sdfilter.h>
#include "platform_cb.h"

namespace akmedia {

    class CAudioSink {
        public:

            /*
             * 构造函数
             */
            CAudioSink();

            /*
             * 析构函数
             */
            ~CAudioSink();

            /*
             * 打开音频设备
             * 参数：
             * sampleRate[in]:采样率
             * nChannel[in]:采样通道
             * bitsPerSample[in]:采样位数
             * 返回值：
             * 0：成功
             * 其它：失败
             */
            int open(int sampleRate, int nChannel, int bitsPerSample);

            /*  
             * 确认音频设备是事打开
             * 参数：
             * 无
             * 返回值：
             * true:音频设备已经打开
             * false:音频设备尚未打开
             */
            bool isOpened();

            /* 
             * 关闭音频设备
             * 参数：
             * 无
             * 返回值：
             * 无
             */
            void close( );

            /*
             * 清空音频缓冲区里未播放的数据
             * 参数：
             * 无
             * 返回值：
             * 无
             */
            void cancel();

            /* 
             * 输出音频数据
             * 参数：
             * buff[in]:PCM数据
             * len[in]:字节数
             * 返回值：
             * 0:成功
             * 其它：失败
             */
            int render(void *buff, int len);

            /*
             * 启动音频播放
             * 参数:
             * 无
             * 返回值
             * 无
             */
            void start(void);

            /*
             * 启动音频播放
             * 参数:
             * 无
             * 返回值
             * 无
             */
            void pause(void);

            /*
             * 停止音频播放
             * 参数:
             * 无
             * 返回值
             * 无
             */
            void stop(void);

            /*
             * 设置音量大小
             * 参数:
             * volume:音量值，范围从0到1024
             * 返回值:
             * 无
             */
            void setVolume(int volume);

            /*
             * 获取当前音量大小
             * 参数：
             * 无
             * 返回值：
             * 当前的音量大小
             */
            int  getVolume( );

            /* 
             * 设置音频起始时间，用于同步操作
             * 参数：
             * ms[in]:起始时间，单位为毫秒
             * 返回值：
             * 无
             */
            void setStartTime(unsigned long ms);

            /*
             * 获取音频延迟时间
             * 参数：
             * 无
             * 返回值：
             * 延迟的ms数
             */
            unsigned long latency(void);

            /* 
             * 获取同步时间，用于音视频同步操作
             * 参数：
             * 无
             * 返回值：
             * 同步时间，单位为毫秒
             */
            unsigned long getSyncTime(void);

            /*
             * 设置音频同步时间有效
             * 参数：
             * bValid:true为有效, false为无效
             * 返回值：
             * 无
             */
            inline void setSynTimeValid(bool bValid)
            {
                bSynTimeValid = bValid;
            }
            
            /*
             * 获取音频同步时间是否有效
             * 参数：
             * 无
             * 返回值：
             * true:有效, false无效
             */
            inline bool getSynTimeValid()
            {
                return bSynTimeValid;
            }

            /*
             * 设置音效模式
             * 参数：
             * eq[in]:音效模式
             * 返回值：
             * 0：成功
             * -1：失败
             */
            int setEQ(int eq);

			int setAGC(int agclevel);

        private:

            /*
             * 打开EQ模式
             * 返回值：
             * 0：打开成功
             * -1：打开失败
             */
            int openEQ();

		
		
			
            /*
             * 关闭EQ模式
             * 参数：
             * 无
             * 返回值：
             * 0：关闭成功
             * -1：关闭失败
             */
            int closeEQ();

            /*
             * 处理EQ音效
             * 参数：
             * buf[in,out]:要处理的音频数据
             * len[in]:处理的数据大小
             * 返回值：
             * 无
             */
            void handleEQ(void *buf, int len);

            pthread_mutex_t         mutex;               /* pcm 操作互斥量        */
            snd_pcm_t              *playback_handle;    /* pcm palyback handler  */
            T_VOID                 *audio_filter;       /* 音效处理句柄          */

            volatile unsigned long  startTimeMs;         /* 音频开始播放的时间    */
            volatile unsigned long  mAudioOutputSize;    /* 音频播放的数据大小    */
            volatile bool           bSynTimeValid;
            unsigned                period_time;         /* 每个period的播放时间  */
            unsigned                buffer_time;         /* 总的buffer播放时间    */

            unsigned int            dSampleRate;         /* 音频采样率            */
            int                     dChannelCount;       /* 音频通道数            */
            int                     dBitsPerSample;      /* 音频数据的采样位数    */
            int                     currentEq;           /* 当前的音效            */
            int                     m_volume;            /* 当前的音量大小        */
    };

}
#endif
