/* @file Player_CodecLib.c
 * @brief 
 *
 * Copyright (C) 2012 Anyka (GuangZhou) Software Technology Co., Ltd.
 * @author Du Hongguang
 * @date 2012-7-18
 * @version 1.0
 */

#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <errno.h>
#include <string.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <linux/input.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/time.h>
#include <getopt.h>
#include <execinfo.h>
#include <linux/sched.h>
#include <pthread.h>

#include <akuio.h>
#include <sdfilter.h>


#include "IFileSource.h"
#include "AudioSink.h"
#include "AkAudioDecoder.h"


using namespace std;
using namespace akmedia;


#define KBD_DEV_NAME_98 "/dev/input/event1"
#define KBD_DEV_NAME_37 "/dev/input/event2"
#define SEEK_STEP       3*1000 
#define AGC_LEVEL		1024

typedef void* (*pthread_entry)(void*);

enum
{
    RET_OK,
    RET_NO_BUFFER,
    RET_ERROR,
    RET_END,
};

int dmx_seekTo(int msec);
static int  demuxerThread();
int dmx_reviseAudioInfo();
int dmx_reviseVideoInfo();

// demux used var
static IFileSource *dSource = NULL;
//static AkVideoDecoder *dVideoDecoder = NULL;
static AkAudioDecoder *dAudioDecoder = NULL;
static CAudioSink *dAudioSink = NULL;
static T_VOID * hMedia = NULL;
static volatile bool bPause = true;
static volatile bool bExit = false;
static volatile bool bSeeking = false;
static volatile bool audioComplete = false;
volatile bool DecodeComplete = false;
static volatile bool videoComplete = false;
static pthread_t dmxTid = 0;
static T_MEDIALIB_DMX_INFO mediaInfo;
//for sync with main thread
static pthread_mutex_t dmxMutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  dmxCond  = PTHREAD_COND_INITIALIZER;
//for sync with videoDecoder and dAudioSink
static volatile bool bBufWait = false;
static pthread_mutex_t bufMutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  bufCond = PTHREAD_COND_INITIALIZER;
//static CLcdDevice *pLcd = NULL;

//short options for command
static const char * short_options = "n:e:v:r:h";
//filename for play
char file_name[256];
static int currentEq = 0;
static int currentVolume = 500;
static rotate = -1;

//long options for command
static const struct option long_options[] = {
    {"name", 1, 0, 0},
    {"eqmode", 1, 0, 0},
	{"rotate", 1, 0, 0},
    {"help", 0, 0, 0},
    {"volume", 1, 0, 0},
    {0, 0, 0, 0}
};

/*
 * 读取数据回调函数
 * 参数：
 * hFile:指向的文件指针
 * buf:  放置存储的缓冲区
 * size: 读取的数据大小
 * 返回值：
 * 实际读取到的数据大小
 */
static T_S32 libFread(T_S32 hFile, T_pVOID buf, T_S32 size)
{
    IFileSource *dataSource = (IFileSource*)hFile;
    int rtt =dataSource->read(buf, size);
    return rtt; 
}

/*
 * 写数据回调函数
 * 参数：
 * hFile:指向的文件指针
 * buf:  放置存储的缓冲区
 * size: 写入的数据大小
 * 返回值：
 * 总是返回-1
 */
static T_S32 libFwrite(T_S32 hFile, T_pVOID buf, T_S32 size)
{
    return -1;
}

/*
 * 写数据回调函数
 * 参数：
 * hFile:指向的文件指针
 * buf:  放置存储的缓冲区
 * size: 写入的数据大小
 * 返回值：
 * 实际写入的数据大小
 */
static T_S32 libFseek(T_S32 hFile, T_S32 offset, T_S32 whence)
{
    IFileSource *dataSource = (IFileSource*)hFile;
    if (dataSource->seek(offset, whence) < 0) {
        return -1;
    }

    return dataSource->tell();
}

/*
 * 获取文件的大小
 * 参数：
 * hFile:文件句柄
 * 返回值：
 * 文件的大小
 */
static T_S32 libFtell(T_S32 hFile)
{
    IFileSource *dataSource = (IFileSource*)hFile;
    return dataSource->tell();
}

/*
 * 获取平台类型
 * 参数：
 * 无
 * 返回值：
 * 平台类型
 */
