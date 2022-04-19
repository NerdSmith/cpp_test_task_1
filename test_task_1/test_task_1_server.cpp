#include <ws2tcpip.h>
#include <stdio.h>
#include <iostream>
#include <string>
#include <cstring>
#include <map>
#include <vector>
#include <fstream>
#include <fileapi.h>
#include <windows.h>
#include <list>
using namespace std;

#pragma comment(lib, "Ws2_32.lib")

#define DEFAULT_BUFLEN 512
#define RESERVE_BLOCK_LENGTH 5
#define MSG_LEN DEFAULT_BUFLEN - RESERVE_BLOCK_LENGTH

static DWORD threadId;
static HANDLE hServTread;
static struct ServInfo* servInfo;


struct ServInfo {
	char* ipAddr;
	char* port;
	char* dirname;
};

struct Client {
	SOCKET TCPSock = INVALID_SOCKET;
	SOCKET UDPSock = INVALID_SOCKET;
	string filename;
	string UDPPort;
	map<int, vector<char>> dataBlocks;
};

BOOL WINAPI CtrlHandler(DWORD fdwCtrlType)
{
	printf("Server stop\n");
	if (fdwCtrlType == CTRL_C_EVENT) {
		TerminateThread(hServTread, 0);
		return TRUE;
	}
	return FALSE;
}

string toStr(char* cStr) {
	string s(cStr);
	return s;
}

int createDirIfNotExist(string dirName) {
	
	if (CreateDirectoryA(dirName.c_str(), NULL) ||
		ERROR_ALREADY_EXISTS == GetLastError()) {
		printf("Dir is OK\n");
		return 0;
	}
	
	printf("Can't create dir\n");
	return 1;
}

void setupHints(addrinfo& hints, int ai_family, int ai_socktype, int ai_protocol) {
	ZeroMemory(&hints, sizeof(hints));
	hints.ai_family = ai_family;
	hints.ai_socktype = ai_socktype;
	hints.ai_protocol = ai_protocol;
}

SOCKET createAndBindSocket(char* ipAddr, char* port, addrinfo& hints) {
	int iRes;
	SOCKET sock;
	addrinfo* addr = NULL;

	iRes = getaddrinfo(ipAddr, port, &hints, &addr);
	if (iRes != 0) {
		printf("getaddrinfo failed: %d\n", iRes);
		WSACleanup();
		return INVALID_SOCKET;
	}

	sock = socket(addr->ai_family, addr->ai_socktype, addr->ai_protocol);
	if (sock == INVALID_SOCKET) {
		printf("Error at socket(): %ld\n", WSAGetLastError());
		freeaddrinfo(addr);
		WSACleanup();
		return INVALID_SOCKET;
	}

	iRes = bind(sock, addr->ai_addr, (int)addr->ai_addrlen);
	if (iRes == SOCKET_ERROR) {
		printf("bind failed with error: %d\n", WSAGetLastError());
		freeaddrinfo(addr);
		closesocket(sock);
		WSACleanup();
		return INVALID_SOCKET;
	}

	freeaddrinfo(addr);

	return sock;
}


