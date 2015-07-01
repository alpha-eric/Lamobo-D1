/*
 * @file IAudioSink.h
 * @brief declare CAudioSink class
 *
 * Copyright (C) 2012 Anyka (GuangZhou) Software Technology Co., Ltd.
 * @author Du Hongguang
 * @date 2012-7-18
 * @version 1.0
 */

#include <sys/time.h>
#include <time.h>
#include <sys/stat.h>
#include <stdio.h>
#include "AudioSink.h"
#include "platform_cb.h"

using namespace std;

#define MAX_VOLUME_FOR_AK98         0xa186   /* -4.00dB                  */
#define MAX_VOLUME_FOR_AK39         0xa186   /* -4.00dB                  */

#define MAX_VOLUME         MAX_VOLUME_FOR_AK39  

#define CONFIG_DRIVER_PERIOD_TIME   16       /* 配置驱动Period的播放时间 */
#define AK98_MIN_PERIOD_SIZE        128      /* 最小Reriod的大小         */
#define CONFIG_DRIVER_DURATION_TIME 256

namespace akmedia {

    /*
     * 将单声道音频转化为立体声
     * 参数：
     * mono_buf:单声道音频数据buf
     * frames:  要转化的帧数
     * stero_buf:立体声数据buf
     * bpf: 每帧音频字节数
     */
    int mono2stereo(const unsigned char *mono_buf, 
                           unsigned long frames, 
                           unsigned char *stereo_buf, 
                           int bpf)
    {
        int i, j;

        i = j = 0;
        switch(bpf)
        {
            case 1:
                for(; frames > 0; frames--)
                {
                    stereo_buf[i++] = mono_buf[j];
                    stereo_buf[i++] = mono_buf[j];
                    j++;
                }
                break;

            case 2:
                for(; frames > 0; frames--)
                {
                    stereo_buf[i++] = mono_buf[j];
                    stereo_buf[i++] = mono_buf[j+1];
                    stereo_buf[i++] = mono_buf[j];
                    stereo_buf[i++] = mono_buf[j+1];
                    j += 2;
                }
                break;

            case 3:
                for(; frames > 0; frames--)
                {
                    stereo_buf[i++] = mono_buf[j];
                    stereo_buf[i++] = mono_buf[j+1];
                    stereo_buf[i++] = mono_buf[j+2];
                    stereo_buf[i++] = mono_buf[j];
                    stereo_buf[i++] = mono_buf[j+1];
                    stereo_buf[i++] = mono_buf[j+2];
                    j += 3;
                }
                break;
            case 4:
                for(; frames > 0; frames--)
                {
                    stereo_buf[i++] = mono_buf[j];
                    stereo_buf[i++] = mono_buf[j+1];
                    stereo_buf[i++] = mono_buf[j+2];
                    stereo_buf[i++] = mono_buf[j+3];
                    stereo_buf[i++] = mono_buf[j];
                    stereo_buf[i++] = mono_buf[j+1];
                    stereo_buf[i++] = mono_buf[j+2];
                    stereo_buf[i++] = mono_buf[j+3];
                    j += 4;
                }
                break;
            default:
                printf("%s: Unsupport bits-per-sample: %i\n", __func__, bpf);
                return -1;
        }
        return frames;
    }

    /*
     * 构造函数
     */
    CAudioSink::CAudioSink( )
    {
        startTimeMs = 0;
        mAudioOutputSize = 0;
        playback_handle = NULL;
        audio_filter = NULL;
        currentEq = _SD_EQ_MODE_NORMAL;
        bSynTimeValid = false;
        dSampleRate = 0;
        dChannelCount = 0;
        dBitsPerSample = 0;
        m_volume = 500;
        pthread_mutex_init(&mutex, NULL);
    }

    /*
     * 析构函数
     */
    CAudioSink::~CAudioSink()
    {
        pthread_mutex_destroy(&mutex);
    }

    /*  
     * 确认音频设备是事打开
     * 参数：
     * 无
     * 返回值：
     * true:音频设备已经打开
     * false:音频设备尚未打开
     */
    bool CAudioSink::isOpened() 
    {
        if(playback_handle != NULL)
            return true;
        else
            return false;
    }

    /*
     * 设置音量大小
     * 参数:
     * volume:音量值，范围从0到1024
     * 返回值:
     * 无
     */
    void CAudioSink::setVolume(int volume)
    {
        pthread_mutex_lock(&mutex);
        m_volume = volume;
        pthread_mutex_unlock(&mutex);
    }