T_eMEDIALIB_CHIP_TYPE CheckMach( )
{   
    char buf[256];
    int fd;
    int len;
    memset(buf, 0, 256);

    system("uname -r > /tmp/machinfo");
    fd = open("/tmp/machinfo", O_RDONLY);
    read(fd, buf, 256);
    close(fd);
    system("rm -rf /tmp/machinfo");

    len = strlen(buf);
    if(strncmp(buf + len - strlen("ak98") - 1, "ak98", 4) == 0)
    {
        printf("mach ak98.....\n");
        return MEDIALIB_CHIP_AK9802;
    }
    else if(strncmp(buf + len - strlen("ak37") - 1, "ak37", 4) == 0)
    {
        printf("mach ak37.....\n");
        return MEDIALIB_CHIP_AK3751;
    }
    return MEDIALIB_CHIP_UNKNOWN;
}
    
/*
 * 判断文件是否存在
 * 参数：
 * hFile:文件句柄
 * 返回值：
 * 存在返回1，不存在返回0
 */
static T_S32 lnx_fhandle_exist(T_S32 hFile)
{
    if (hFile == 0) {
        return 0;
    }
    IFileSource *dataSource = (IFileSource*)hFile;
    if (dataSource->tell() < 0) {
        return 0;
    }

    return 1;
}

/* 信号处理函数
 * 参数：
 * sig:信号
 * 返回值：
 * 无
 */
static void sigprocess(int sig)
{
    int ii = 0;
    void *tracePtrs[16];
    int count = backtrace(tracePtrs, 16);
    char **funcNames = backtrace_symbols(tracePtrs, count);
    for(ii = 0; ii < count; ii++)
        printf("%s\n", funcNames[ii]);
    free(funcNames);
    fflush(stderr);
    printf("signal %d caught\n", sig);
    fflush(stdout);

    exit(1);
}

/*
 * 初始化信号处理
 * 参数：
 * 无
 * 返回值：
 * 总是返回0
 */
static int sig_init(void)
{
    signal(SIGSEGV, sigprocess);
    signal(SIGINT, sigprocess);
    signal(SIGTERM, sigprocess);
    return 0;
}

/* 
 * 打印帮助文件
 * 参数：
 * 无
 * 返回值：
 * 无
 */
static void print_help(void)
{
    printf("usage:\n");
    printf("-h/--help  help\n");
    printf("-n/--name <filename> filename\n");
    printf("-e/--eqmode <eq mode>\n");
    printf("-v/--volume <volume> (0-1024)\n");
    printf("for example:\n\tmedia_player -n test.wav -e 1 -v 600 \n");
}

/*
 * 处理命令号字符串
 * 参数：
 * argc:参数数量
 * argv:参数字符串列表
 * 返回值：
 * 总是返回0
 */
static int parse_cmd(int argc, char* argv[])
{
    int c;
    bool bname = false;

    while(1)
    {
        int option_index = 0;

        c = getopt_long(argc, argv, short_options,
                long_options, &option_index);
        if (c == -1)
            break;

        switch (c) {
            case 0:
                if(option_index == 0)
                {
                    if(optarg)
                    {
                        strcpy(file_name, optarg);
                        bname = true;
                    }           
                }
                else if(option_index == 1)
                {
                    currentEq = atoi(optarg);
                }
                else if(option_index == 2)
				{
					rotate = atoi(optarg);
				}
				else if(option_index == 3)
                {
                    print_help();
                    exit(0);
                }
                else if(option_index == 4)
                {
                    currentVolume = atoi(optarg);
                }
                break;
            case 'n':
                strcpy(file_name, optarg);
                bname = true;
                break;
            case 'e':
                currentEq = atoi(optarg);
                break;
            case 'v':
                currentVolume = atoi(optarg);
                break;
			case 'r':
				rotate = atoi(optarg);
				break;
            case 'h':
                print_help();
                exit(0);
                break;
            default:
                printf("Error: not support this parame.\n");
                exit(1);
        }
    }

    if(!bname)
    {
        printf("Error: no file name !\n");
        exit(2);
    }

    printf("filename:%s\n", file_name);

    return 0;
}

/*
 * 初始化Demux库
 * 参数：
 * source[in]:文件源
 * 返回值：
 * 0：初始化成功
 * 其它：初始化失败
 */
