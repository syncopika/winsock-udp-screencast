// server using winsock2
/* helpful stuff?

https://dzone.com/articles/sdl2-pixel-drawing
https://stackoverflow.com/questions/22050413/c-get-raw-pixel-data-from-hbitmap
https://programmersheaven.com/discussion/385410/unable-to-read-bitmap-data-from-hbitmap
https://docs.microsoft.com/en-us/windows/win32/medfound/image-stride

*/

#define _WIN32_WINNT 0x501 // https://stackoverflow.com/questions/36420044/winsock-server-and-client-c-code-getaddrinfo-was-not-declared-in-this-scope

#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <cassert>
#include <cmath>
#include <stdio.h>
#include <stdlib.h>
#include <iostream>
#include <gdiplus.h>  // for screen capture 
#include <memory>     // for screen capture 
#include <cstring>    // memcpy

#define DEFAULT_PORT 2000 // for some reason 27015 doesn't work!? (even though it's used in the microsoft tutorial)
#define DEFAULT_BUFLEN 60000
#define CLIENT_WIDTH 600  // the width of the client application screen 
#define CLIENT_HEIGHT 400 // the height of the client application screen 

using namespace Gdiplus;

// get pixel data of screen
// let's try taking a whole screen capture then resizing
BitmapData* screenCapture(){
	
	int x1 = 0; // top left x coord 
	int y1 = 0; // top left y coord
	
	int x2 = GetSystemMetrics(SM_CXSCREEN); // screen bottom right x coord //600
	int y2 = GetSystemMetrics(SM_CYSCREEN); // screen bottom right y coord //400
	
	int width = x2 - x1;
	int height = y2 - y1;

    HDC hDc = CreateCompatibleDC(0);
    HBITMAP hBmp = CreateCompatibleBitmap(GetDC(NULL), width, height);
    SelectObject(hDc, hBmp);
    BitBlt(hDc, 0, 0, width, height, GetDC(0), 0, 0, SRCCOPY);
	
	// capture the cursor
	CURSORINFO screenCursor = {sizeof(screenCursor)};
	GetCursorInfo(&screenCursor);
	if(screenCursor.flags == CURSOR_SHOWING){
		RECT rcWnd;
		HWND hwnd = GetDesktopWindow();
		GetWindowRect(hwnd, &rcWnd);
		ICONINFO iconInfo = {sizeof(iconInfo)};
		GetIconInfo(screenCursor.hCursor, &iconInfo);
		int cursorX = screenCursor.ptScreenPos.x - iconInfo.xHotspot;
		int cursorY = screenCursor.ptScreenPos.y - iconInfo.yHotspot;
		BITMAP cursorBMP = {0};
		GetObject(iconInfo.hbmColor, sizeof(cursorBMP), &cursorBMP);
		DrawIconEx(hDc, cursorX, cursorY, screenCursor.hCursor, cursorBMP.bmWidth, cursorBMP.bmHeight, 0, NULL, DI_NORMAL);
	}
	
	// resize bitmap 
	Bitmap* bitmap = Bitmap::FromHBITMAP(hBmp, NULL);
	float xScaleFactor = (float)CLIENT_WIDTH / bitmap->GetWidth();
	float yScaleFactor = (float)CLIENT_HEIGHT / bitmap->GetHeight();

	Image* temp = new Bitmap(CLIENT_WIDTH, CLIENT_HEIGHT);
	Graphics g(temp);
	
	g.ScaleTransform(xScaleFactor, yScaleFactor);
	g.DrawImage(bitmap, 0, 0);
	Bitmap* rescaledBitmap = dynamic_cast<Bitmap*>(temp); // the image is actually a bitmap
	
    // return the pixel data from the bitmap
	BitmapData* bmpData = new BitmapData;
	Rect rect(0, 0, CLIENT_WIDTH, CLIENT_HEIGHT);
    rescaledBitmap->LockBits(&rect, ImageLockModeRead, PixelFormat32bppARGB, bmpData);
	
	DeleteObject(temp);
	DeleteObject(bitmap);
	DeleteObject(hBmp);
	
    return bmpData; // don't forget to delete when done sending
}