    /*
     * 获取当前音量大小
     * 参数：
     * 无
     * 返回值：
     * 当前的音量大小
     */
    int CAudioSink::getVolume()
    {
        int vol = 0;

        pthread_mutex_lock(&mutex);
        vol = m_volume;
        pthread_mutex_unlock(&mutex);
        
        return vol;
    }

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
    int CAudioSink::open(int sampleRate, int channelCount, int bitsPerSample)
    {
        int rc = 0;
        snd_pcm_uframes_t buffer_size = 0;
        snd_pcm_uframes_t period_size;  
        snd_pcm_uframes_t period_size_min = 0;
        snd_pcm_uframes_t period_size_max = 0;
        snd_pcm_uframes_t periods_min = 0;
        snd_pcm_uframes_t periods_max = 0;
        snd_pcm_format_t format;

        if (isOpened())
        {
            return 0;
        }

        pthread_mutex_lock(&mutex);

        dSampleRate = sampleRate;
        dChannelCount = channelCount;
        dBitsPerSample = bitsPerSample;

        /* Open the pcm device */
        if((rc = snd_pcm_open(&playback_handle, "default", SND_PCM_STREAM_PLAYBACK, 0)) < 0) 
        {
            printf("cannot open audio device %s\n", snd_strerror(rc));
            pthread_mutex_unlock(&mutex);
            return -1;
        }

        /* open the audio eq */
        openEQ();

        /* Get Initial hardware parameters */
        snd_pcm_hw_params_t *hw;
 
        snd_pcm_hw_params_alloca(&hw);
        snd_pcm_hw_params_any(playback_handle, hw);

        if(dBitsPerSample == 8)
        {
            format = SND_PCM_FORMAT_S8;
        }
        else if(dBitsPerSample == 16)
        {
            format = SND_PCM_FORMAT_S16;
        }
        else
        {
            pthread_mutex_unlock(&mutex);
            return -1;
        }

        rc = snd_pcm_hw_params_set_format(playback_handle, hw, format);
        if (rc)
        {
            printf("cannot set sample format: %s\n", snd_strerror(rc));
            pthread_mutex_unlock(&mutex);
            return -1;
        }

        rc = snd_pcm_hw_params_set_access(playback_handle, hw, SND_PCM_ACCESS_RW_INTERLEAVED);
        if(rc)
        {
            printf("cannot set access mode: %s\n", snd_strerror(rc));
            pthread_mutex_unlock(&mutex);
            return -1;
        }

        rc = snd_pcm_hw_params_set_channels(playback_handle, hw, dChannelCount);
        if (rc)
        {
            printf("cannot set %u channels: %s\n", dChannelCount, snd_strerror(rc));
            pthread_mutex_unlock(&mutex);
            return -1;
        }

        /* Set sample rate */
        unsigned rate = dSampleRate;
        rc = snd_pcm_hw_params_set_rate_near(playback_handle, hw, &rate, NULL);
        if (rc)
        {
            printf("cannot set sample rate: %s\n", snd_strerror(rc));
            pthread_mutex_unlock(&mutex);
            return -1;
        }

        rc = snd_pcm_hw_params_get_period_size_min(hw, &period_size_min, NULL);
        if (rc)
        {
            printf("cannot get minimum period_size: %s\n", snd_strerror(rc));
            pthread_mutex_unlock(&mutex);
            return -1;
        }

        rc = snd_pcm_hw_params_get_period_size_max(hw, &period_size_max, NULL);
        if (rc)
        {
            printf("cannot get maximum period_size: %s\n", snd_strerror(rc));
            pthread_mutex_unlock(&mutex);
            return -1;
        }

        rc = snd_pcm_hw_params_get_periods_min(hw, (unsigned int *)&periods_min, NULL);
        if (rc)
        {
            printf("cannot get minimum periods: %s\n", snd_strerror(rc));
            pthread_mutex_unlock(&mutex);
            return -1;
        }

        rc = snd_pcm_hw_params_get_periods_max(hw, (unsigned int *)&periods_max, NULL);
        if (rc)
        {
            printf("cannot get maximum periods: %s\n", snd_strerror(rc));
            pthread_mutex_unlock(&mutex);
            return -1;
        }
        printf("Open==>period_size[min=%ul,max=%ul]periods[min=%ul, max=%ul]\n", 
                (unsigned int)period_size_min, (unsigned int)period_size_max, 
                (unsigned int)periods_min, (unsigned int)periods_max);

        snd_pcm_sframes_t frames = CONFIG_DRIVER_PERIOD_TIME * rate / 1000;

        frames = frames - (frames % period_size_min);
        if (frames < AK98_MIN_PERIOD_SIZE)
            frames = AK98_MIN_PERIOD_SIZE;

        unsigned int periods = CONFIG_DRIVER_DURATION_TIME * rate / frames / 1000;

        printf("Open==>optimal setting frames=%u, periods=%u\n", (unsigned int)frames, periods);

        if(snd_pcm_hw_params_test_period_size(playback_handle, hw, frames, 0)
           || snd_pcm_hw_params_test_periods(playback_handle, hw, periods, 0))
        {
            printf("Open==>the optimal setting can not be satisfied\n");
            unsigned int i, k;
            int distance = 0x7fffffff;
            for(i = periods_min; i <= periods_max; i++)
            {
                for(k = period_size_min; k <= period_size_max; k++) 
                {
                    int duration_time = i * k * 1000 / rate;
                    int new_distance = abs(CONFIG_DRIVER_DURATION_TIME - duration_time);
                    if(new_distance < distance)
                    {
                        printf("Open==>%ld %d replace is replaced with %d, %d(%d, dis=%d)\n",
                                frames, periods, k, i, new_distance, distance);
                        distance = new_distance;
                        frames = k;
                        periods = i;
                    }
                }
            }
        }    

        rc = snd_pcm_hw_params_test_period_size(playback_handle, hw, frames, NULL);
        if(rc) 
        {
            printf("test period_size %ld error %s\n", frames, snd_strerror(rc));
            pthread_mutex_unlock(&mutex);
            return -1;
        }

        rc = snd_pcm_hw_params_test_periods(playback_handle, hw, periods, NULL);
        if (rc)
        {
            printf("test_periods %u error %s\n", periods, snd_strerror(rc));
            pthread_mutex_unlock(&mutex);
            return -1;
        }

        rc = snd_pcm_hw_params_set_periods(playback_handle, hw, periods, NULL);
        if (rc) 
        {
            printf("cannot set  periods=%u: %s\n", periods, snd_strerror(rc));
            pthread_mutex_unlock(&mutex);
            return -1;
        }   

        rc = snd_pcm_hw_params_set_period_size(playback_handle, hw, frames, NULL);
        if (rc) 
        {
            printf("cannot set  period_frames: %s\n", snd_strerror(rc));
            pthread_mutex_unlock(&mutex);
            return -1;
        }

        if((rc = snd_pcm_hw_params_get_period_time(hw, &period_time, NULL)) < 0)
        {
            printf("unable to get period time (%s)\n", snd_strerror(rc));
            pthread_mutex_unlock(&mutex);
            return -1;
        }

        if((rc = snd_pcm_hw_params_get_buffer_time(hw, &buffer_time, NULL)) < 0)
        {
            printf("unable to get buffer time (%s)\n", snd_strerror(rc));
            pthread_mutex_unlock(&mutex);
            return -1;
        }

        printf("Open==>period_size=%ul, periods=%ul, period_time=%ul, buffer_time=%ul\n", 
                (unsigned int)period_size, (unsigned int)periods, 
                (unsigned int)period_time, (unsigned int)buffer_time);

        /* Commit hardware parameters */
        rc = snd_pcm_hw_params (playback_handle, hw);
        if(rc < 0)
        {
            printf("cannot commit hardware parameters: %s\n", snd_strerror(rc));
            pthread_mutex_unlock(&mutex);
            return -1;
        }

        /* Get Initial software parameters */
        snd_pcm_sw_params_t *sw;

        snd_pcm_sw_params_alloca (&sw);
        snd_pcm_sw_params_current (playback_handle, sw);

        snd_pcm_get_params(playback_handle, &buffer_size, &period_size);
        printf("Open==>buffer_size=%ld, period_size=%ld\n", buffer_size, period_size);

        rc = snd_pcm_sw_params_set_start_threshold (playback_handle, sw, buffer_size);
        if(rc < 0)
        {
            printf("unable to set start threshold (%s)\n", snd_strerror(rc));
            pthread_mutex_unlock(&mutex);
            return -1;
        }

        /* Commit software parameters. */
        rc = snd_pcm_sw_params(playback_handle, sw);
        if(rc)
        {
            printf("cannot commit software parameters: %s\n", snd_strerror(rc));
            pthread_mutex_unlock(&mutex);
            return -1;
        }
        pthread_mutex_unlock(&mutex);

        return 0;
    }