static int dmx_init(IFileSource* source)
{
    T_MEDIALIB_DMX_OPEN_INPUT open_input;
    T_MEDIALIB_DMX_OPEN_OUTPUT open_output;

    memset(&open_input, 0, sizeof(T_MEDIALIB_DMX_OPEN_INPUT));
    memset(&open_output, 0, sizeof(T_MEDIALIB_DMX_OPEN_OUTPUT));
    open_input.m_hMediaSource = (T_S32)source;
    open_input.m_CBFunc.m_FunPrintf = (MEDIALIB_CALLBACK_FUN_PRINTF)printf;
    open_input.m_CBFunc.m_FunRead = (MEDIALIB_CALLBACK_FUN_READ)libFread;
    open_input.m_CBFunc.m_FunWrite = (MEDIALIB_CALLBACK_FUN_WRITE)libFwrite;
    open_input.m_CBFunc.m_FunSeek = (MEDIALIB_CALLBACK_FUN_SEEK)libFseek;
    open_input.m_CBFunc.m_FunTell = (MEDIALIB_CALLBACK_FUN_TELL)libFtell;
    open_input.m_CBFunc.m_FunMalloc = (MEDIALIB_CALLBACK_FUN_MALLOC)malloc;
    open_input.m_CBFunc.m_FunFree = (MEDIALIB_CALLBACK_FUN_FREE)free;
    open_input.m_CBFunc.m_FunFileHandleExist = lnx_fhandle_exist;

    // open the dumxer
    hMedia = MediaLib_Dmx_Open(&open_input, &open_output);
    if (AK_NULL == hMedia)
    {
        return -1;
    }   
    // get media info
    memset(&mediaInfo, 0, sizeof(T_MEDIALIB_DMX_INFO));
    MediaLib_Dmx_GetInfo(hMedia, &mediaInfo);

    // release the info memory
    MediaLib_Dmx_ReleaseInfoMem(hMedia);

    return 0;
}

/*
 * 释放Demux占用的资源
 * 参数：
 * 无
 * 返回值
 * 无
 */
void dmx_deinit()
{
    if(dmxTid != 0)
        pthread_join(dmxTid, NULL);

    //close lib
    if (hMedia != NULL)
    {
        MediaLib_Dmx_Stop(hMedia);
        MediaLib_Dmx_Close(hMedia);
    }

    //close source
    if (dSource != NULL)
    {
        dSource->close();
    }

}

/*
 * 启动Demuxer线程
 * 参数：
 * 无
 * 返回值：
 * 0：启动Demuxer成功
 * 其它：启动Demuxer失败
 */
int dmx_start()
{
    pthread_mutex_lock(&dmxMutex);
    bPause = false;
    pthread_cond_signal(&dmxCond);
    pthread_mutex_unlock(&dmxMutex);

    //start audio decoder thread
    if (mediaInfo.m_bHasAudio && (dAudioDecoder != NULL))
    {
        dAudioDecoder->start();
    }


    return 0;
}

/*
 * 暂停Demuxer,当解析出现错误时才会调用该操作
 * 参数：
 * 无
 * 返回值：
 * 0：成功
 * 其它：失败
 */
int dmx_pause()
{

    unsigned long time1, time2;
    
    time1 = get_system_time_ms();
    //pause audio decode thread
    if (mediaInfo.m_bHasAudio)
    {
        dAudioDecoder->pause();
    }
    time2 = get_system_time_ms();
    printf("p,a:%lu.\n",time2 - time1);
    

    time1 = get_system_time_ms();
    //pause video decode thread

    time2 = get_system_time_ms();
    printf("p,v:%lu.\n",time2 - time1);

    

    time1 = get_system_time_ms();
    //pause audio decode thread
    pthread_mutex_lock(&dmxMutex);
    bPause= true;
    pthread_cond_signal(&bufCond);
    pthread_mutex_unlock(&dmxMutex);
    time2 = get_system_time_ms();
    printf("p,d:%lu.\n",time2 - time1);

    return 0;
}

/*
 * 停止Demuxer操作
 * 参数：
 * 无
 * 返回值：
 * 0：成功
 * 其它：失败
 */
int dmx_stop()
{
    //stop video decode thread

    //stop audio decode thread
    if (mediaInfo.m_bHasAudio && (dAudioDecoder != NULL))
    {
        dAudioDecoder->stop();
        audioComplete = false;
    }
    
    //stop demuxer thread
    pthread_mutex_lock(&dmxMutex);
    bPause = false;
    bExit = true;
    pthread_cond_signal(&dmxCond);
    pthread_mutex_unlock(&dmxMutex);

    return 0;
}

/*
 * Demuxer完成
 * 参数:
 * 无
 * 返回值:
 * 无
 */
void dmx_complete( )
{
    //notify audio decoder, the file demuxer complete
    if(mediaInfo.m_bHasAudio && (dAudioDecoder != NULL))
    {
        dAudioDecoder->srcEndNotify();
    }


}

