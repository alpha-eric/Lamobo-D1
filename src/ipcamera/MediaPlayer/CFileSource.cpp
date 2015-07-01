/* 
 * @file IFileSource.h
 * @brief declare CFileSource class
 *
 * Copyright (C) 2012 Anyka (GuangZhou) Software Technology Co., Ltd.
 * @author
 * @date 2012-7-18
 * @version 1.0
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "IFileSource.h"

namespace akmedia {

    /* 
     * 构造函数
     * 参数：
     * 无
     * 返回值：
     * 无
     */
    CFileSource::CFileSource():
        m_file(NULL),
        filename(NULL)
    {

    }

    /* 
     * 构造函数
     * 参数：
     * path[in]:文件名
     * 返回值：
     * 无
     */
    CFileSource::CFileSource(const char *path)
    {

        if(m_file)
        {
            fclose(m_file);
        }

        if(path)
        {
            m_file = fopen(path, "r");
            filename = strdup(path);
        }
    }

    /* 
     * 构造函数
     * 参数：
     * fd[in]:文件句柄
     * 返回值：
     * 无
     */
    CFileSource::CFileSource(FILE * fd)
    {
        m_file = fd;
    }

    /* 
     * 析构函数，关闭文件句柄，释放内存
     * 参数：
     * 无
     * 返回值：
     * 无
     */
    CFileSource::~CFileSource( )
    {
        if(m_file)
        {
            fclose(m_file);
        }

        if(filename)
        {
            free(filename);
        }
    }

    /*
     * 打开文件
     * 参数
     * path[in]: 文件名
     * 返回值：
     * 0：成功
     * 其它：失败
     */
    int CFileSource::open(const char *path)
    {
        if(m_file)
        {
            fclose(m_file);
            m_file = NULL;
        }

        if(filename)
        {
            free(filename);
            filename = NULL;
        }

        if(path)
        {
            m_file = fopen(path, "r");
            filename = strdup(path);
        }

        if(m_file)
        {
            return 0;
        }
        else
        {
            return -1;
        }
    }

    /* 
     * 关闭文件
     * 参数：
     * 无
     * 返回值：
     * 无
     */
    void CFileSource::close()
    {
        if(m_file)
        {
            fclose(m_file);
            m_file = NULL;
        }

        if(filename)
        {
            free(filename);
            filename = NULL;
        }
    }

    /* 
     * 读取文件
     * 参数：
     * buf[out]:输出缓冲区
     * len[in]:读取的长度
     * 返回值：
     * 实际读取的长度
     */
    int CFileSource::read(void *buf, long len)
    {
        int n = 0;

        if(m_file && buf)
        {
            n = fread(buf, 1, len, m_file);
        }

        return n;
    }

    /* 
     * 写文件
     * 参数：
     * ptr[in]:要写的数据缓冲区指针
     * size:要写的数据长度
     * 返回值：
     * 实现写的数据长度
     * 注意：该函数不实现写操作
     */
    int CFileSource::write(const void *ptr, long size)
    {
        return 0;
    }

    /* 
     * 移动文件指针
     * 参数：
     * offset[in]:偏移量
     * whence[in]:偏移方向
     * 返回值：
     * 0:移动成功
     * 其它：失败
     */
    int CFileSource::seek(long offset, int whence)
    {
        if(m_file)
        {
            return fseek(m_file, offset, whence);
        }

        return -1;
    }

    /* 
     * 获取文件偏移位置
     * 参数：
     * 无
     * 返回值：
     * 偏移位置
     */
    int CFileSource::tell()
    {
        if(m_file)
        {
            return ftell(m_file);
        }
        return -1;
    }

    /*
     * 获取文件大小
     * 参数：
     * 无
     * 返回值：
     * 文件的大小，以字节为单位
     */
    unsigned int CFileSource::getLength()
    {
        struct stat sb;

        if( 0 == fstat(fileno(m_file), &sb))
        {
            return sb.st_size;
        }

        return 0;
    }

    /* 
     * 获取文件名
     * 参数：
     * 无
     * 返回值：
     * NULL: 失败
     * 其它：文件名指针
     * 注意：用户要拷贝该字符串内容，而且不能free此指针
     */
    char * CFileSource::getName()
    {
        return filename;
    }

} // namespace