DWORD WINAPI servFunc(LPVOID lpParam)
{
	map<SOCKET, Client> clients;
	map<SOCKET, SOCKET> UDP_TCP_map;

	WSADATA wsaData;

	char recvbuf[DEFAULT_BUFLEN];
	int iResult, iSendResult;
	int recvbuflen = DEFAULT_BUFLEN;

	SOCKET ListenSocket = INVALID_SOCKET;
	SOCKET TCPClientSocket = INVALID_SOCKET;
	SOCKET UDPClientSocket = INVALID_SOCKET;

	struct addrinfo* servAddr = NULL;
	struct addrinfo TCPHints;
	struct addrinfo UDPHints;

	ServInfo* servInfo = static_cast<struct ServInfo*>(lpParam);

	char* ipAddr = servInfo->ipAddr;
	char* port = servInfo->port;

	string dirName = servInfo->dirname;


	if (createDirIfNotExist(toStr(servInfo->dirname))) {
		return 1;
	};

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
	char strBuf[DEFAULT_BUFLEN];

	iResult = WSAStartup(MAKEWORD(2, 2), &wsaData);
	if (iResult != 0) {
		printf("WSAStartup failed: %d\n", iResult);
		return 1;
	}

	setupHints(TCPHints, AF_INET, SOCK_STREAM, IPPROTO_TCP);
	setupHints(UDPHints, AF_INET, SOCK_DGRAM, IPPROTO_UDP);

	createAndBindSocket(servInfo->ipAddr, servInfo->port, TCPHints);

	freeaddrinfo(servAddr);

	fd_set sockets_fds;
	FD_ZERO(&sockets_fds);
	list<SOCKET> TCPSockets;
	list<SOCKET> UDPSockets;
	

	iResult = listen(ListenSocket, SOMAXCONN);
	if (iResult == SOCKET_ERROR) {
		printf("listen failed with error: %d\n", WSAGetLastError());
		closesocket(ListenSocket);
		WSACleanup();
		return 1;
	}

	for (;;) {
		
		FD_SET(ListenSocket, &sockets_fds);
		/// add sockets to fd_set
		
		select(0, &sockets_fds, NULL, NULL, NULL);

		if (FD_ISSET(ListenSocket, &sockets_fds)) {
			TCPClientSocket = accept(ListenSocket, NULL, NULL);
			if (TCPClientSocket == INVALID_SOCKET) {
				printf("accept failed: %d\n", WSAGetLastError());
				closesocket(ListenSocket);
				WSACleanup();
				return 1;
			}
			struct Client client;
			client.TCPSock = TCPClientSocket;

			TCPSockets.push_back(TCPClientSocket);
			clients.emplace(TCPClientSocket, client);
		}

		for (const SOCKET& sock : TCPSockets) {
			// if not 0 recv -> fill struct, create new sock
			// otherwise -> close conn, del from sockets & clients
			if (FD_ISSET(sock, &sockets_fds)) {
				iResult = recv(sock, recvbuf, recvbuflen, 0);
				if (iResult > 0) {
					printf("Bytes received: %d\n", iResult);
					iSendResult = send(sock, recvbuf, iResult, 0);
					if (iSendResult == SOCKET_ERROR) {
						printf("send failed: %d\n", WSAGetLastError());
						closesocket(sock);
						closesocket(ListenSocket);
						WSACleanup();
						return 1;
					}
					printf("Bytes sent: %d\n", iSendResult);

					memcpy(udpPort, recvbuf, RESERVE_BLOCK_LENGTH);
					memcpy(filenameBuf, recvbuf + RESERVE_BLOCK_LENGTH, MSG_LEN);

					clients[sock].UDPPort = udpPort;
					clients[sock].filename = filenameBuf;

					printf("UDP port: %s\n", clients[sock].UDPPort);
					printf("Filename: %s\n", clients[sock].filename);

					ZeroMemory(&recvbuf, sizeof(recvbuf));
				}
				else if (iResult == 0) {
					TCPSockets.remove(clients[sock].TCPSock);
					UDPSockets.remove(clients[sock].UDPSock);

					clients.erase(clients[sock].TCPSock);
					clients.erase(clients[sock].UDPSock);

					closesocket(clients[sock].TCPSock);
					closesocket(clients[sock].UDPSock);

					printf("Connection closing...\n");
				}
				else {
					printf("recv failed: %d\n", WSAGetLastError());
					closesocket(TCPClientSocket);
					closesocket(ListenSocket);
					WSACleanup();
					return 1;
				}
			}
		}

		for (const SOCKET& sock : UDPSockets) {
			// get client & fill map with dataBlocks
		}

	}





	for (;;) {
		TCPClientSocket = accept(ListenSocket, NULL, NULL);
		if (TCPClientSocket == INVALID_SOCKET) {
			printf("accept failed: %d\n", WSAGetLastError());
			closesocket(ListenSocket);
			WSACleanup();
			return 1;
		}

		do {
			iResult = recv(TCPClientSocket, recvbuf, recvbuflen, 0);
			if (iResult > 0) {
				printf("Bytes received: %d\n", iResult);
				iSendResult = send(TCPClientSocket, recvbuf, iResult, 0);
				if (iSendResult == SOCKET_ERROR) {
					printf("send failed: %d\n", WSAGetLastError());
					closesocket(TCPClientSocket);
					closesocket(ListenSocket);
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
				closesocket(TCPClientSocket);
				closesocket(ListenSocket);
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

		iResult = getaddrinfo(ipAddr, udpPort, &UDPHints, &servAddr);
		if (iResult != 0) {
			printf("getaddrinfo failed: %d\n", iResult);
			closesocket(TCPClientSocket);
			closesocket(ListenSocket);
			WSACleanup();
			return 1;
		}

		// UDP

		UDPClientSocket = socket(servAddr->ai_family, servAddr->ai_socktype, servAddr->ai_protocol);
		if (UDPClientSocket == INVALID_SOCKET) {
			printf("Socket failed with error %d\n", WSAGetLastError());
			closesocket(TCPClientSocket);
			closesocket(ListenSocket);
			WSACleanup();
			return 1;
		}


		iResult = bind(UDPClientSocket, servAddr->ai_addr, (int)servAddr->ai_addrlen);
		if (iResult != 0) {
			printf("Bind failed with error %d\n", WSAGetLastError());
			closesocket(TCPClientSocket);
			closesocket(ListenSocket);
			WSACleanup();
			return 1;
		}

		memset(blockNbBuf, 0, sizeof(blockNbBuf));
		memset(fileDataBuf, 0, sizeof(fileDataBuf));

		printf("Receiving datagrams...\n");

		for (;;) {
			FD_SET(TCPClientSocket, &rset);
			FD_SET(UDPClientSocket, &rset);

			iResult = select(0, &rset, NULL, NULL, NULL);

			if (FD_ISSET(UDPClientSocket, &rset)) {
				iResult = recvfrom(UDPClientSocket,
					recvbuf, DEFAULT_BUFLEN, 0, (SOCKADDR*)&SenderAddr, &SenderAddrSize);
				if (iResult == SOCKET_ERROR) {
					printf("Recvfrom failed with error %d\n", WSAGetLastError());
					continue;
				}
				memcpy(blockNbBuf, recvbuf, RESERVE_BLOCK_LENGTH);
				blockNb = atoi(blockNbBuf);
				memcpy(fileDataBuf, recvbuf + RESERVE_BLOCK_LENGTH, DEFAULT_BUFLEN - (RESERVE_BLOCK_LENGTH + 1));
				printf("Got block %d\n", blockNb);

				vector<char> buffer(fileDataBuf, fileDataBuf + sizeof(fileDataBuf));
				dataBlocks.emplace(blockNb, buffer);

				iSendResult = send(TCPClientSocket, recvbuf, iResult, 0);
				if (iSendResult == SOCKET_ERROR) {
					printf("send failed: %d\n", WSAGetLastError());
					continue;
				}
				printf("Bytes sent: %d\n", iSendResult);
				if (iResult == iSendResult) {
					continue;
				}
			}

			if (FD_ISSET(TCPClientSocket, &rset)) {
				iResult = recv(TCPClientSocket, recvbuf, recvbuflen, 0);
				if (iResult > 0) {
					printf("Bytes received: %d\n", iResult);
				}
				else if (iResult == 0)
					printf("Connection closing...\n");
				else {
					printf("recv failed: %d\n", WSAGetLastError());
					closesocket(TCPClientSocket);
					closesocket(ListenSocket);
					closesocket(UDPClientSocket);
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

		if (!myfile) {
			printf("Unable to open file");
			closesocket(TCPClientSocket);
			closesocket(ListenSocket);
			WSACleanup();
			return 1;
		}

		for (i = 0; i < dataBlocks.size(); i++) {
			printf("writing block: %d\n", i + 1);
			memset(&strBuf[0], 0, sizeof(strBuf));
			copy(dataBlocks[i].begin(), dataBlocks[i].end(), strBuf);

			myfile.write(&strBuf[0], strlen(strBuf));
		}

		myfile.close();
		printf("Transfer completed, file path: %s\n", fileFullPath.c_str());
		memset(&recvbuf[0], 0, sizeof(recvbuf));
		dataBlocks.clear();
	}

	closesocket(ListenSocket);

	closesocket(TCPClientSocket);
	WSACleanup();

	return 0;
}



int main(int argc, char* argv[])
{
	locale::global(locale());
	if (argc < 4) {
		printf("Not enough command line arguments, got: %d, need: 3", argc - 1);
		return 1;
	}

	servInfo = new ServInfo();
	servInfo->ipAddr = argv[1];
	servInfo->port = argv[2];
	servInfo->dirname = argv[3];

	hServTread = CreateThread(
		NULL,
		0,
		servFunc,
		servInfo,
		0,
		&threadId
	);

	if (hServTread == NULL) {
		printf("Can't create server thread\n");
		return 1;
	}

	SetConsoleCtrlHandler(CtrlHandler, TRUE);
	
	WaitForSingleObject(hServTread, INFINITE);
	return 0;
}