/*
 * 对音频进行seek操作
 * 参数：
 * msec: 移动到的位置，ms为单位
 * isEnd:是否移动到末尾
 * 返回值：
 * 0：成功
 * -1：失败
 */
int dmx_seekAudioTo(int msec)
{
    T_AUDIO_SEEK_INFO audioSeekInfo;

    if (NULL == dAudioDecoder)
    {
        return -1;
    }

    T_AUDIO_SEEK_INFO *pAudioSeekInfo = MediaLib_Dmx_GetAudioSeekInfo(hMedia);

    //no info? create a new one
    if (AK_NULL == pAudioSeekInfo)
    {
        audioSeekInfo.real_time = msec;
        pAudioSeekInfo = &audioSeekInfo;
    }

    //tell audio decoder
    dAudioDecoder->seekTo(pAudioSeekInfo);

    return 0;
}

/*
 * Demuxer Seekto操作
 * 参数：
 * msec：seek到的ms数
 * 0：成功
 * -1:失败
 */
int dmx_seekTo(int msec)
{
    T_S32 seekPlace = 0;
    bool  bAudioPlaying = false;

    if(!mediaInfo.m_bAllowSeek)
    {
        printf("WARNING:: not allow seek.\n");
        return -1;
    }

    // if seek time out of range, we seek to begin time
    if (msec < 0 || msec > (int)mediaInfo.m_ulTotalTime_ms)
        msec = 0;

    // pause video and audio

    if (mediaInfo.m_bHasAudio && (dAudioDecoder != NULL))
    {
        bAudioPlaying = dAudioDecoder->isPlaying();
        if (bAudioPlaying)
        {
            dAudioDecoder->pause();
        }
    }

    // set position
    pthread_mutex_lock(&dmxMutex);

    seekPlace = MediaLib_Dmx_SetPosition(hMedia, msec, true);
    if (seekPlace < 0)
    {
        pthread_mutex_unlock(&dmxMutex);
        return -1;
    }

    bPause = false;
	
   
    if (mediaInfo.m_bHasAudio && (dAudioDecoder != NULL))
    {
        dmx_seekAudioTo(seekPlace);
    }

	MediaLib_Dmx_Start(hMedia, seekPlace);
	
    pthread_cond_signal(&bufCond);
    pthread_cond_signal(&dmxCond); 
    pthread_mutex_unlock(&dmxMutex);

    // start again
    if (mediaInfo.m_bHasAudio && bAudioPlaying && (dAudioDecoder != NULL))
    {
        dAudioDecoder->start();
    }

    return 0;
}

/*
 * 修正获取的视频信息，因为Demuxer开获取的视频信息有可能不正确，必须解码后修正
 * 参数：
 * 无
 * 返回值：
 * 0：成功
 * 其它：失败
 */
 /*
int dmx_reviseVideoInfo()
{
    T_pDATA buff = NULL;
    T_U32 buffLen = 0;
    
    if(dVideoDecoder == NULL)
        return -1;
    buffLen = MediaLib_Dmx_GetFirstVideoSize(hMedia);
    buff = dVideoDecoder->getBuffer(buffLen);
    
    //get data and update write pointer
    if ((buff == NULL) 
         || (MediaLib_Dmx_GetFirstVideo(hMedia, buff, &buffLen) == AK_FALSE)
         || dVideoDecoder->pushBuffer(buff, buffLen) < 0)
    {
        printf("reviseVideoInfo:demux data failed!!\n");
        return -1;
    }

    //decode
    if (dVideoDecoder->decodeFirstFrame() < 0)
    {
        printf("reviseVideoInfo: decode data failed!!\n");
        return -1;
    }

    return 0;
}
*/
/*
 * 修改音频信息
 * 参数：
 * 无
 * 返回值：
 * 0：成功
 * 其它：失败
 */
