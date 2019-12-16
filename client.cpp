#define _WIN32_WINNT 0x501
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <stdio.h>
#include <string>
#include <iostream>
#include <limits>
#include <ctime>
#include <utility>
#include <queue>
#include <vector>

// running into compilation error about WinMain not having defined reference
// need to define SDL_MAIN_HANDLED
// this is interesting? https://djrollins.com/2016/10/02/sdl-on-windows/
#define SDL_MAIN_HANDLED
#include <SDL2/SDL.h>

#define DEFAULT_PORT 2000
#define DEFAULT_BUFLEN 60000
#define MSG_HEADER 6 // this is the header that comes with image data chunk packets 

#define WINDOW_WIDTH 600
#define WINDOW_HEIGHT 400

void logSDLError(std::ostream &os, const std::string &msg){
	os << msg << " error: " << SDL_GetError() << std::endl;
}

int main(void){
	
	if(SDL_Init(SDL_INIT_VIDEO) != 0){
		logSDLError(std::cout, "SDL_Init Error: ");
		return 1;
	}
	
	printf("starting client...\n");
	
	// create a window 
	SDL_Window *window = SDL_CreateWindow("Hello World!", 100, 100, WINDOW_WIDTH, WINDOW_HEIGHT, SDL_WINDOW_SHOWN);
	if(window == nullptr){
		logSDLError(std::cout, "SDL_CreateWindow Error: ");
		SDL_Quit();
		return 1;
	}
	
	// create the renderer to render the window with 
	SDL_Renderer *renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
	if(renderer == nullptr){
		SDL_DestroyWindow(window);
		logSDLError(std::cout,"SDL_CreateRenderer Error: ");
		SDL_Quit();
		return 1;
	}
	
	// variables for networking
	WSADATA wsaData;
	int iResult;
	char recvbuf[DEFAULT_BUFLEN + MSG_HEADER];
	int recvbuflen = DEFAULT_BUFLEN + MSG_HEADER;
	int rtnVal;
	
	iResult = WSAStartup(MAKEWORD(2,2), &wsaData);
	if(iResult != 0){
		printf("WSAStartup failed: %d\n", iResult);
		return 1;
	}
	
	struct sockaddr_in servAddr;
	ZeroMemory(&servAddr, sizeof(servAddr));
	servAddr.sin_family = AF_INET;

	const char* theIp = "127.0.0.1";
	int size = sizeof(servAddr);
	iResult = WSAStringToAddressA((LPSTR)theIp, AF_INET, NULL, (struct sockaddr *)&servAddr, &size);
	if(iResult != 0){
		printf("getaddrinfo failed: %d\n", iResult);
		WSACleanup();
	}else{
		servAddr.sin_port = htons(DEFAULT_PORT);
	}
	
	SOCKET connectSocket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if(connectSocket == INVALID_SOCKET){
		printf("socket() failed\n");
		WSACleanup();
		return 1;
	}

	
	printf("preparing to message server...\n");
	std::string str = "hellooo there"; 
	const char* msg = str.c_str();
	
	bool sentInitMsg = false;
	
	// functor for comparing for priority_queue 
	struct Compare{
		bool operator()(const std::pair<int, UINT8*> a, const std::pair<int, UINT8*> b){
			return a.first > b.first; // why isn't it a.first < b.first?
									  // I think it's because priority_queue by default sorts by descending order.
		}
	};
	
	// buffer to hold all pixel data 
	// use a priority queue (or min heap) to keep order of the packets since they could come in 
	// out of order. 
	// use a pair to store the data like this: (packetNum, pixelDataArray) in the queue. sort by packetNum.
	// https://stackoverflow.com/questions/37318537/deciding-priority-in-case-of-pairint-int-inside-priority-queue
	// https://stackoverflow.com/questions/12508496/comparators-in-stl
	// https://stackoverflow.com/questions/356950/what-are-c-functors-and-their-uses
	std::priority_queue<std::pair<int, UINT8*>, 
						std::vector<std::pair<int, UINT8*>>,
						Compare> pqueue;
	
	int packetsToExpect = 0;
	int packetsReceived = 0;
	while(1){
		// clear the recv buffer 
		ZeroMemory(recvbuf, recvbuflen);
		
		// send msg 
		if(!sentInitMsg){
			int bytesSent = sendto(connectSocket, msg, str.length(), 0, (struct sockaddr *)&servAddr, size);
			if(bytesSent < 0){
				printf("sendto failed.\n");
				exit(1);
			}
			sentInitMsg = true;
		}
							
		// if we get more than half the packets (probably rethink this later)
		if((float)packetsReceived > (packetsToExpect / 2)){
			
			std::vector<UINT8> pixelData;
			std::cout << "got at least half the packets..." << std::endl;
			
			int lastPacketNum = 0;
			while(!pqueue.empty()){
				std::cout << "packet number: " << pqueue.top().first << std::endl;
				
				// collect all the data for the image
				// we can cheat a little for now. we know exactly how big the image is coming in,
				// so we know exactly how many pixel data bytes can go into each packet. 
				// for now, let's use this knowledge (i.e. there should be 16 packets, each one with 
				// 60000 bytes of data except the last packet, which has 50000). this is beacause we hardcoded
				// the width and height of the image, which is 600 and 400, respectively.
				
				// neat! http://forums.codeguru.com/showthread.php?509825-std-vectors-Append-chunk-of-data-to-the-end
				// https://stackoverflow.com/questions/7593086/why-use-non-member-begin-and-end-functions-in-c11
				int currPacketNum = pqueue.top().first;
				UINT8* data = pqueue.top().second;
				int dataSize = (currPacketNum == 16) ? 50000 : 60000; // last packet has a different length of bytes
				
				if(currPacketNum != lastPacketNum + 1){
					for(int i = lastPacketNum+1; i < currPacketNum; i++){
						dataSize = (i == 16) ? 50000 : 60000;
						std::cout << "filling in data for missing packet num: " << i << std::endl;
						// fill with rgba(255,255,255,255) for any missing packets between last packet num and current 
						UINT8* filler = new UINT8[dataSize];
						memset(filler, 255, dataSize);
						pixelData.insert(pixelData.end(), filler, filler+dataSize);
						delete filler;
					}
				}
				
				/* checking values here 
				for(int j = 0; j < 10; j++){
					std::cout << "packet " << currPacketNum << " data: " << (int)data[j] << std::endl;
				}*/
				
				pixelData.insert(pixelData.end(), data, data+dataSize);

				delete data;
				lastPacketNum = currPacketNum;
				pqueue.pop();
			}
			
			// if missing last packet, fill in 
			std::cout << "lastPacketNum seen: " << lastPacketNum << std::endl;
			std::cout << "num packets expected: " << packetsToExpect << std::endl;
			for(int i = lastPacketNum; i < packetsToExpect; i++){
				int size = (i == 16) ? 50000 : 60000;
				UINT8* filler = new UINT8[size];
				memset(filler, 0, size);
				pixelData.insert(pixelData.end(), filler, filler+size);
				delete filler;
			}
			
			std::cout << "--------------" << std::endl;
			// form image?
			// use sdl to create a surface with the pixel data.
			// turn the surface into a texture. put texture in renderer
			// https://stackoverflow.com/questions/21007329/what-is-an-sdl-renderer
			// try this: https://gamedev.stackexchange.com/questions/102490/fastest-way-to-render-image-data-from-buffer
			// this might help too? https://www.gamedev.net/forums/topic/683956-blit-a-byte-array-of-pixels-to-screen-in-sdl-fast/
			std::cout << "the total size of the image data in bytes: " << pixelData.size() << std::endl;
			uint8_t* imageData = new uint8_t[(int)pixelData.size()];// = (uint8_t *)&pixelData[0];
			
			
			SDL_Texture* texBuf = SDL_CreateTexture(
									renderer,
									SDL_PIXELFORMAT_ARGB8888,
									SDL_TEXTUREACCESS_STREAMING,
									WINDOW_WIDTH,
									WINDOW_HEIGHT);
			
			int pitch = WINDOW_WIDTH * 4; // 4 bytes per pixel
			SDL_LockTexture(texBuf, NULL, (void**)&imageData, &pitch);
			
			// fill the texture pixel buffer
			memcpy(imageData, (uint8_t *)&pixelData[0], pixelData.size());
			
			SDL_UnlockTexture(texBuf);
			SDL_RenderCopy(renderer, texBuf, NULL, NULL);
			SDL_RenderPresent(renderer);	
		}
	
		// how do we know when we should process all the chunks of a frame? we should expect packet loss,
		// so we don't really know when we've seen the last packet of a frame?
		// since on the server side we have a sleep in between frames, is it very unlikely that a packet for the 
		// next frame could slip in with the packets of the current frame? so we can look at frame id maybe?
		
		// receive msg
		rtnVal = recvfrom(connectSocket, recvbuf, recvbuflen, 0, (struct sockaddr *)&servAddr, &size);
		if(rtnVal > 0){	
			// check recvbuf 
			if(recvbuf[0] == 'i'){
				printf("Got data about the next image frame: %s\n", recvbuf);
				printf("num packets to expect for image: %d\n", (int)recvbuf[4]);
				std::cout << "--------------" << std::endl;
				
				packetsToExpect = (int)recvbuf[4];
			}else if(recvbuf[0] == 'p'){
				
				packetsReceived++;
				UINT8* pixels = (UINT8*)recvbuf;
				
				// packet chunk 
				int packetNum = (int)pixels[1];
				std::cout << "frame id: " << (int)pixels[3] << std::endl;
				std::cout << "packet num: " << packetNum << std::endl;
				std::cout << "data size for this packet: " << (int)pixels[5] << std::endl;
				std::cout << "first byte of data: " << (int)pixels[6] << std::endl;
				
				// need to make a copy of the received data 
				// note that this is hardcoded right now based on assumptions of the image data!
				int dataSize = (packetNum == 16 ? 50000 : 60000);
				UINT8* pixelsCopy = new UINT8[dataSize];
				memcpy(pixelsCopy, recvbuf+6, dataSize);
				
				std::pair<int, UINT8*> packetChunk(packetNum, pixelsCopy);
				pqueue.push(packetChunk);
				
				std::cout << "--------------" << std::endl;
			}else{
				// frame data. rebuild image from the chunks and display
				printf("Message received from server: %s\n", recvbuf);
			}
		}else if(rtnVal == 0){
			printf("connection closed.\n");
			exit(1);
		}else{
			printf("recv failed: %d\n", WSAGetLastError());
			exit(1);
		}
		
		// don't sleep unless you want to lose packets on purpose...
		//Sleep(200);
	}
	
	WSACleanup();
	SDL_Quit();
	return 0;
}
