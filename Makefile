CC       = x86_64-w64-mingw32-gcc
WINDRES  = x86_64-w64-mingw32-windres

VERSION  = 0.4.0

VER_MAJOR := $(word 1,$(subst ., ,$(VERSION)))
VER_MINOR := $(word 2,$(subst ., ,$(VERSION)))
VER_PATCH := $(word 3,$(subst ., ,$(VERSION)))

CFLAGS   = -O2 -s -flto -mwindows \
           -DUNICODE -D_UNICODE \
           -DMNOTIFY_VERSION='"$(VERSION)"' \
           -Wall -Wextra -Wno-unused-parameter \
           -I$(LUA_DIR) \
           $(CFLAGS_EXTRA)

CFLAGS_EXTRA ?=

RCFLAGS  = -DVER_MAJOR=$(VER_MAJOR) \
           -DVER_MINOR=$(VER_MINOR) \
           -DVER_PATCH=$(VER_PATCH)

LDLIBS   = -luser32 -lgdi32 -lshell32 -lversion -ldwmapi -lshcore \
           -lole32 -luuid -lshlwapi -lpropsys -lm

SRC_DIR  = src
LUA_DIR  = vendor/lua/src

MNOTIFY_SRCS = $(SRC_DIR)/mnotify.c    \
               $(SRC_DIR)/tray.c       \
               $(SRC_DIR)/tray_proto.c \
               $(SRC_DIR)/popup.c      \
               $(SRC_DIR)/menu.c       \
               $(SRC_DIR)/toast_xml.c  \
               $(SRC_DIR)/toasts.c     \
               $(SRC_DIR)/history.c    \
               $(SRC_DIR)/icons.c      \
               $(SRC_DIR)/history_list.c \
               $(SRC_DIR)/activate.c   \
               $(SRC_DIR)/config.c     \
               $(SRC_DIR)/lua_api.c    \
               $(SRC_DIR)/anim.c       \
               $(SRC_DIR)/log.c

LUA_SRCS  = $(LUA_DIR)/lapi.c       \
            $(LUA_DIR)/lauxlib.c    \
            $(LUA_DIR)/lbaselib.c   \
            $(LUA_DIR)/lcode.c      \
            $(LUA_DIR)/lcorolib.c   \
            $(LUA_DIR)/lctype.c     \
            $(LUA_DIR)/ldblib.c     \
            $(LUA_DIR)/ldebug.c     \
            $(LUA_DIR)/ldo.c        \
            $(LUA_DIR)/ldump.c      \
            $(LUA_DIR)/lfunc.c      \
            $(LUA_DIR)/lgc.c        \
            $(LUA_DIR)/linit.c      \
            $(LUA_DIR)/liolib.c     \
            $(LUA_DIR)/llex.c       \
            $(LUA_DIR)/lmathlib.c   \
            $(LUA_DIR)/lmem.c       \
            $(LUA_DIR)/loadlib.c    \
            $(LUA_DIR)/lobject.c    \
            $(LUA_DIR)/lopcodes.c   \
            $(LUA_DIR)/loslib.c     \
            $(LUA_DIR)/lparser.c    \
            $(LUA_DIR)/lstate.c     \
            $(LUA_DIR)/lstring.c    \
            $(LUA_DIR)/lstrlib.c    \
            $(LUA_DIR)/ltable.c     \
            $(LUA_DIR)/ltablib.c    \
            $(LUA_DIR)/ltm.c        \
            $(LUA_DIR)/lundump.c    \
            $(LUA_DIR)/lutf8lib.c   \
            $(LUA_DIR)/lvm.c        \
            $(LUA_DIR)/lzio.c

MNOTIFY_OBJS = $(MNOTIFY_SRCS:.c=.o)
LUA_OBJS     = $(LUA_SRCS:.c=.o)
RES_OBJ      = $(SRC_DIR)/mnotify.res.o

TARGET       = mnotify.exe