int dmx_reviseAudioInfo()
{
    T_pDATA buff = NULL;
    T_U32 buffLen = 0;
    T_S32 ret = 0;
    T_U32 buffSize = 1024;

    if(dAudioDecoder == NULL)
        return -1;

    T_U8* outBuff = (T_U8 *)malloc(sizeof(T_U8) * buffSize);
    if (NULL == outBuff)
    {
        return -1;
    }

    bool isEnd = false;

    //must start the lib
    MediaLib_Dmx_Start(hMedia, 0);

    do
    {
        //get size
        buffLen = MediaLib_Dmx_GetAudioDataSize(hMedia);
        if (0 == buffLen)
        {
            //check end
            if (AK_TRUE == MediaLib_Dmx_CheckAudioEnd(hMedia))
            {
                isEnd = true;
            }
            else // error
            {
                printf("reviseAudioInfo: get audio data size failed!!\n");
                free(outBuff);
                return -1;
            }
        }
        else
        {
            //get buffer
            buff = dAudioDecoder->getBuffer(buffLen);
            if (NULL == buff)
            {
                printf("reviseAudioInfo: get buffer failed!!\n");
                free(outBuff);
                return -1;
            }

            //get data
            T_U32 getSize = MediaLib_Dmx_GetAudioData(hMedia, buff, buffLen);
            if (0 == getSize)
            {
                //check end
                if (AK_TRUE == MediaLib_Dmx_CheckAudioEnd(hMedia))
                {
                    isEnd = true;
                }
                else //error
                {
                    printf("reviseAudioInfo: get audio data failed!!\n");
                    free(outBuff);
                    return -1;
                }
            }
            else if (getSize < buffLen)
            {
                //check status
                if (MEDIALIB_DMX_ERR == MediaLib_Dmx_GetStatus(hMedia))
                {
                    printf("reviseAudioInfo: get audio data failed!!\n");
                    free(outBuff);
                    return -1;
                }
            }

            //update write pointer
            if (getSize > 0 && dAudioDecoder->pushBuffer(getSize) < 0)
            {
                printf("reviseAudioInfo: push buffer failed!!\n");
                free(outBuff);
                return -1;
            }
        }

        //解码一帧音频数据
        ret = dAudioDecoder->decodeFirstFrame(outBuff, buffSize, isEnd);
        if (ret < 0 || (isEnd && 0 == ret))
        {
            printf("reviseAudioInfo: decode data failed!!\n");
            free(outBuff);
            return -1;
        }
    } while ((0 == ret) && !isEnd);

    free(outBuff);

    //seek the position of demux lib to 0
    MediaLib_Dmx_SetPosition(hMedia, 0, AK_TRUE);
    dmx_seekAudioTo(0);
    
    printf("reviseAudioInfo success\n");

    return 0;
}

/*
 * 初始化demuxer线程
 * 参数：
 * 无
 * 返回值
 * 0：成功
 * 其它：失败
 */
int dmx_prepare()
{
    if(dmxTid != 0)
    {
        printf("Demuxer thread had created. Can't run again.\n");
        return -1;
    }

    // Start dmuxer lib
    MediaLib_Dmx_Start(hMedia, 0);
    
    //create decode thread  
    pthread_mutex_lock(&dmxMutex);

    pthread_attr_t attr;                
    struct sched_param priparam;                                
    pthread_attr_init(&attr);               
    // pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);                
    pthread_attr_setschedpolicy(&attr, SCHED_RR);               
    priparam.sched_priority = 1;                
    pthread_attr_setschedparam(&attr, &priparam);  

    // 启动demuxerThread
    int result = pthread_create(&dmxTid, &attr,(pthread_entry)demuxerThread, NULL);
    pthread_attr_destroy(&attr);
    
    if (result != 0) {
        printf("Create demuxer thread failed.\n");
        pthread_mutex_unlock(&dmxMutex);
        return -1;
    }

    // 等待线程启动
    //pthread_cond_wait(&dmxCond, &dmxMutex);
    pthread_mutex_unlock(&dmxMutex);
    
    return 0;
}

/*
 * wakeUpFromBuffWait
 * param:
 * return
 */
void dmx_wakeUpFromBuffWait()
{
    pthread_mutex_lock(&bufMutex);
    if (bBufWait)
    {
        pthread_cond_signal(&bufCond);
    }
    pthread_mutex_unlock(&bufMutex);
}

/*
 * 解析音频数据
 * 参数：
 * buffLen[in]:解析的音频数据长度
 * 返回值：
 * RET_ERROR:出现解析错误
 * RET_END:解析到文件末尾
 * RET_NO_BUFFER:没有缓冲区
 * RET_OK:解析成功
 */