    /* 
     * 关闭音频设备
     * 参数：
     * 无
     * 返回值：
     * 无
     */
    void CAudioSink::close(void)
    {
        pthread_mutex_lock(&mutex);
        snd_pcm_drop(playback_handle);
        snd_pcm_close(playback_handle);
        pthread_mutex_unlock(&mutex);

        // 同时关闭音效处理
        closeEQ();
    }

    /*
     * 启动音频播放
     * 参数:
     * 无
     * 返回值
     * 无
     */
    void CAudioSink::start(void)
    {
    	// do nothing
    }

    /*
     * 启动音频播放
     * 参数:
     * 无
     * 返回值
     * 无
     */
    void CAudioSink::pause(void)
    {
    	// do nothing
    }

    /*
     * 清空音频缓冲区里未播放的数据
     * 参数：
     * 无
     * 返回值：
     * 无
     */
    void CAudioSink::cancel(void)
    {
    	// do nothing
    }

    /*
     * 停止音频播放
     * 参数:
     * 无
     * 返回值
     * 无
     */
    void CAudioSink::stop()
    {
        pthread_mutex_lock(&mutex);
        // 将所有数据播放完，并停止
        snd_pcm_sframes_t frames = 0;
        snd_pcm_sframes_t last_frames = 0;
        int ret = 0;
        while(!ret)
        {
            ret = snd_pcm_delay(playback_handle, &frames);
            if((ret != 0) || (frames == last_frames))
            {
                break;
            }
            last_frames = frames;
            usleep(2 * period_time);
        }

        snd_pcm_drop (playback_handle);
        //snd_pcm_prepare (playback_handle);
        pthread_mutex_unlock(&mutex);
    }

