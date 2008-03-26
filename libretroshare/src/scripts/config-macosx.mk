
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
UPNPC_DIR=../../../../../src/miniupnpc-1.0

include $(RS_TOP_DIR)/scripts/checks.mk

############ ENFORCE DIRECTORY NAMING ########################

CC = g++
RM = /bin/rm

# RANLIB = ranlib
# Dummy ranlib -> can't do it until afterwards with universal binaries.
RANLIB = ls -l 

LIBDIR = $(RS_TOP_DIR)/lib
LIBRS = $(LIBDIR)/libretroshare.a

# Unix: Linux/Cygwin
INCLUDE = -I $(RS_TOP_DIR) 
CFLAGS = -Wall -g $(INCLUDE) 

# This Line is for Universal BUILD.
# CFLAGS = -arch ppc -arch i386 -Wall -g $(INCLUDE) 

# This Line is for Universal BUILD for 10.4 + 10.5 (but unlikely to work unless Qt Libraries are build properly)
# CFLAGS = -isysroot /Developer/SDKs/MacOSX10.4u.sdk -arch i386 -arch ppc -Wall -g $(INCLUDE) 

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


# RSCFLAGS = -Wall -g $(INCLUDE) 

#########################################################################
# OS Compile Options
#########################################################################

# For the SSL BIO compilation. (Copied from OpenSSL compilation flags)
BIOCC  = gcc

# MacOSX flags
BIOCFLAGS =  -I $(SSL_DIR)/include -DOPENSSL_SYSNAME_MACOSX -DOPENSSL_THREADS -D_REENTRANT -DOPENSSL_NO_KRB5 -O3 -fomit-frame-pointer -fno-common -DB_ENDIAN

# This is for the Universal Build...
# but is unlikely to work... as options are PPC specific.... 
# 
# BIOCFLAGS =  -arch ppc -arch i386 -I $(SSL_DIR)/include -DOPENSSL_SYSNAME_MACOSX -DOPENSSL_THREADS -D_REENTRANT -DOPENSSL_NO_KRB5 -O3 -fomit-frame-pointer -fno-common -DB_ENDIAN


#########################################################################
# OS specific Linking.
#########################################################################

LIBS = -Wl,-search_paths_first

# for Univeral BUILD
# LIBS += -arch ppc -arch i386

# LIBS += -Wl,-syslibroot,/Developer/SDKs/MacOSX10.4u.sdk -arch ppc -arch i386

LIBS +=  -L$(LIBDIR) -lretroshare 
ifdef PQI_USE_XPGP
	LIBS +=  -L$(SSL_DIR) 
  endif
LIBS +=  -lssl -lcrypto  -lpthread
LIBS +=  -L$(UPNPC_DIR) -lminiupnpc
LIBS +=  $(XLIB) -ldl -lz 
	
RSLIBS = $(LIBS)