int demuxAudioData( T_U32 buffLen)
{   
    if(dAudioDecoder == NULL)
        return RET_OK;
    
    T_pDATA buff = dAudioDecoder->getBuffer(buffLen);
    if (buff != NULL)
    {
        T_U32 getSize = MediaLib_Dmx_GetAudioData(hMedia, buff, buffLen);

        if (getSize < buffLen && MEDIALIB_DMX_ERR == MediaLib_Dmx_GetStatus(hMedia))
        {
            printf("demux audio data error!!\n");
            return RET_ERROR;
        }

        if(getSize == 0)
        {
            // MediaLib_Dmx_GetAudioDataSize() always return 1024 when decoding pure audio file,
            // when MediaLib_Dmx_GetAudioData() return 0, it means all audio data has been read.
            // But if the media file has audio and video data, when MediaLib_Dmx_GetAudioData() return 0,
            // datasource maybe read error, but MediaLib_Dmx_GetStatus() wouldn't return MEDIALIB_DMX_ERR,
            // so we return RET_ERROR here.
            if (mediaInfo.m_bHasVideo)
            {
                printf("demux audio data error!!\n");
                return RET_ERROR;
            }
            else
            {
                return RET_END;
            }
        }

        // 更新音频数据写指针
        dAudioDecoder->pushBuffer(getSize);
    }
    else
    {
        return RET_NO_BUFFER;
    }

    return RET_OK;
}

/*
 * 解析视频数据
 * 参数：
 * buffLen[in]:视频数据长度
 * 返回值：
 * RET_ERROR:出现错误
 * RET_NO_BUFFER:没有缓冲区
 * RET_OK:成功
 */

/*
int demuxVideoData(T_U32 buffLen)
{
    if(dVideoDecoder == NULL)
        return RET_OK;
    
    T_pDATA buff = dVideoDecoder->getBuffer(buffLen);

    if (buff != NULL)
    {
        T_BOOL bResult = MediaLib_Dmx_GetVideoFrame(hMedia, buff, &buffLen);
        if (bResult == AK_FALSE)
        {
            printf("demux video data error\n");
            return RET_ERROR;
        }

        int ret = dVideoDecoder->pushBuffer(buff, buffLen);
    }
    else
    {
        return RET_NO_BUFFER;
    }

    return RET_OK;
}
*/
/*
 * 进行Demuxer解析
 * 参数：
 * 无
 * 返回值：
 * RET_ERROR:出现错误
 * RET_END:解析结束
 * RET_NO_BUFFER:没有buffer
 * RET_OK:解析成功
 */
int DoDemux()
{
    T_pDATA buff = NULL;
    T_U32   buffLen = 0;

    bool audioBuffFull = false;
    bool videoBuffFull = false;

    //==========audio data=============
    if (!mediaInfo.m_bHasAudio)
    {
        audioBuffFull = true;
        audioComplete = true;
    }
    else if (!audioComplete && (dAudioDecoder != NULL))
    {
        //check size
        buffLen = MediaLib_Dmx_GetAudioDataSize(hMedia);
        if (0 == buffLen)
        {
            if (AK_TRUE == MediaLib_Dmx_CheckAudioEnd(hMedia))
            {
                printf("demux have no audio data\n");
                audioComplete = true;
                dAudioDecoder->srcEndNotify();
            }
            else //error
            {
                printf("ERROR!! MediaLib_Dmx_GetAudioDataSize = 0\n");
                return RET_ERROR;
            }
        }
        else
        {
            //get audio data
            int ret = demuxAudioData(buffLen);
            if (ret == RET_ERROR)
            {
                return RET_ERROR;
            }
            else if (ret == RET_END)
            {
                audioComplete = true;
                dAudioDecoder->srcEndNotify();
            }
            else if (ret == RET_NO_BUFFER)
            {
                audioBuffFull = true;
            }
        }
    }
    else
    {
        audioBuffFull = true;
    }

        
    //=============video data==============

    buff = NULL;
    buffLen = 0;

    if (audioBuffFull && videoBuffFull)
    {
        return RET_NO_BUFFER;
    }

    return RET_OK;

}

/*
 * demuxerThread线程
 * 参数：
 * 无
 * 返回值：
 * 0：
 */
