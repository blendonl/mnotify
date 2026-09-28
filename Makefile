CC       = x86_64-w64-mingw32-gcc
WINDRES  = x86_64-w64-mingw32-windres

VERSION  = 0.2.0

VER_MAJOR := $(word 1,$(subst ., ,$(VERSION)))
VER_MINOR := $(word 2,$(subst ., ,$(VERSION)))
VER_PATCH := $(word 3,$(subst ., ,$(VERSION)))

CFLAGS   = -O2 -s -flto -mwindows \
           -DUNICODE -D_UNICODE \
           -DMNOTIFY_VERSION='"$(VERSION)"' \
           -Wall -Wextra -Wno-unused-parameter \
           $(CFLAGS_EXTRA)

CFLAGS_EXTRA ?=

RCFLAGS  = -DVER_MAJOR=$(VER_MAJOR) \
           -DVER_MINOR=$(VER_MINOR) \
           -DVER_PATCH=$(VER_PATCH)

LDLIBS   = -luser32 -lgdi32 -lshell32 -lversion -ldwmapi -lshcore \
           -lole32 -luuid -lshlwapi -lpropsys

SRC_DIR  = src

MNOTIFY_SRCS = $(SRC_DIR)/mnotify.c    \
               $(SRC_DIR)/tray.c       \
               $(SRC_DIR)/tray_proto.c \
               $(SRC_DIR)/popup.c      \
               $(SRC_DIR)/menu.c       \
               $(SRC_DIR)/toast_xml.c  \
               $(SRC_DIR)/toasts.c     \
               $(SRC_DIR)/activate.c   \
               $(SRC_DIR)/log.c

MNOTIFY_OBJS = $(MNOTIFY_SRCS:.c=.o)
RES_OBJ      = $(SRC_DIR)/mnotify.res.o

TARGET       = mnotify.exe

DISTNAME   = mnotify-$(VERSION)-win64
DISTDIR    = dist/$(DISTNAME)
DIST_FILES = README.md CHANGELOG.md MANUAL-TESTS.md LICENSE

HOST_CC   = cc
TEST_DIR  = test
TEST_BINS = $(TEST_DIR)/test_tray_proto $(TEST_DIR)/test_toast_xml

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

$(TARGET): $(MNOTIFY_OBJS) $(RES_OBJ)
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

$(SRC_DIR)/%.o: $(SRC_DIR)/%.c $(SRC_DIR)/mnotify.h $(SRC_DIR)/tray_proto.h $(SRC_DIR)/toast_xml.h $(SRC_DIR)/log.h
	@echo "  CC    $<"
	$(CC) $(CFLAGS) -c -o $@ $<

$(RES_OBJ): $(SRC_DIR)/mnotify.rc $(SRC_DIR)/mnotify.exe.manifest
	@echo "  RC    $<"
	$(WINDRES) $(RCFLAGS) -I$(SRC_DIR) -O coff -i $< -o $@

dist: $(TARGET)
	@echo "  DIST  $(DISTNAME)"
	rm -rf "$(DISTDIR)" "dist/$(DISTNAME).zip"
	mkdir -p "$(DISTDIR)"
	cp $(TARGET)     "$(DISTDIR)/"
	cp $(DIST_FILES) "$(DISTDIR)/"
	cd dist && python3 -m zipfile -c "$(DISTNAME).zip" "$(DISTNAME)"
	@echo "  ->    dist/$(DISTNAME).zip"

$(TEST_DIR)/test_tray_proto: $(TEST_DIR)/test_tray_proto.c $(TEST_DIR)/tests.h $(SRC_DIR)/tray_proto.c $(SRC_DIR)/tray_proto.h
	@echo "  HOSTCC $@"
	$(HOST_CC) -O1 -Wall -Wextra -I$(SRC_DIR) -o $@ $(TEST_DIR)/test_tray_proto.c $(SRC_DIR)/tray_proto.c

$(TEST_DIR)/test_toast_xml: $(TEST_DIR)/test_toast_xml.c $(TEST_DIR)/tests.h $(SRC_DIR)/toast_xml.c $(SRC_DIR)/toast_xml.h
	@echo "  HOSTCC $@"
	$(HOST_CC) -O1 -Wall -Wextra -I$(SRC_DIR) -o $@ $(TEST_DIR)/test_toast_xml.c $(SRC_DIR)/toast_xml.c

test: $(TEST_BINS)
	@echo "  TEST"
	@fail=0; for t in $(TEST_BINS); do ./$$t || fail=1; done; \
	 if [ $$fail -ne 0 ]; then echo "  TESTS FAILED"; exit 1; fi; \
	 echo "  all tests passed"

clean:
	rm -f $(TARGET) $(MNOTIFY_OBJS) $(RES_OBJ) $(TEST_BINS)
	rm -f .version-*
