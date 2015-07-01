USE_CPP := 1
LOCAL_SRC_CXXFILES := \
  		CAudioSink.cpp \
		platform_cb.cpp \
		pcmplayer.cpp 

LOCAL_MODULE := pcmplayer
PREFIX := arm-none-linux-gnueabi-

LOCAL_SHARED_LIBRARIES := akuio asound akmedialib akaudiocodec akaudiofilter
LOCAL_C_INCLUDES := ../../librootfs/akmedialib/include/akmedialib \
					../../librootfs/akmedialib/include \
					../../librootfs/akuiolib/include \
					../../librootfs/alsalib/include
#LOCAL_LDFLAGS := -L../akmedialib/lib -L./

#include LcdLib/Make_config
LOCAL_LDFLAGS += -L../../librootfs/akmedialib/usr/lib \
				-L../../librootfs/akuiolib/usr/lib \
				-L../../librootfs/alsalib/usr/lib
include exec_rules.mk
