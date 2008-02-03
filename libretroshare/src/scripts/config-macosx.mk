
ifneq ($(OS),MacOSX)
dummy:
	echo "ERROR MacOSX configuration file included, but (OS != MacOSX)

endif

############   LINUX CONFIGURATION    ########################

# flags for components....
PQI_USE_XPGP = 1
#PQI_USE_PROXY = 1
#PQI_USE_CHANNELS = 1
#USE_FILELOOK = 1

SSL_DIR=../../../../../src/openssl-0.9.7g-xpgp-0.1c
UPNPC_DIR=../../../../../src/miniupnpc-20070515

include $(RS_TOP_DIR)/scripts/checks.mk

############ ENFORCE DIRECTORY NAMING ########################

CC = g++
RM = /bin/rm
RANLIB = ranlib
LIBDIR = $(RS_TOP_DIR)/lib
LIBRS = $(LIBDIR)/libretroshare.a

# Unix: Linux/Cygwin
INCLUDE = -I $(RS_TOP_DIR) 
CFLAGS = -Wall -g $(INCLUDE) 

ifdef PQI_USE_XPGP
	INCLUDE += -I $(SSL_DIR)/include 
endif

ifdef PQI_USE_XPGP
	CFLAGS += -DPQI_USE_XPGP
endif

ifdef PQI_USE_PROXY
	CFLAGS += -DPQI_USE_PROXY
endif

ifdef PQI_USE_CHANNELS
	CFLAGS += -DPQI_USE_CHANNELS
endif

ifdef USE_FILELOOK
	CFLAGS += -DUSE_FILELOOK
endif


RSCFLAGS = -Wall -g $(INCLUDE) 

#########################################################################
# OS Compile Options
#########################################################################

# For the SSL BIO compilation. (Copied from OpenSSL compilation flags)
BIOCC  = gcc

# MacOSX flags
BIOCFLAGS =  -I $(SSL_DIR)/include -DOPENSSL_SYSNAME_MACOSX -DOPENSSL_THREADS -D_REENTRANT -DOPENSSL_NO_KRB5 -O3 -fomit-frame-pointer -fno-common -DB_ENDIAN


#########################################################################
# OS specific Linking.
#########################################################################

LIBS = -Wl,-search_paths_first
LIBS +=  -L$(LIBDIR) -lretroshare 
ifdef PQI_USE_XPGP
	LIBS +=  -L$(SSL_DIR) 
  endif
LIBS +=  -lssl -lcrypto  -lpthread
LIBS +=  -L$(UPNPC_DIR) -lminiupnpc
LIBS +=  $(XLIB) -ldl -lz 
	
RSLIBS = $(LIBS)


