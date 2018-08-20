bin_PROGRAMS = obmc-ikvm

noinst_HEADERS = \
	ikvm_args.hpp \
	ikvm_input.hpp \
	ikvm_manager.hpp \
	ikvm_server.hpp \
	ikvm_video.hpp

obmc_ikvm_SOURCES = \
	ikvm_args.cpp \
	ikvm_input.cpp \
	ikvm_manager.cpp \
	ikvm_server.cpp \
	ikvm_video.cpp \
	obmc-ikvm.cpp

obmc_ikvm_CXXFLAGS = $(PHOSPHOR_LOGGING_CFLAGS) -std=c++11

obmc_ikvm_LDFLAGS = $(PHOSPHOR_LOGGING_LIBS) -lvncserver -lpthread