    /* 
     * 输出音频数据
     * 参数：
     * buff[in]:PCM数据
     * len[in]:字节数
     * 返回值：
     * 0:成功
     * 其它：失败
     */
    int CAudioSink::render(void *buf, int len)
    {
        int i = 0;
        int temp = 0;
        int rc = 0;

        //eq
        handleEQ(buf, len);
        pthread_mutex_lock(&mutex);
        snd_pcm_uframes_t size = len / (dChannelCount * dBitsPerSample / 8);
        short *tempbuf = (short *)buf;

        for(i=0; i < len / 2; i++)
        {
            temp = (long)(tempbuf[i]);
            temp = (temp * MAX_VOLUME)>>16;
            temp = (temp * m_volume) >> 10;
            tempbuf[i] = (short)temp;
        }
		
        while(size > 0)
        {
            rc = snd_pcm_writei (playback_handle, tempbuf, size);
            if(rc == -EAGAIN)
                continue;

            if(rc < 0)
            {
                if(snd_pcm_recover(playback_handle, rc, 0) < 0)
                {
                    printf("Write error: %s\n", snd_strerror(rc));
                    pthread_mutex_unlock(&mutex);
                    return -1;
                }
                break;  /* skip one period */
            }

            tempbuf += rc * (dChannelCount * dBitsPerSample / 8) / 2;
            size -= rc;


            // Increase total audio output size
            mAudioOutputSize += rc * (dChannelCount * dBitsPerSample / 8) ;
        }

        pthread_mutex_unlock(&mutex);
        return size;
    }

   
    /*
     * 获取音频延迟时间
     * 参数：
     * 无
     * 返回值：
     * 延迟的ms数
     */
    unsigned long CAudioSink::latency(void)
    {
        snd_pcm_sframes_t frames = 0;
        int ret = 0;
        ret = snd_pcm_delay(playback_handle, &frames);
        if(ret != 0)
            ret = (long)frames * 1000 / dSampleRate;
        return ret;
    }

    /* 
     * 获取同步时间，用于音视频同步操作
     * 参数：
     * 无
     * 返回值：
     * 同步时间，单位为毫秒
     */
    unsigned long CAudioSink::getSyncTime(void)
    {
        unsigned long sync_time;

        pthread_mutex_lock(&mutex);

        sync_time = mAudioOutputSize * 1000 / (dSampleRate * 4);

        if (sync_time > latency())
            sync_time -= latency();
        else
            sync_time = 0;
        sync_time += startTimeMs;
        pthread_mutex_unlock(&mutex);

        return sync_time;
    }

    /* 
     * 设置音频起始时间，用于同步操作
     * 参数：
     * ms[in]:起始时间，单位为毫秒
     * 返回值：
     * 无
     */
    void CAudioSink::setStartTime(unsigned long ms)
    {
        pthread_mutex_lock(&mutex);
        startTimeMs = ms;
        mAudioOutputSize = 0;
        pthread_mutex_unlock(&mutex);
    }