DISTNAME   = mnotify-$(VERSION)-win64
DISTDIR    = dist/$(DISTNAME)
DIST_FILES = README.md CHANGELOG.md MANUAL-TESTS.md LICENSE THIRD-PARTY-NOTICES.md

HOST_CC   = cc
TEST_DIR  = test
TEST_BINS = $(TEST_DIR)/test_tray_proto $(TEST_DIR)/test_toast_xml \
            $(TEST_DIR)/test_lua_api $(TEST_DIR)/test_anim $(TEST_DIR)/test_history_list

HOST_LUA_DIR  = $(TEST_DIR)/lua-host
HOST_LUA_OBJS = $(patsubst $(LUA_DIR)/%.c,$(HOST_LUA_DIR)/%.o,$(LUA_SRCS))
HOST_LUA_LIB  = $(TEST_DIR)/liblua-host.a

.PHONY: all bump clean dist test print-version

all: $(TARGET)

bump:
	-git fetch --tags --quiet
	python3 tools/bump.py

print-version:
	@echo $(VERSION)

VERSION_STAMP = .version-$(VERSION)

$(VERSION_STAMP):
	@rm -f .version-*
	@touch $@

$(MNOTIFY_OBJS) $(RES_OBJ): $(VERSION_STAMP)

$(TARGET): $(MNOTIFY_OBJS) $(LUA_OBJS) $(RES_OBJ)
	@echo "  LINK  $@"
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

$(SRC_DIR)/log.o: $(SRC_DIR)/log.c $(SRC_DIR)/log.h
	@echo "  CC    $<"
	$(CC) $(CFLAGS) -c -o $@ $<

$(SRC_DIR)/tray_proto.o: $(SRC_DIR)/tray_proto.c $(SRC_DIR)/tray_proto.h
	@echo "  CC    $<"
	$(CC) $(CFLAGS) -c -o $@ $<

$(SRC_DIR)/toast_xml.o: $(SRC_DIR)/toast_xml.c $(SRC_DIR)/toast_xml.h
	@echo "  CC    $<"
	$(CC) $(CFLAGS) -c -o $@ $<

$(SRC_DIR)/history_list.o: $(SRC_DIR)/history_list.c $(SRC_DIR)/history_list.h
	@echo "  CC    $<"
	$(CC) $(CFLAGS) -c -o $@ $<

$(SRC_DIR)/lua_api.o: $(SRC_DIR)/lua_api.c $(SRC_DIR)/lua_api.h $(SRC_DIR)/config_types.h
	@echo "  CC    $<"
	$(CC) $(CFLAGS) -c -o $@ $<

$(SRC_DIR)/anim.o: $(SRC_DIR)/anim.c $(SRC_DIR)/anim.h $(SRC_DIR)/config_types.h
	@echo "  CC    $<"
	$(CC) $(CFLAGS) -c -o $@ $<

$(SRC_DIR)/%.o: $(SRC_DIR)/%.c $(SRC_DIR)/mnotify.h $(SRC_DIR)/tray_proto.h $(SRC_DIR)/toast_xml.h $(SRC_DIR)/log.h \
                $(SRC_DIR)/config_types.h $(SRC_DIR)/lua_api.h $(SRC_DIR)/anim.h $(SRC_DIR)/history_list.h
	@echo "  CC    $<"
	$(CC) $(CFLAGS) -c -o $@ $<

$(LUA_DIR)/%.o: $(LUA_DIR)/%.c
	@echo "  CC    $<"
	$(CC) $(CFLAGS) -DLUA_COMPAT_5_3 -c -o $@ $<

$(RES_OBJ): $(SRC_DIR)/mnotify.rc $(SRC_DIR)/mnotify.exe.manifest
	@echo "  RC    $<"
	$(WINDRES) $(RCFLAGS) -I$(SRC_DIR) -O coff -i $< -o $@