static int  demuxerThread()
{
    // 解析是否出错
    bool bError = false;
    int  countError = 0;

    printf("Demuxer thread run.\n");
    
    // 通知Demuxer线程已经启动
    // pthread_cond_signal(&dmxCond);

    // 初始位置0启动Demuxer解析
    // MediaLib_Dmx_Start(hMedia, 0);
    
    while (1)
    {
        bError = false;

        pthread_mutex_lock(&dmxMutex);
        if(bSeeking)
        {
            // wait 1ms for seek oper
            usleep(1000);
        }

        if (bPause)
        {
            pthread_cond_wait(&dmxCond, &dmxMutex);
        }

        if (bExit)
        {
            pthread_mutex_unlock(&dmxMutex);
            break;
        }

        // 实际的demux操作: 立即返回不阻塞
        int ret = DoDemux();

        if (ret == RET_ERROR)
        {
            countError++;
            if(countError > 5)
                bError = true;
        }
        else if (ret == RET_NO_BUFFER)
        {
            pthread_mutex_unlock(&dmxMutex);
            bBufWait = true;
            while( true )
            {
                int ret;
                pthread_mutex_lock(&bufMutex);
                //printf("while true wait buffer\n");

                // 缓冲区满，等待有缓冲条件，阻塞
                struct timeval tv;
                struct timespec tm;
                gettimeofday(&tv, NULL);
                tm.tv_sec = tv.tv_sec;
                tm.tv_nsec = tv.tv_usec * 1000UL + 10 * 1000UL * 1000UL;
                ret = pthread_cond_timedwait(&bufCond, &bufMutex, &tm);
                pthread_mutex_unlock(&bufMutex);
                if( ret == 0 )
                    break;

                if( bExit )
                    break;
            }
            bBufWait = false;
            continue;
        }

        //========== error handle ===========
        if (bError)
        {
            printf("Dumxer error.\n");
            bPause = true; 
            bError = false;
            pthread_mutex_unlock(&dmxMutex);
            continue;
        }

        //========== complete  handle ===========
        if (audioComplete && videoComplete)
        {
            // 音视频播放完成，等待音视频解码完成
            printf("audio and video complete.\n");

            bPause = true;
        }

        pthread_mutex_unlock(&dmxMutex);
    }

    //exit the thread
    pthread_mutex_lock(&dmxMutex);
    dmxTid = 0;
    pthread_cond_signal(&dmxCond);
    pthread_mutex_unlock(&dmxMutex);

    printf("Demuxer thread exit!\n");

    return 0;
}

/*
 * 播放、暂停操作处理函数
 * 参数：
 * arg:未使用
 * 返回值：
 * 无
 */
void play_and_pause_handler(void *arg)
{
    printf("Enter %s \n",__func__);
    if(bPause)
    {
        printf("play handler.\n");
        dmx_start();
    }
    else
    {
        printf("pause handler.\n");
        dmx_pause();
    }
    printf("Leave %s \n",__func__);
}

/*
 * 退出按键消息处理
 * 参数：
 * arg:未使用
 * 返回值：
 * 无
 */
void quit_handler(void *arg)
{
    printf("stop_handler\n");
    dmx_stop();
}

/*
 * 音量加消息处理
 * 参数：
 * arg:未使用
 * 返回值：
 * 无
 */
void volumeup_handler(void *arg)
{
    int volume;

    if(dAudioSink != NULL)
    {
        volume = dAudioSink->getVolume();
        volume += 100;
        if(volume <= 1024)
        {
            dAudioSink->setVolume(volume);
        }
    }
}

/*
 * 音量减消息处理
 * 参数：
 * arg:未使用
 * 返回值：
 * 无
 */
void volumedown_handler(void *arg)
{
    int volume;

    if(dAudioSink != NULL)
    {
        volume = dAudioSink->getVolume();
        volume -= 100;
        if(volume >= 0)
        {
            dAudioSink->setVolume(volume);
        }
    }
}   

/*
 * 快进消息处理函数
 * 参数：
 * arg:未使用
 * 返回值：
 * 无
 */
void speedup_handler(void *arg)
{
    printf("speedup_handler\n");
    int timepos;

    // check the status from the two thread

		if (mediaInfo.m_bHasAudio && (dAudioDecoder != NULL))
    {
        dAudioDecoder->getCurrentPosition(&timepos);
    }
    else
    {
        return;
    }
    timepos += SEEK_STEP;
    printf("Seek pos:%d.\n", timepos);
    dmx_seekTo(timepos);
}

/*
 * 快退消息处理
 * 参数：
 * arg:未使用
 * 返回值：
 * 无
 */
void speeddown_handler(void *arg)
{
    printf("speeddown_handler\n");

    int timepos;

    // check the status from the two thread
   /* if (mediaInfo.m_bHasVideo && (dVideoDecoder != NULL))
    {
        dVideoDecoder->getCurrentPosition(&timepos);
    }
    else */
		if (mediaInfo.m_bHasAudio && (dAudioDecoder != NULL))
    {
        dAudioDecoder->getCurrentPosition(&timepos);
    }
    else
    {
        return;
    }
    
    timepos -= SEEK_STEP;
    printf("Seek pos:%d.\n", timepos);
    if(timepos < 0)
        timepos = 0;
    dmx_seekTo(timepos);
}

/*
 * 主函数
 * 参数：
 * argc:参数数量
 * argv:参数列表
 * 返回值：
 * 0表示成功，其它表示失败
 */
