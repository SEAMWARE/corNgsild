#
# FILE            makefile
#
# AUTHOR          Ken Zangelin
#
# Copyright 2026 Seamware
#
LIB_NAME      = corNgsild
LIB_SO        = lib$(LIB_NAME).so
LIB           = lib$(LIB_NAME).a
CC            = gcc
PREFIX        = ..
INCLUDE       = -I$(PREFIX)
DFLAGS        = -DANSI

#
# ICU "root" collation for orderBy string ordering (§ 7.6.2.1). ON by default;
# build with 'make COR_WITH_ICU=0 ...' to drop the libicu dependency (orderBy
# then uses a case-insensitive ASCII approximation of root collation). The
# coraine CMake option COR_FEATURE_ICU_COLLATION must match this setting — it
# adds the matching -licui18n/-licuuc/-licudata to the final broker link.
#
COR_WITH_ICU  ?= 1
ifeq ($(COR_WITH_ICU),1)
DFLAGS       += -DCOR_WITH_ICU
ICU_CFLAGS   := $(shell pkg-config --cflags icu-i18n 2>/dev/null)
ICU_LIBS     := $(shell pkg-config --libs icu-i18n 2>/dev/null)
endif

CFLAGS        = -Wall -Werror -O2 -fPIC -Wno-unused-function $(DFLAGS) $(INCLUDE) $(ICU_CFLAGS) -MMD -MP

debug: CFLAGS += -g -DDEBUG
debug: all
LIB_SOURCES   = corNgsild.c \
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
                ldSysTimestamp.c \
                ldApiEntityToDbModel.c \
                ldEntityToApi.c \
                ldEntityMerge.c \
                ldRegSubMerge.c \
                ldScopeExprParse.c \
                ldScopeMatch.c \
                ldTypeExprParse.c \
                ldQParse.c \
                ldGeoRelParse.c \
                ldUrlParams.c \
                ldProj.c \
                ldOrderSort.c \
                ldLangReduce.c \
                ldToTemporalValues.c \
                ldToAggregatedValues.c \
                ldNormalizeInput.c \
                ldHooks.c \
                ldPagination.c \
                ldParamsValidate.c \
                ldToGeoJson.c \
                ldEntityMatch.c \
                ldEntityAttrsSet.c \
                ldSubscriptionNotify.c \
                ldThrottleDirty.c \
                ldCsrSubNotify.c \
                ldNotifyStatsHook.c \
                ldNotifyDefer.c \
                ldSubCache.c \
                ldSubStatus.c \
                ldSubStatsFlush.c \
                ldStatsFlushLoop.c \
                ldPernotCache.c \
                ldPernotLoop.c \
                ldPeriodicLoop.c \
                ldEntityMap.c \
                ldQueryBody.c \
                ldPCheckQuery.c \
                ldRegCache.c \
                ldEntityFragment.c \
                ldDistOp.c \
                ldContextHost.c \
                ldBatchErrors.c \
                ldWriteResult.c \
                ldDistSub.c \
                ldProbeSourceIdentity.c \
                ldForwarding.c \
                ldExpandParams.c \
                ldCsourceAlias.c \
                ldQRender.c \
                ldQAttrs.c \
                ldSubscriptionCompactQ.c \
                ldSubscriptionCounters.c \
                ldDiscovery.c \
                ldDiscoveryForward.c \
                ldStripAtContext.c \
                ldDistMerge.c \
                ldDatasetIdDedup.c \
                ldNameContentCheck.c \
                ldUrlWildcardCheck.c \
                ldMqttNotify.c \
                ldSnapshotCache.c \
                ldSnapshotNotify.c \
                ldIso8601Duration.c \
                ldRequestSubstitute.c \
                ldLinkedEntitiesHook.c \
                ldExpiresAtPropagate.c \
                ldConformanceDowngrade.c
LIB_OBJS      = $(LIB_SOURCES:c=o)
LIB_DEPS      = $(LIB_SOURCES:c=d)

LIBS          = ../corRest/libcorRest.a ../corJsonld/libcorJsonld.a ../kalloc/libkalloc.a ../kjson/libkjson.a ../kbase/libkbase.a ../klog/libklog.a ../ktrace/libktrace.a ../khash/libkhash.a -lpthread

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
						-L../corRest -L../corJsonld -L../kalloc -L../kjson -L../kbase -L../klog -L../ktrace -L../khash \
						-lcorRest -lcorJsonld -lkalloc -lkjson -lkbase -lklog -lktrace -lkhash -lmicrohttpd -lssl -lcrypto -lpthread -lmosquitto $(ICU_LIBS) \
						-Wl,-rpath,'$$ORIGIN/../corRest:$$ORIGIN/../corJsonld:$$ORIGIN/../kalloc:$$ORIGIN/../kjson:$$ORIGIN/../kbase:$$ORIGIN/../klog:$$ORIGIN/../ktrace:$$ORIGIN/../khash'

%.o: %.c
					$(CC) $(CFLAGS) -c $< -o $@

%.i: %.c
					$(CC) $(CFLAGS) -c $^ -E > $@

-include $(LIB_DEPS)
