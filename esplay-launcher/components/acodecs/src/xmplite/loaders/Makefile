
LOADERS	= xm_load.o mod_load.o s3m_load.o it_load.o

LOADERS_OBJS	= common.o itsex.o sample.o $(LOADERS)
LOADERS_DFILES	= Makefile $(LOADERS_OBJS:.o=.c) \
		  it.h loader.h mod.h s3m.h xm.h
LOADERS_PATH	= src/loaders

OBJS += $(addprefix $(LOADERS_PATH)/,$(LOADERS_OBJS))

default:

dist-loaders::
	mkdir -p $(DIST)/$(LOADERS_PATH)
	cp -RPp $(addprefix $(LOADERS_PATH)/,$(LOADERS_DFILES)) $(DIST)/$(LOADERS_PATH)
