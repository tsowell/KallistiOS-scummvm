# KallistiOS ##version##
#
# libmp3/Makefile
# (c)2001 Dan Potter
#

OBJS = snd_iface.o snd_sfxmgr.o snd_stream.o snd_stream_drv.o snd_mem.o snd_midi.o

# Only compile this if we have an ARM compiler handy
ifdef DC_ARM_CC
	SUBDIRS = arm
endif

include $(KOS_BASE)/Makefile.prefab

snd_stream_drv.o: subdirs arm/stream.drv
	$(KOS_BASE)/utils/bin2o/bin2o arm/stream.drv snd_stream_drv snd_stream_drv.o

