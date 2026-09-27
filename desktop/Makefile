# OrangeM Desktop (Windows x64) — кросс-сборка на Linux через mingw-w64
# Использование:  make            -> orangem.exe
#                 make clean

CXX      := x86_64-w64-mingw32-g++
WINDRES  := x86_64-w64-mingw32-windres
CXXFLAGS := -std=c++17 -O2 -Wall -Wextra -Wno-unused-parameter -municode -DUNICODE -D_UNICODE -Isrc
LDFLAGS  := -mwindows -static -static-libgcc -static-libstdc++ \
            -lwinhttp -lws2_32 -lgdi32 -lgdiplus -lcomctl32 -lcomdlg32 -lole32 -lshell32 -lshlwapi -luuid -luser32

SRC      := $(wildcard src/*.cpp)
RES      := res/resources.res
BIN      := orangem.exe

all: $(BIN)

$(RES): res/resources.rc res/app.ico
	$(WINDRES) -I res $< -O coff -o $@

$(BIN): $(SRC) $(RES)
	$(CXX) $(CXXFLAGS) $(SRC) $(RES) -o $@ $(LDFLAGS)
	@echo "built: $(BIN)"

clean:
	rm -f $(BIN) $(RES) src/*.o

.PHONY: all clean
