# Runs the pure logic - feature extraction, classification, the library - on
# the host under ASan/UBSan. No Flipper, no radio, no GUI.
#
#   make -C test

CC     ?= cc
CFLAGS := -std=gnu2x -Wall -Wextra -Werror -O1 -g
CFLAGS += -fsanitize=address,undefined -fno-omit-frame-pointer
CFLAGS += -I ..

FEATURES_SRC := host_features_test.c ../helpers/sib_features.c
CLASSIFY_SRC := host_classify_test.c ../helpers/sib_classify.c \
                ../helpers/sib_library.c ../helpers/sib_features.c
LIBRARY_SRC  := host_library_test.c ../helpers/sib_library.c \
                ../helpers/sib_classify.c ../helpers/sib_features.c

MOCKUP_SRC   := host_mockup_dump.c ../helpers/sib_classify.c \
                ../helpers/sib_library.c ../helpers/sib_features.c

BINS := host_features_test host_classify_test host_library_test

.PHONY: all run clean
all: run

host_features_test: $(FEATURES_SRC) sib_gen.h sib_test.h ../helpers/sib_features.h
	$(CC) $(CFLAGS) $(FEATURES_SRC) -o $@

host_classify_test: $(CLASSIFY_SRC) sib_gen.h sib_test.h ../helpers/sib_classify.h
	$(CC) $(CFLAGS) $(CLASSIFY_SRC) -o $@

host_library_test: $(LIBRARY_SRC) sib_test.h ../helpers/sib_library.h
	$(CC) $(CFLAGS) $(LIBRARY_SRC) -o $@

# Feeds tools_gen_mockups.py. Built without the sanitizers so the output is
# clean text, and not part of `run` - it asserts nothing, it only reports.
host_mockup_dump: $(MOCKUP_SRC) sib_gen.h
	$(CC) -std=gnu2x -Wall -Wextra -Werror -O1 -I .. $(MOCKUP_SRC) -o $@

run: $(BINS)
	./host_features_test
	@echo
	./host_classify_test
	@echo
	./host_library_test

clean:
	rm -rf $(BINS) host_mockup_dump mockup_data.txt *.dSYM
