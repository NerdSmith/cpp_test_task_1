#include <ws2tcpip.h>
#include <stdio.h>
#include <iostream>
#include <string>
#include <cstring>
#include <map>
#include <vector>
#include <fstream>
#include <fileapi.h>
using namespace std;

#pragma comment(lib, "Ws2_32.lib")


#define DEFAULT_BUFLEN 512
#define RESERVE_BLOCK_LENGTH 5
#define MSG_LEN DEFAULT_BUFLEN - RESERVE_BLOCK_LENGTH


int main(int argc, char* argv[])
{
	locale::global(locale());
	if (argc < 4) {
		printf("Not enough command line arguments, got: %d, need: 3", argc - 1);
		return 1;
	}

	WSADATA wsaData;

	char recvbuf[DEFAULT_BUFLEN];
	int iResult, iSendResult;
	int recvbuflen = DEFAULT_BUFLEN;

	SOCKET ListenSocket = INVALID_SOCKET;
	SOCKET ClientSocket = INVALID_SOCKET;
	SOCKET UDPClientSocket = INVALID_SOCKET;

	struct addrinfo* result = NULL;
	struct addrinfo TCPHints;
	struct addrinfo UDPHints;

	char* ipAddr = argv[1];
	char* port = argv[2];

	string dirName = argv[3];
	if (CreateDirectoryA(dirName.c_str(), NULL) ||
		ERROR_ALREADY_EXISTS == GetLastError())
	{
		printf("Dir is OK\n");
	}
	else
	{
		printf("Can't create dir\n");
		return 1;
	}

	char udpPort[RESERVE_BLOCK_LENGTH];
	char filenameBuf[MSG_LEN];
	string filename;

	struct sockaddr_in SenderAddr;
	int SenderAddrSize = sizeof(SenderAddr);

	fd_set rset;
	FD_ZERO(&rset);

	map<int, vector<char>> dataBlocks;
	int blockNb;
	char blockNbBuf[RESERVE_BLOCK_LENGTH];
	char fileDataBuf[MSG_LEN];

	string fileFullPath;
	int i;
	
	iResult = WSAStartup(MAKEWORD(2, 2), &wsaData);
	if (iResult != 0) {
		printf("WSAStartup failed: %d\n", iResult);
		return 1;
	}

	ZeroMemory(&TCPHints, sizeof(TCPHints));
	TCPHints.ai_family = AF_INET;
	TCPHints.ai_socktype = SOCK_STREAM;
	TCPHints.ai_protocol = IPPROTO_TCP;

	ZeroMemory(&UDPHints, sizeof(UDPHints));
	UDPHints.ai_family = AF_INET;
	UDPHints.ai_socktype = SOCK_DGRAM;
	UDPHints.ai_protocol = IPPROTO_UDP;

	iResult = getaddrinfo(ipAddr, port, &TCPHints, &result);
	if (iResult != 0) {
		printf("getaddrinfo failed: %d\n", iResult);
		WSACleanup();
		return 1;
	}

	ListenSocket = socket(result->ai_family, result->ai_socktype, result->ai_protocol);
	if (ListenSocket == INVALID_SOCKET) {
		printf("Error at socket(): %ld\n", WSAGetLastError());
		freeaddrinfo(result);
		WSACleanup();
		return 1;
	}

	iResult = bind(ListenSocket, result->ai_addr, (int)result->ai_addrlen);
	if (iResult == SOCKET_ERROR) {
		printf("bind failed with error: %d\n", WSAGetLastError());
		freeaddrinfo(result);
		closesocket(ListenSocket);
		WSACleanup();
		return 1;
	}

	freeaddrinfo(result);

	iResult = listen(ListenSocket, SOMAXCONN);
	if (iResult == SOCKET_ERROR) {
		printf("listen failed with error: %d\n", WSAGetLastError());
		closesocket(ListenSocket);
		WSACleanup();
		return 1;
	}

	while (TRUE) {
		ClientSocket = accept(ListenSocket, NULL, NULL);
		if (ClientSocket == INVALID_SOCKET) {
			printf("accept failed: %d\n", WSAGetLastError());
			closesocket(ListenSocket);
			WSACleanup();
			return 1;
		}

		do {
			iResult = recv(ClientSocket, recvbuf, recvbuflen, 0);
			if (iResult > 0) {
				printf("Bytes received: %d\n", iResult);
				iSendResult = send(ClientSocket, recvbuf, iResult, 0);
				if (iSendResult == SOCKET_ERROR) {
					printf("send failed: %d\n", WSAGetLastError());
					closesocket(ClientSocket);
					WSACleanup();
					return 1;
				}
				printf("Bytes sent: %d\n", iSendResult);
				if (iResult == iSendResult) {
					break;
				}
			}
			else if (iResult == 0)
				printf("Connection closing...\n");
			else {
				printf("recv failed: %d\n", WSAGetLastError());
				closesocket(ClientSocket);
				WSACleanup();
				return 1;
			}
		} while (iResult > 0);

		// udp_port & filename

		memcpy(udpPort, recvbuf, RESERVE_BLOCK_LENGTH);
		memcpy(filenameBuf, recvbuf + RESERVE_BLOCK_LENGTH, MSG_LEN);
		filename = filenameBuf;
		printf("UDP port: %s\n", udpPort);
		printf("Filename: %s\n", filename.c_str());
		memset(recvbuf, 0, sizeof(recvbuf));

		iResult = getaddrinfo(ipAddr, udpPort, &UDPHints, &result);
		if (iResult != 0) {
			printf("getaddrinfo failed: %d\n", iResult);
			closesocket(ClientSocket);
			WSACleanup();
			return 1;
		}

		// UDP

		UDPClientSocket = socket(result->ai_family, result->ai_socktype, result->ai_protocol);
		if (UDPClientSocket == INVALID_SOCKET) {
			printf("Socket failed with error %d\n", WSAGetLastError());
			closesocket(ClientSocket);
			WSACleanup();
			return 1;
		}


		iResult = bind(UDPClientSocket, result->ai_addr, (int)result->ai_addrlen);
		if (iResult != 0) {
			printf("Bind failed with error %d\n", WSAGetLastError());
			closesocket(ClientSocket);
			WSACleanup();
			return 1;
		}

		memset(blockNbBuf, 0, sizeof(blockNbBuf));
		memset(fileDataBuf, 0, sizeof(fileDataBuf));

		printf("Receiving datagrams...\n");

		for (;;) {
			FD_SET(ClientSocket, &rset);
			FD_SET(UDPClientSocket, &rset);

			iResult = select(0, &rset, NULL, NULL, NULL);

			if (FD_ISSET(UDPClientSocket, &rset)) {
				iResult = recvfrom(UDPClientSocket,
					recvbuf, DEFAULT_BUFLEN, 0, (SOCKADDR*)&SenderAddr, &SenderAddrSize);
				if (iResult == SOCKET_ERROR) {
					printf("Recvfrom failed with error %d\n", WSAGetLastError());
				}
				memcpy(blockNbBuf, recvbuf, RESERVE_BLOCK_LENGTH);
				blockNb = atoi(blockNbBuf);
				memcpy(fileDataBuf, recvbuf + RESERVE_BLOCK_LENGTH, DEFAULT_BUFLEN - (RESERVE_BLOCK_LENGTH + 1));
				printf("Got block %d\n", blockNb);

				vector<char> buffer(fileDataBuf, fileDataBuf + sizeof(fileDataBuf));
				dataBlocks.emplace(blockNb, move(buffer));

				iSendResult = send(ClientSocket, recvbuf, iResult, 0);
				if (iSendResult == SOCKET_ERROR) {
					printf("send failed: %d\n", WSAGetLastError());
					continue;
				}
				printf("Bytes sent: %d\n", iSendResult);
				if (iResult == iSendResult) {
					continue;
				}
			}

			if (FD_ISSET(ClientSocket, &rset)) {
				iResult = recv(ClientSocket, recvbuf, recvbuflen, 0);
				if (iResult > 0) {
					printf("Bytes received: %d\n", iResult);
				}
				else if (iResult == 0)
					printf("Connection closing...\n");
				else {
					printf("recv failed: %d\n", WSAGetLastError());
					closesocket(ClientSocket);
					WSACleanup();
					return 1;
				}
				break;
			}
		}

		closesocket(UDPClientSocket);
		printf("blocks nb: %d\n", dataBlocks.size());

		fileFullPath = dirName + "\\" + filename;

		ofstream myfile(fileFullPath);

		for (i = 0; i < dataBlocks.size(); i++) {
			printf("writing block: %d\n", i + 1);
			myfile.write((const char*)&(dataBlocks[i])[0], (dataBlocks[i]).size());
		}

		myfile.close();
		printf("Transmission done, file path: %s\n", fileFullPath.c_str());
		memset(&recvbuf[0], 0, sizeof(recvbuf));
		dataBlocks.clear();
	}
	
	closesocket(ListenSocket);

	closesocket(ClientSocket);
	WSACleanup();

	return 0;
}