    /*
     * 打开EQ模式
     * 参数：
     * 无
     * 返回值：
     * 0：打开成功
     * -1：打开失败
     */
    int CAudioSink::openEQ()
    {
        if (audio_filter == NULL)
        {
            T_AUDIO_FILTER_INPUT filter_input;
            memset(&filter_input, 0 ,sizeof(T_AUDIO_FILTER_INPUT));
            filter_input.cb_fun.Malloc = libMalloc;
            filter_input.cb_fun.Free = libFree;
            filter_input.cb_fun.printf = libPrintf;
            filter_input.cb_fun.delay = lnx_delay;

            filter_input.m_info.m_Type= _SD_FILTER_EQ;
            filter_input.m_info.m_SampleRate = dSampleRate;
            filter_input.m_info.m_Channels = dChannelCount;
            filter_input.m_info.m_BitsPerSample = dBitsPerSample;
            filter_input.m_info.m_Private.m_eq.eqmode = _SD_EQ_MODE_NORMAL;

            audio_filter = _SD_Filter_Open(&filter_input);
            if (audio_filter == NULL)
            {
                return -1;
            }

            currentEq = _SD_EQ_MODE_NORMAL;
        }
		
        return 0;
    }

    /*
     * 关闭EQ模式
     * 参数：
     * 无
     * 返回值：
     * 0：关闭成功
     * -1：关闭失败
     */
    int CAudioSink::closeEQ()
    {
        if(audio_filter != NULL)
        {
            T_S32 ret = _SD_Filter_Close(audio_filter);
            if (ret == AK_FALSE)
            {
                return -1;
            }

            audio_filter = NULL;

            currentEq = _SD_EQ_MODE_NORMAL;
        }

        return 0;
    }

    /*
     * 处理EQ音效
     * 参数：
     * buf[in,out]:要处理的音频数据
     * len[in]:处理的数据大小
     * 返回值：
     * 无
     */
    void CAudioSink::handleEQ(void *buf, int len)
    {
        if (currentEq != _SD_EQ_MODE_NORMAL)
        {
            T_AUDIO_FILTER_BUF_STRC buffStruct;
            buffStruct.buf_in = buf;
            buffStruct.len_in = len;
            buffStruct.buf_out = buf;
            buffStruct.len_out = len;
            _SD_Filter_Control(audio_filter, &buffStruct);
        }
    }

    /*
     * 设置音效模式
     * 参数：
     * eq[in]:音效模式
     * 返回值：
     * 0：成功
     * -1：失败
     */
    int CAudioSink::setEQ(int eq)
    {
        if (eq < _SD_EQ_MODE_NORMAL || eq >= _SD_EQ_USER_DEFINE)
        {
            return -1;
        }

        if (eq == currentEq)
        {
            return 0;
        }

        T_AUDIO_FILTER_IN_INFO in_info;
        memset(&in_info, 0 ,sizeof(T_AUDIO_FILTER_IN_INFO));
        in_info.m_Type= _SD_FILTER_EQ;
        in_info.m_SampleRate = dSampleRate;
        in_info.m_Channels = dChannelCount;
        in_info.m_BitsPerSample = dBitsPerSample;
        in_info.m_Private.m_eq.eqmode = (T_EQ_MODE)eq;

        T_S32 ret = _SD_Filter_SetParam(audio_filter, &in_info);
        if (ret == AK_FALSE)
        {
            printf("set EQ error!!\n");
            return -1;
        }

        currentEq = eq;
		
        return 0;
    }

	int CAudioSink::setAGC(int agclevel)
	{
		if (agclevel < 0 || agclevel > 32787)
        {
            return -1;
        }
		T_AUDIO_FILTER_IN_INFO in_info;
        memset(&in_info, 0 ,sizeof(T_AUDIO_FILTER_IN_INFO));

		in_info.m_Type= _SD_FILTER_AGC;
        in_info.m_SampleRate = dSampleRate;
        in_info.m_Channels = dChannelCount;
        in_info.m_BitsPerSample = dBitsPerSample;
        in_info.m_Private.m_agc.AGClevel = agclevel;

        T_S32 ret = _SD_Filter_SetParam(audio_filter, &in_info);
        if (ret == AK_FALSE)
        {
            printf("set AGC error!!\n");
            return -1;
        }
		return 0;
	}


}
// end of namespace 

