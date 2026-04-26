#
# FILE            makefile
#
# AUTHOR          Ken Zangelin
#
# Copyright 2026 Seamware
#
LIB_NAME      = swNgsild
LIB_SO        = lib$(LIB_NAME).so
LIB           = lib$(LIB_NAME).a
CC            = gcc
PREFIX        = ..
INCLUDE       = -I$(PREFIX)
DFLAGS        = -DANSI
CFLAGS        = -Wall -Werror -O2 -fPIC -Wno-unused-function $(DFLAGS) $(INCLUDE) -MMD -MP

debug: CFLAGS += -g -DDEBUG
debug: all
LIB_SOURCES   = swNgsild.c \
                ldInit.c \
                ldError.c \
                ldParams.c \
                ldTypes.c \
                ldAttrTypeDetect.c \
                ldCheckUri.c \
                ldCheckDateTime.c \
                ldCheckGeo.c \
                ldCheckEntity.c \
                ldCheckAttribute.c \
                ldCheckSubscription.c \
                ldCheckRegistration.c \
                ldRender.c \
                ldQueryParams.c \
                ldPickOmit.c \
                ldStripSysAttrs.c \
                ldApiEntityToDbModel.c \
                ldEntityToApi.c \
                ldEntityMerge.c \
                ldScopeExprParse.c \
                ldScopeMatch.c \
                ldTypeExprParse.c \
                ldQParse.c \
                ldGeoRelParse.c \
                ldUrlParams.c \
                ldOrderSort.c \
                ldLangReduce.c \
                ldNormalizeInput.c \
                ldHooks.c \
                ldPagination.c \
                ldParamsValidate.c \
                ldToGeoJson.c \
                ldEntityMatch.c \
                ldEntityAttrsSet.c \
                ldSubscriptionNotify.c \
                ldCsrSubNotify.c \
                ldNotifyStatsHook.c \
                ldNotifyDefer.c \
                ldSubCache.c \
                ldSubStatsFlush.c \
                ldStatsFlushLoop.c \
                ldPernotCache.c \
                ldPernotLoop.c \
                ldEntityMap.c \
                ldQueryBody.c \
                ldRegCache.c \
                ldEntityFragment.c \
                ldDistOp.c \
                ldDistSub.c \
                ldProbeSourceIdentity.c \
                ldForwarding.c \
                ldExpandParams.c \
                ldCsourceAlias.c \
                ldQRender.c \
                ldQAttrs.c \
                ldSubscriptionCompactQ.c \
                ldSubscriptionCounters.c \
                ldSimplifyEntity.c \
                ldDiscovery.c \
                ldDiscoveryForward.c \
                ldStripAtContext.c
LIB_OBJS      = $(LIB_SOURCES:c=o)
LIB_DEPS      = $(LIB_SOURCES:c=d)

LIBS          = ../swRest/libswRest.a ../swJsonld/libswJsonld.a ../kalloc/libkalloc.a ../kjson/libkjson.a ../kbase/libkbase.a ../klog/libklog.a ../ktrace/libktrace.a ../khash/libkhash.a -lpthread

.PHONY: all clean test install i di ci

all: $(LIB_SO) $(LIB)

clean:
					rm -f $(LIB_OBJS)
					rm -f $(LIB_DEPS)
					rm -f $(LIB_SO)
					rm -f $(LIB)

i:          install

install:    all
					@mkdir -p $(PREFIX)/include/$(LIB_NAME)
					@mkdir -p $(PREFIX)/lib
					cp *.h $(PREFIX)/include/$(LIB_NAME)/
					cp $(LIB) $(LIB_SO) $(PREFIX)/lib/

di:         debug install

ci:         clean install

cdi:        clean debug install

test:
					@echo "No tests yet"

$(LIB):			$(LIB_OBJS) $(LIB_SOURCES)
					ar r $(LIB) $(LIB_OBJS)
					ranlib $(LIB)

$(LIB_SO):	$(LIB_OBJS) $(LIB_SOURCES)
					$(CC) -shared $(LIB_OBJS) -o $(LIB_SO) \
						-L../swRest -L../swJsonld -L../kalloc -L../kjson -L../kbase -L../klog -L../ktrace -L../khash \
						-lswRest -lswJsonld -lkalloc -lkjson -lkbase -lklog -lktrace -lkhash -lmicrohttpd -lssl -lcrypto -lpthread \
						-Wl,-rpath,'$$ORIGIN/../swRest:$$ORIGIN/../swJsonld:$$ORIGIN/../kalloc:$$ORIGIN/../kjson:$$ORIGIN/../kbase:$$ORIGIN/../klog:$$ORIGIN/../ktrace:$$ORIGIN/../khash'

%.o: %.c
					$(CC) $(CFLAGS) -c $< -o $@

%.i: %.c
					$(CC) $(CFLAGS) -c $^ -E > $@

-include $(LIB_DEPS)
