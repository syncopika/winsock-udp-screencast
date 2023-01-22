# make file for winsock client

CXX = g++

HEADER = headers/

# include gdiplus headers 
# careful about the paths! use the gdiplus folder that was included with your mingw installation.
INCLUDE = -IC:\winlibs-i686-posix-dwarf-gcc-12.2.0-mingw-w64ucrt-10.0.0-r2\mingw32\i686-w64-mingw32\include\gdiplus
SDL_INCLUDE = -IC:\libraries\SDL2-2.0.10\i686-w64-mingw32\include

FLAGS = -Wall -g -c -std=c++14

LIB = -lws2_32 -lmswsock -lmingw32

ALL_LINK_FLAGS = $(LIB) -static-libstdc++ -static-libgcc

# only server needs gdi
SERVER_LINK_FLAGS = -lgdi32 -lgdiplus

# only client needs sdl2 + sdl2_ttf
# note that here I put the SDL2_ttf lib files in the same directory as the SDL2 lib files for convenience
CLIENT_LINK_FLAGS = -LC:\libraries\SDL2-2.0.10\i686-w64-mingw32\lib -lSDL2main -lSDL2 -lSDL2_ttf

EXE = server.exe client.exe

SERVER_OBJ = server.o 

CLIENT_OBJ = client.o

all: $(EXE)

client.exe: $(CLIENT_OBJ)
	$(CXX) $(CLIENT_OBJ) $(ALL_LINK_FLAGS) $(CLIENT_LINK_FLAGS) -o $@
	
server.exe: $(SERVER_OBJ)
	$(CXX) $(SERVER_OBJ) $(ALL_LINK_FLAGS) $(SERVER_LINK_FLAGS) -o $@

client.o: client.cpp
	$(CXX) $(FLAGS) $(SDL_INCLUDE) $< -o $@
	
server.o: server.cpp 
	$(CXX) $(FLAGS) $(INCLUDE) $< -o $@
	
	
clean:
	rm *.o