dist: $(TARGET)
	@echo "  DIST  $(DISTNAME)"
	rm -rf "$(DISTDIR)" "dist/$(DISTNAME).zip"
	mkdir -p "$(DISTDIR)"
	mkdir -p "$(DISTDIR)/config" "$(DISTDIR)/meta"
	cp $(TARGET)     "$(DISTDIR)/"
	cp $(DIST_FILES) "$(DISTDIR)/"
	cp config/init.lua config/.luarc.json "$(DISTDIR)/config/"
	cp meta/mnotify.lua "$(DISTDIR)/meta/"
	cd dist && python3 -m zipfile -c "$(DISTNAME).zip" "$(DISTNAME)"
	@echo "  ->    dist/$(DISTNAME).zip"

$(TEST_DIR)/test_tray_proto: $(TEST_DIR)/test_tray_proto.c $(TEST_DIR)/tests.h $(SRC_DIR)/tray_proto.c $(SRC_DIR)/tray_proto.h
	@echo "  HOSTCC $@"
	$(HOST_CC) -O1 -Wall -Wextra -I$(SRC_DIR) -o $@ $(TEST_DIR)/test_tray_proto.c $(SRC_DIR)/tray_proto.c

$(TEST_DIR)/test_toast_xml: $(TEST_DIR)/test_toast_xml.c $(TEST_DIR)/tests.h $(SRC_DIR)/toast_xml.c $(SRC_DIR)/toast_xml.h
	@echo "  HOSTCC $@"
	$(HOST_CC) -O1 -Wall -Wextra -I$(SRC_DIR) -o $@ $(TEST_DIR)/test_toast_xml.c $(SRC_DIR)/toast_xml.c

$(HOST_LUA_DIR)/%.o: $(LUA_DIR)/%.c
	@mkdir -p $(HOST_LUA_DIR)
	@echo "  HOSTCC $<"
	$(HOST_CC) -O1 -w -DLUA_USE_POSIX -DLUA_COMPAT_5_3 -c -o $@ $<

$(HOST_LUA_LIB): $(HOST_LUA_OBJS)
	@echo "  AR     $@"
	ar rcs $@ $^

$(TEST_DIR)/test_lua_api: $(TEST_DIR)/test_lua_api.c $(TEST_DIR)/tests.h $(SRC_DIR)/lua_api.c $(SRC_DIR)/lua_api.h $(SRC_DIR)/config_types.h $(HOST_LUA_LIB)
	@echo "  HOSTCC $@"
	$(HOST_CC) -O1 -Wall -Wextra -I$(SRC_DIR) -I$(LUA_DIR) -o $@ $(TEST_DIR)/test_lua_api.c $(SRC_DIR)/lua_api.c $(HOST_LUA_LIB) -lm

$(TEST_DIR)/test_anim: $(TEST_DIR)/test_anim.c $(TEST_DIR)/tests.h $(SRC_DIR)/anim.c $(SRC_DIR)/anim.h $(SRC_DIR)/config_types.h
	@echo "  HOSTCC $@"
	$(HOST_CC) -O1 -Wall -Wextra -I$(SRC_DIR) -o $@ $(TEST_DIR)/test_anim.c $(SRC_DIR)/anim.c

$(TEST_DIR)/test_history_list: $(TEST_DIR)/test_history_list.c $(TEST_DIR)/tests.h $(SRC_DIR)/history_list.c $(SRC_DIR)/history_list.h
	@echo "  HOSTCC $@"
	$(HOST_CC) -O1 -Wall -Wextra -I$(SRC_DIR) -o $@ $(TEST_DIR)/test_history_list.c $(SRC_DIR)/history_list.c -lm

test: $(TEST_BINS)
	@echo "  TEST"
	@fail=0; for t in $(TEST_BINS); do ./$$t || fail=1; done; \
	 if [ $$fail -ne 0 ]; then echo "  TESTS FAILED"; exit 1; fi; \
	 echo "  all tests passed"

clean:
	rm -f $(TARGET) $(MNOTIFY_OBJS) $(LUA_OBJS) $(RES_OBJ) $(TEST_BINS) $(HOST_LUA_LIB)
	rm -rf $(HOST_LUA_DIR)
	rm -f .version-*
