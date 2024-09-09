CXX = g++
CXXFLAGS = -std=c++20 -Wall -Wextra -pedantic
LDFLAGS =

SRC_DIR = .
BUILD_DIR = build
BIN_DIR = .

SRCS = $(wildcard $(SRC_DIR)/*.cpp)
OBJS = $(SRCS:$(SRC_DIR)/%.cpp=$(BUILD_DIR)/%.o)
DEPS = $(OBJS:.o=.d)

# Debug build settings
DEBUG_DIR = $(BUILD_DIR)/debug
DEBUG_OBJS = $(SRCS:$(SRC_DIR)/%.cpp=$(DEBUG_DIR)/%.o)
DEBUG_DEPS = $(DEBUG_OBJS:.o=.d)
DEBUG_TARGET = $(BIN_DIR)/doip_uds_server_debug
DEBUG_CXXFLAGS = $(CXXFLAGS) -g -DDEBUG

# Release build settings
RELEASE_DIR = $(BUILD_DIR)/release
RELEASE_OBJS = $(SRCS:$(SRC_DIR)/%.cpp=$(RELEASE_DIR)/%.o)
RELEASE_DEPS = $(RELEASE_OBJS:.o=.d)
RELEASE_TARGET = $(BIN_DIR)/doip_uds_server
RELEASE_CXXFLAGS = $(CXXFLAGS) -O2 -DNDEBUG

.PHONY: all debug release clean

all: debug release

debug: $(DEBUG_DIR) $(DEBUG_TARGET)

release: $(RELEASE_DIR) $(RELEASE_TARGET)

$(DEBUG_DIR):
	mkdir -p $(DEBUG_DIR)

$(RELEASE_DIR):
	mkdir -p $(RELEASE_DIR)

$(DEBUG_TARGET): $(DEBUG_OBJS)
	$(CXX) $(LDFLAGS) -o $@ $^

$(RELEASE_TARGET): $(RELEASE_OBJS)
	$(CXX) $(LDFLAGS) -o $@ $^

$(DEBUG_DIR)/%.o: $(SRC_DIR)/%.cpp
	$(CXX) $(DEBUG_CXXFLAGS) -MMD -MP -c $< -o $@

$(RELEASE_DIR)/%.o: $(SRC_DIR)/%.cpp
	$(CXX) $(RELEASE_CXXFLAGS) -MMD -MP -c $< -o $@

-include $(DEBUG_DEPS)
-include $(RELEASE_DEPS)

clean:
	rm -rf $(BUILD_DIR) $(DEBUG_TARGET) $(RELEASE_TARGET)
