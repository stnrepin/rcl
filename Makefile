CC ?= gcc
CCFLAGS := -std=c11
DBGFLAGS := -g -Wall -Wextra -Wpedantic
CCOBJFLAGS := $(CCFLAGS) -c

BIN_PATH := build
SRC_PATH := .

TARGET_NAME := rcl_test
TARGET_RELEASE := $(BIN_PATH)/release/$(TARGET_NAME)
TARGET_DEBUG := $(BIN_PATH)/debug/$(TARGET_NAME)

OBJ_REL_PATH := $(BIN_PATH)/release/obj
OBJ_DBG_PATH := $(BIN_PATH)/debug/obj

SRC := $(foreach x, $(SRC_PATH), $(wildcard $(addprefix $(x)/*,.c*)))
OBJ_RELEASE := $(addprefix $(OBJ_REL_PATH)/, $(addsuffix .o, $(notdir $(basename $(SRC)))))
OBJ_DEBUG := $(addprefix $(OBJ_DBG_PATH)/, $(addsuffix .o, $(notdir $(basename $(SRC)))))

DISTCLEAN_LIST := $(OBJ_RELEASE) \
                  $(OBJ_DEBUG)
CLEAN_LIST := $(TARGET_RELEASE) \
			  $(TARGET_DEBUG) \
			  $(DISTCLEAN_LIST)

default: makedir all

$(OBJ_REL_PATH)/%.o: $(SRC_PATH)/%.c*
	$(CC) $(CCOBJFLAGS) -o $@ $<

$(OBJ_DBG_PATH)/%.o: $(SRC_PATH)/%.c*
	$(CC) $(CCOBJFLAGS) $(DBGFLAGS) -o $@ $<

$(TARGET_RELEASE): $(OBJ_RELEASE)
	$(CC) $(CCFLAGS) -o $@ $(OBJ_RELEASE)

$(TARGET_DEBUG): $(OBJ_DEBUG)
	$(CC) $(CCFLAGS) $(DBGFLAGS) $(OBJ_DEBUG) -o $@

.PHONY: makedir
makedir:
	@mkdir -p $(BIN_PATH) $(OBJ_REL_PATH) $(OBJ_DBG_PATH)

.PHONY: all
all: $(TARGET_DEBUG)

.PHONY: release
release: $(TARGET_RELEASE)

.PHONY: debug
debug: $(TARGET_DEBUG)

.PHONY: clean
clean:
	@echo CLEAN $(CLEAN_LIST)
	@rm -f $(CLEAN_LIST)

.PHONY: distclean
distclean:
	@echo CLEAN $(DISTCLEAN_LIST)
	@rm -f $(DISTCLEAN_LIST)
