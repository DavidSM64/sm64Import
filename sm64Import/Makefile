################ Target Executable and Sources ###############

TARGET := sm64import

SRC_FILES  := \
	source.cpp

OBJ_DIR     = ./obj

##################### Compiler Options #######################

WIN64_CROSS = x86_64-w64-mingw32-
WIN32_CROSS = i686-w64-mingw32-
#CROSS     = $(WIN32_CROSS)
CXX       = $(CROSS)g++
LD        = $(CXX)

INCLUDES  = 
DEFS      = 
# Release flags
#CXXFLAGS  = -Wall -Wextra -O2 -std=c++11 $(INCLUDES) $(DEFS) -MMD
#LDFLAGS   = -s
# Debug flags
CXXFLAGS  = -Wall -Wextra -O0 -std=c++11 -g $(INCLUDES) $(DEFS) -MMD
LDFLAGS   =
LIBS      = 

OBJ_FILES = $(addprefix $(OBJ_DIR)/,$(SRC_FILES:.cpp=.o))
DEP_FILES = $(OBJ_FILES:.o=.d)

######################## Targets #############################

default: all

all: $(TARGET)

$(OBJ_DIR)/%.o: %.cpp
	@[ -d $(dir $@) ] || mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -o $@ -c $<

$(TARGET): $(OBJ_FILES)
	$(LD) $(LDFLAGS) -o $@ $^ $(LIBS)

clean:
	rm -f $(OBJ_FILES) $(DEP_FILES) $(TARGET) $(TARGET).exe
	-@[ -d $(OBJ_DIR) ] && rmdir --ignore-fail-on-non-empty $(OBJ_DIR)

.PHONY: all clean default

#################### Dependency Files ########################

-include $(DEP_FILES)
