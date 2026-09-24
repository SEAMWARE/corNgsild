#
# FILE            makefile
#
# AUTHOR          Ken Zangelin
#
# Copyright 2026 Seamware
# SPDX-License-Identifier: Apache-2.0
#
LIB_NAME      = corNgsild
LIB_SO        = lib$(LIB_NAME).so
LIB           = lib$(LIB_NAME).a
CC            = gcc
PREFIX        = ..
INCLUDE       = -I$(PREFIX)
DFLAGS        = -DANSI

#
# ICU "root" collation for orderBy string ordering (§ 7.6.2.1). OFF by default;
# build with 'make COR_WITH_ICU=1 ...' to link libicu and get the collation the
# specification names. The coraine CMake option COR_FEATURE_ICU_COLLATION must
# match this setting — it adds the matching -licui18n/-licuuc/-licudata to the
# final broker link, and coraine's makefile passes both from one variable.
#
# OFF by default because of what ON costs: three shared libraries and 39.2 MiB,
# of which libicudata is 31.6 MiB - nine times the size of the whole broker, for
# a collation table. That is the single largest thing a coraine deployment can
# put on a machine, and it is there for one sort order.
#
# What the default gives up is bounded and measured, not a vague approximation.
# Pure ASCII sorts identically either way (which is why it costs no ETSI test
# purpose - the suite's orderBy TPs sort "A", "B", "C"). Three things differ:
# punctuation no longer sorts before digits ('1one' before '_under'), a
# non-ASCII letter takes its byte weight rather than its base letter's ('äpple'
# after 'zebra' instead of beside 'apple'), and collation=<locale> is ignored.
# Replacing ICU with a root-collation implementation of our own, tailorings
# added on demand, is on the coraine roadmap; until then ON is one flag away.
#
COR_WITH_ICU  ?= 0
ifeq ($(COR_WITH_ICU),1)
DFLAGS       += -DCOR_WITH_ICU
ICU_CFLAGS   := $(shell pkg-config --cflags icu-i18n 2>/dev/null)
ICU_LIBS     := $(shell pkg-config --libs icu-i18n 2>/dev/null)
endif

#
# EXTRA_CFLAGS - the hook for a caller that needs to ADD flags to this build.
#
# It exists because DFLAGS cannot do that job. DFLAGS is a plain variable, so
# `make DFLAGS=...` REPLACES it - the command line beats the makefile, and the
# `DFLAGS += -DCOR_WITH_ICU` above is then ignored too, because a `+=` never
# appends to a variable set on the command line. A caller reaching for DFLAGS to
# add one flag therefore silently drops -DANSI and the ICU selection, and this
# lib gets compiled with the ASCII-approximation collation path (ldOrderSort.c
# has five #ifdef COR_WITH_ICU blocks) while the broker links against ICU -
# exactly the mismatch the COR_WITH_ICU comment above warns about.
#
# That is not hypothetical: coraine's coverage targets did it, so every coverage
# run measured a differently-compiled broker than the one that ships.
#
# EXTRA_CFLAGS is appended LAST, so a caller's -O0 / -Wno-error also win over the
# -O2 / -Werror here, which is what an instrumented build needs.
#
CFLAGS        = -Wall -Werror -O2 -fPIC $(DFLAGS) $(INCLUDE) $(ICU_CFLAGS) -MMD -MP $(EXTRA_CFLAGS)

#
# BUILD - which flavour of build this is, and where its objects live.
#
# Objects used to sit next to their sources, one set for every flavour, and that
# is a silent-wrong-answer machine: `make coverage` leaves instrumented objects
# behind, a later ordinary build finds them NEWER than the sources and relinks
# them into a binary that calls itself ordinary. That is not hypothetical - it
# is why `libs-rebuild` exists.
#
# A plain variable and not the target-specific `debug: CFLAGS += -g` this used
# to be: target-specific variables are not visible when the makefile is parsed,
# so a directory derived from them would be the same directory for every target.
#
BUILD        ?= debug
OBJDIR       := obj/$(BUILD)

ifeq ($(BUILD),debug)
CFLAGS       += -g -DDEBUG
endif

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
                ldInstanceWritten.c \
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
LIB_OBJS      = $(addprefix $(OBJDIR)/,$(LIB_SOURCES:.c=.o))
LIB_DEPS      = $(addprefix $(OBJDIR)/,$(LIB_SOURCES:.c=.d))