SOCKET constructSocket(){
	SOCKET sockRtnVal = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if(sockRtnVal == INVALID_SOCKET){
		printf("socket() failed\n");
		WSACleanup();
		exit(1);
	}
	return sockRtnVal;
}

// checks for SOCKET_ERROR 
// this error can come up when trying to bind, listen, send 
void socketErrorCheck(int returnValue, SOCKET socketToClose, const char* action){
	const char* actionAttempted = action;
	if(returnValue == SOCKET_ERROR){
		printf("socket error. %s failed with error: %d\n", actionAttempted, WSAGetLastError());
	}else{
		// everything's fine, probably...
		return;
	}
	closesocket(socketToClose);
	WSACleanup();
	exit(1);
}


uint8_t* makeScreenCapBuffer(uint8_t* data, int dataStart, int chunkNum, int frameId, int dataSize){
	// we need 6 bytes to indicate the message type, packet num, the frame id of the frame this fragment belongs to,
	// and the size of the pixel data 
	std::cout << "need to send: " << dataSize << " bytes for packet: " << chunkNum << ", frame id: " << frameId << std::endl;
	uint8_t* buf = new uint8_t[6+dataSize];
	buf[0] = 'p'; // p for packet?
	buf[1] = chunkNum;
	buf[2] = 'f'; // f for frame id 
	buf[3] = frameId;
	buf[4] = 'd'; // d for data 
	buf[5] = 1; //dataSize; - whoops, I'm an idiot. this is a UNIT8 buffer, which means the max number a byte can represent is 255.
				// so 60000 definitely cannot be represented in a byte lol. Use a 1 as a placeholder for now.
	memcpy(buf+6, data+dataStart, dataSize);
	return buf;	
}

void sendCurrentScreencap(int frameIdNum, int sendSocket, struct sockaddr* clientAddr, int clientAddrLen){
		
	BitmapData* screenCap = screenCapture();
	uint8_t* pixels = (uint8_t*)screenCap->Scan0;
	
	std::cout << "stride: " << screenCap->Stride << std::endl;
	std::cout << "width: " << screenCap->Width << std::endl;
	std::cout << "height: " << screenCap->Height << std::endl;
	
	// use stride to help determine how big the pixel buffer should be 
	int totalBytes = screenCap->Stride * screenCap->Height;
	std::cout << "total bytes of image: " << totalBytes << std::endl;
	
	uint8_t* pixelBuffer = new uint8_t[totalBytes];
	memcpy(pixelBuffer, pixels, totalBytes);
	
	// how many udp packets do we need to send that make up the image 
	int numPacketsToSend = (int)ceil((double)totalBytes / DEFAULT_BUFLEN);
	std::cout << "num packets to send: " << numPacketsToSend << std::endl;
	
	// send a message first telling how many packets to expect and a unique id
	// for this screenshot
	char* newFrameMessage = new char[6]; // 5 bytes needed 
	newFrameMessage[0] = 'i'; // some marker to indicate what kind of message this is (the initial one before the data comes, so 'i'?)
	newFrameMessage[1] = 'f'; // frame id 
	newFrameMessage[2] = (frameIdNum % 100); // don't let the number go higher than 100? we can reuse nums
	newFrameMessage[3] = 'p'; // num packets to expect
	newFrameMessage[4] = numPacketsToSend;
	newFrameMessage[5] = '\0';
	
	int sendResult = sendto(sendSocket, newFrameMessage, 6, 0, clientAddr, clientAddrLen);
	if(sendResult < 0){
		printf("sendto failed.\n");
		exit(1);
	}
	delete newFrameMessage;
	
	// then send image chunks
	int totalBytesRem = totalBytes;
	int dataStart = 0;
	for(int i = 0; i < numPacketsToSend; i++){
		int dataSize = DEFAULT_BUFLEN > totalBytesRem ? totalBytesRem : DEFAULT_BUFLEN; 
		
		uint8_t* buffer = makeScreenCapBuffer(pixelBuffer, dataStart, i+1, frameIdNum, dataSize);
		sendResult = sendto(sendSocket, (char *)buffer, dataSize+6, 0, clientAddr, clientAddrLen);
		if(sendResult < 0){
			printf("sendto failed.\n");
			exit(1);
		}
		
		totalBytesRem -= dataSize;
		dataStart += dataSize;
		std::cout << "rem bytes to send: " << totalBytesRem << std::endl;
		delete buffer;
	}
	
	std::cout << "sent all packets for frame: " << frameIdNum << std::endl;
	std::cout << "--------------" << std::endl;
	
	delete pixelBuffer;
	delete screenCap;
}

