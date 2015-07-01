EQ音效测试说明：
    1.系统支持音效设置功能，支持以下类型：
      _SD_EQ_MODE_NORMAL,（普通）
      _SD_EQ_MODE_CLASSIC,（经典）
	  _SD_EQ_MODE_JAZZ,（JAZZ）
      _SD_EQ_MODE_POP,（流行）
      _SD_EQ_MODE_ROCK,（摇滚）
    2.播放时使用 "-e <数字>"进行音效设置，各数字对应的音效如下：
      0:_SD_EQ_MODE_NORMAL（默认）
      1:_SD_EQ_MODE_CLASSIC
      2:_SD_EQ_MODE_JAZZ 
      3: _SD_EQ_MODE_POP
      4:_SD_EQ_MODE_ROCK
	3.默认使用自增益音效。
		在Mediaplayer.cpp中AGC_LEVEL设置级别。
    3.使用示例
      player_codeclib -n /usr/TestFile/test.mp4 -e 3    
测试使用说明:
    1.进入系统运行player_codeclib -n /usr/TestFile/test.mp4进行音频播放
使用说明：
    1.将可执行文件player_codeclib拷贝到目标板目录
    2.运行./player_codeclib -n "path"
      其中path为播放文件的路径
 
编译源程序方法：
    1.　把rootfs/下的akmedialib/库目录拷到Player_CodecLib源码的同一级目录.
    2.　对于AK98和AK37平台，执行make进行编译即可，应用程序能够自己识别平台类型；
        编译完成后会在BUILD_player_codeclib_EXEC文件夹下生成可执行文件player_codeclib
    3.  如果需要清理编译结果可执行以下命令：
        make clean