#
# $(OBJDIR)/.flags - the flags these objects were built with.
#
# The directory separates the flavours; this catches a change WITHIN one. A
# caller adding EXTRA_CFLAGS changes the compile line and nothing else: sources
# are untouched, objects stay newer than them, and make rebuilds nothing. The
# stamp is rewritten only when the flags actually differ, so its timestamp moves
# exactly when a rebuild is due, and every object depends on it.
#
FLAGSTAMP    := $(OBJDIR)/.flags

LIBS          = ../corRest/libcorRest.a ../corJsonld/libcorJsonld.a ../kalloc/libkalloc.a ../kjson/libkjson.a ../kbase/libkbase.a ../ktrace/libktrace.a ../khash/libkhash.a -lpthread

.PHONY: all clean test install i di ci

#
# Built per flavour, then STAGED to the repo root where every consumer expects
# them. Unconditionally: comparing timestamps would reintroduce the bug the
# object directories fix, since obj/debug/libX.a is easily older than a libX.a
# a coverage build left behind.
#
all: $(OBJDIR)/$(LIB_SO) $(OBJDIR)/$(LIB)
					@cp -f $(OBJDIR)/$(LIB) $(LIB)
					@cp -f $(OBJDIR)/$(LIB_SO) $(LIB_SO)

$(FLAGSTAMP): FORCE
					@mkdir -p $(OBJDIR)
					@echo '$(CFLAGS)' | cmp -s - $@ 2>/dev/null || echo '$(CFLAGS)' > $@

FORCE:

clean:
					rm -rf obj
					#
					# ...and the legacy in-tree artefacts. Objects live under obj/ now, but a tree
					# built before that still has .o/.d beside its sources - and, worse, .gcno:
					# gcovr reads those and reports a file nobody compiled as entirely unexecuted,
					# which once moved the published figure by three points.
					#
					rm -f *.o *.d *.gcno *.gcda
					rm -f $(LIB_OBJS)
					rm -f $(LIB_DEPS)
					rm -f $(LIB_SO)
					rm -f $(LIB)

i:          install

install:    all
					@mkdir -p $(PREFIX)/include/$(LIB_NAME)
					@mkdir -p $(PREFIX)/lib
#
# The installed header set is REPLACED, not added to. `cp *.h` alone never
# removes anything, so a header deleted from this repo lived on in the install
# tree and kept compiling - a deleted one survived its own removal that way and
# had to be deleted by hand.
#
					rm -f $(PREFIX)/include/$(LIB_NAME)/*.h
					cp *.h $(PREFIX)/include/$(LIB_NAME)/
					cp $(LIB) $(LIB_SO) $(PREFIX)/lib/

di:         debug install

ci:         clean install

cdi:        clean debug install

test:
					@echo "No tests yet"

#
# The staged artefacts are targets in their own right, so a caller can ask for
# `make libcorX.a` and get the current flavour's archive copied into place. The
# coverage target does exactly that, by name.
#
$(LIB): $(OBJDIR)/$(LIB)
					@cp -f $< $@

$(LIB_SO): $(OBJDIR)/$(LIB_SO)
					@cp -f $< $@

$(OBJDIR)/$(LIB):	$(LIB_OBJS)
					ar r $@ $(LIB_OBJS)
					ranlib $@

$(OBJDIR)/$(LIB_SO):	$(LIB_OBJS)
					$(CC) -shared $(LIB_OBJS) -o $@ \
						-L../corRest -L../corJsonld -L../kalloc -L../kjson -L../kbase -L../ktrace -L../khash \
						-lcorRest -lcorJsonld -lkalloc -lkjson -lkbase -lktrace -lkhash -lmicrohttpd -lssl -lcrypto -lpthread -lmosquitto $(ICU_LIBS) \
						-Wl,-rpath,'$$ORIGIN/../corRest:$$ORIGIN/../corJsonld:$$ORIGIN/../kalloc:$$ORIGIN/../kjson:$$ORIGIN/../kbase:$$ORIGIN/../ktrace:$$ORIGIN/../khash'

$(OBJDIR)/%.o: %.c $(FLAGSTAMP)
					@mkdir -p $(OBJDIR)
					$(CC) $(CFLAGS) -c $< -o $@

%.i: %.c
					$(CC) $(CFLAGS) -c $^ -E > $@

-include $(LIB_DEPS)