int main(int argc, char *argv[])
{
    volatile int quit_pressed = 0;
    
    int fd;
   
    
    T_eMEDIALIB_CHIP_TYPE chipType;

    sig_init();

    // 处理命令行参数
    parse_cmd(argc, argv);

    IFileSource * ifile = new CFileSource(file_name);
    if(ifile == NULL)
        return -1;
    
    // get chip type
    chipType = CheckMach( );

    // init demuxer
    if(0 != dmx_init(ifile))
        return -2;


    // init audio decoder
    if(mediaInfo.m_bHasAudio)
    {
    	int ret;
        dAudioSink = new CAudioSink();
        dAudioDecoder = new AkAudioDecoder(dAudioSink);
        ret = dAudioDecoder->init(&mediaInfo, ifile->getLength());
		if(ret < 0)
		{
			return -1;
		}
        //if has video, set video's audiosink
        /*
        if(mediaInfo.m_bHasVideo && (dVideoDecoder != NULL))
            dVideoDecoder->setAudioSink(dAudioSink);
		*/
        //init audio decode lib and audio output
        int outputRate = 0;
        if (mediaInfo.m_bHasVideo)
        {
            outputRate = mediaInfo.m_uFPS * 2;
        }
        dmx_reviseAudioInfo();
        dAudioDecoder->openAudioSink(outputRate);

        // 设置音效
        dAudioSink->setEQ(currentEq);

		dAudioSink->setAGC(AGC_LEVEL);
		dAudioSink->setVolume(currentVolume);
		
        //启动audio decoder线程
        dAudioDecoder->prepare();
    }

    //启动demuxer线程
    dmx_prepare( );

    /* 打开输入输入的设备文件 */
    if(chipType == MEDIALIB_CHIP_AK9802)
    {
        fd = open(KBD_DEV_NAME_98, O_RDONLY);
    }
    else
    {
        fd = open(KBD_DEV_NAME_37, O_RDONLY);
    }
    if (fd  < 0) 
    {
//        perror("keyboard device open error.\n");
//        return 1;
    }


	play_and_pause_handler(NULL);
	while(false == DecodeComplete)
	{
		sleep(1);
	}
	quit_handler(NULL);
    quit_pressed = 1;
    /* 获取并打印输入设备支持的事件类型 */

#if 0
    while (0)
    {

        rd = read(fd, ev, sizeof(struct input_event) * 64);

        if (rd < (int) sizeof(struct input_event))
        {
            perror("\nkeyevent: error reading");
            return 1;
        }

        for (i = 0; i < rd / sizeof(struct input_event); i++)
        {
            /* Key release */
            if((ev[i].type == EV_KEY) && (ev[i].value == 0))
            {
                /* 不响应按键按下消息 */
            }
            /* Key press */
            else if((ev[i].type == EV_KEY) && (ev[i].value == 1))
            {
                if(chipType == MEDIALIB_CHIP_AK3751)
                {
                    int j;
                    struct input_event ev_tmp;
                    for(j = 0; j < 50; j++)
                    {
                        read(fd, &ev_tmp, sizeof(struct input_event));

                        if(ev_tmp.type == EV_KEY && ev_tmp.value !=2)
                        {
                            break;  //if key-press up
                        }
                    }
                    if(j >= 50)
                    {
                        if(ev[i].code == KEY_MENU)
                        {// 退出
                            ev[i].code = KEY_HOME;
                        }
                    }
                }

                switch(ev[i].code)
                {

                    case KEY_VOLUMEUP:
                    case KEY_RIGHT:
                        // 快进
                        speedup_handler(NULL);
                        break;
                    case KEY_VOLUMEDOWN:
                    case KEY_LEFT:
                        // 快退
                        speeddown_handler(NULL);
                        break;
                    case KEY_MENU:
                        // 播放、暂停
                        play_and_pause_handler(NULL);
                        break;
                    case KEY_UP:
                        volumeup_handler(NULL);
                        break;
                    case KEY_DOWN:
                        volumedown_handler(NULL);
                        break;
                    case KEY_HOME:
                        // 退出
                        quit_handler(NULL);
                        quit_pressed = 1;
                        break;
                    default:
                        break;
                }

            }
        }
        if(quit_pressed)
            break;
    }
#endif
    dmx_deinit();
    
    if(ifile)
    {
        ifile->close();
        delete ifile;
    }

    if(dAudioDecoder)
    {
        dAudioDecoder->deinit();
        delete dAudioDecoder;
    }
    if(dAudioSink)
    {
        dAudioSink->close();
        delete dAudioSink;
    }

    return 0;
}