int main(void){
	
	WSADATA wsaData;
	
	int iResult;
	int sendResult;
	char recvbuf[DEFAULT_BUFLEN] = {0};
	int recvbuflen = DEFAULT_BUFLEN;
	
	SOCKET serverSocket = INVALID_SOCKET;
	
	struct sockaddr_in servAddr;
	struct sockaddr_in clientAddr;
	socklen_t clientAddrLen = sizeof(clientAddr);
	
	// intialize winsock 
	iResult = WSAStartup(MAKEWORD(2,2), &wsaData);
	if(iResult != 0){
		printf("WSAStartup failed: %d\n", iResult);
		return 1;
	}
	
	// initialize gdiplus 
	GdiplusStartupInput gdiplusStartupInput;
    ULONG_PTR gdiplusToken;
    GdiplusStartup(&gdiplusToken, &gdiplusStartupInput, NULL);
	
	// create socket for server
	serverSocket = constructSocket();
	if(serverSocket < 0){
		printf("server socket construction failed.\n");
		return 1;
	}
	
	// set up server address 
	ZeroMemory(&servAddr, sizeof(servAddr));
	servAddr.sin_family = AF_INET;
	servAddr.sin_addr.s_addr = htonl(INADDR_ANY);
	servAddr.sin_port = htons(DEFAULT_PORT);
	
	// bind the server socket to the address 
	iResult = bind(serverSocket, (struct sockaddr *)&servAddr, sizeof(servAddr));
	socketErrorCheck(iResult, serverSocket, "bind");
	
	// accept client connections 
	printf("waiting for connections...\n");
	int frameCount = 0;
	
	while(1){
		//ZeroMemory(recvbuf, recvbuflen);
		
		// get the new data 
		// expect a client to ask for the current screenshot
		// for now, let's interpret any receive to be an 'ask' message. 
		// so basically for any client that talks to this server, we send them the current desktop screenshot
		iResult = recvfrom(serverSocket, recvbuf, recvbuflen, 0, (struct sockaddr *)&clientAddr, (int *)&clientAddrLen);
		
		// how about differentiating certain messages from client?
		// i.e. when a client first talks to the server, there should be an initiation message to receive frames
		// like some kind of hello message first. 
		// if the user wants to stop receiving messages, they send an exit message 
		// server can keep count of how many users to send messages to? use a map to keep track of clients?
		// if the count of users is 0, the server just waits
		// right now it looks like the server only sends messages to the client who most recently sent a message to it
		
		if(iResult > 0){
			printf("got client data...\n");
			printf("message: %s\n", recvbuf);
			
			if(recvbuf[0] == 'h'){
				
				printf("got a request from a client!\n");
				
				sendResult = sendto(serverSocket, recvbuf, recvbuflen, 0, (struct sockaddr *)&clientAddr, clientAddrLen);
				if(sendResult < 0){
					printf("sendto failed.\n");
					exit(1);
				}
		
				// send curr frame client
				sendCurrentScreencap(frameCount, serverSocket, (struct sockaddr *)&clientAddr, clientAddrLen);
				frameCount++;
			}
		}
		
		// when a client closes and stops talking to the server and a new client instance is started and 
		// tries to talk to the same server that's still running, it looks like it takes a while for the server to notice a new 
		// hello message to start sending over images (more than the sleep time). do I need to clear something?
		// is there something wrong on the client end?
		
		ZeroMemory(recvbuf, recvbuflen);
		Sleep(1000);
	}
	
	// shutdown gdiplus 
	GdiplusShutdown(gdiplusToken);
	
	WSACleanup();
	return 0;
}