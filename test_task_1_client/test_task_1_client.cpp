#include <winsock2.h>
#include <ws2tcpip.h>
#include <stdio.h>
#include <cstdio>
#include <fstream>
#include <set>
#include <iostream>

#pragma comment(lib, "Ws2_32.lib")

#define DEFAULT_BUFLEN 512
#define RESERVE_BLOCK_LENGTH sizeof(int16_t)
#define MSG_LEN DEFAULT_BUFLEN - RESERVE_BLOCK_LENGTH

int main(int argc, char* argv[]) 
{
	if (argc < 4) {
		printf("Not enough command line arguments, got: %d, need: 5", argc - 1);
		return 1;
	}

	WSADATA wsaData;
	int iResult;
	int sendResult;
	int receiveResult;
	SOCKET ConnectSocket = INVALID_SOCKET;
	SOCKET UDPSocket = INVALID_SOCKET;

	char* ipAddr = argv[1];
	char* port = argv[2];
	char* udpPort = argv[3];
	char* filename = argv[4];
	timeval timeout = timeval();
	timeout.tv_sec = 0;
	timeout.tv_usec = atoi(argv[5]);

	int recvbuflen = DEFAULT_BUFLEN;
	char recvbuf[DEFAULT_BUFLEN];

	struct addrinfo* result = NULL,
					* ptr = NULL,
					TCPHints,
					UDPHints;

	char sendbuf[DEFAULT_BUFLEN];
	ZeroMemory(&sendbuf, sizeof(sendbuf));

	int16_t* reserve_block_i16 = NULL;
	std::set<int> pNumbers;

	int blockNb = 0;
	int recvBlockNb;

	int selectRes;

	fd_set rset;
	FD_ZERO(&rset);

	// setup
	iResult = WSAStartup(MAKEWORD(2, 2), &wsaData);
	if (iResult != 0) {
		printf("WSAStartup failed: %d\n", iResult);
		return 1;
	}

	ZeroMemory(&TCPHints, sizeof(TCPHints));
	TCPHints.ai_family = AF_INET;
	TCPHints.ai_socktype = SOCK_STREAM;
	TCPHints.ai_protocol = IPPROTO_TCP;


	iResult = getaddrinfo(ipAddr, port, &TCPHints, &result);
	if (iResult != 0) {
		printf("getaddrinfo failed: %d\n", iResult);
		WSACleanup();
		return 1;
	}

	for (ptr = result; ptr != NULL; ptr = ptr->ai_next) {

		ConnectSocket = socket(ptr->ai_family, ptr->ai_socktype,
			ptr->ai_protocol);
		if (ConnectSocket == INVALID_SOCKET) {
			printf("socket failed with error: %ld\n", WSAGetLastError());
			WSACleanup();
			return 1;
		}

		iResult = connect(ConnectSocket, ptr->ai_addr, (int)ptr->ai_addrlen);
		if (iResult == SOCKET_ERROR) {
			closesocket(ConnectSocket);
			ConnectSocket = INVALID_SOCKET;
			continue;
		}
		break;
	}

	freeaddrinfo(result);

	if (ConnectSocket == INVALID_SOCKET) {
		printf("Unable to connect to server!\n");
		WSACleanup();
		return 1;
	}

	// create port_filename sendbuf
	ZeroMemory(&sendbuf, sizeof(sendbuf));

	reserve_block_i16 = (int16_t*)(sendbuf);
	*reserve_block_i16 = (int16_t)(atoi(udpPort));

	memcpy(sendbuf + RESERVE_BLOCK_LENGTH, filename, strlen(filename));

	//send port & filename
	do {
		sendResult = send(ConnectSocket, sendbuf, (int)sizeof(sendbuf), 0);
		if (sendResult == SOCKET_ERROR) {
			printf("send failed: %d\n", WSAGetLastError());
			closesocket(ConnectSocket);
			WSACleanup();
			return 1;
		}
		printf("Bytes Sent: %ld\n", sendResult);

		receiveResult = recv(ConnectSocket, recvbuf, recvbuflen, 0);

		if (receiveResult > 0)
			printf("Bytes received: %d\n", receiveResult);
		else if (receiveResult == 0)
			printf("Connection closed\n");
		else
			printf("recv failed: %d\n", WSAGetLastError());

	} while ((sendResult != receiveResult) || (receiveResult == 0));

	// create upd socket
	ZeroMemory(&UDPHints, sizeof(UDPHints));
	UDPHints.ai_family = AF_INET;
	UDPHints.ai_socktype = SOCK_DGRAM;
	UDPHints.ai_protocol = IPPROTO_UDP;

	iResult = getaddrinfo(ipAddr, udpPort, &UDPHints, &result);
	if (iResult != 0) {
		printf("getaddrinfo failed: %d\n", iResult);
		closesocket(ConnectSocket);
		WSACleanup();
		return 1;
	}

	UDPSocket = socket(result->ai_family, result->ai_socktype, result->ai_protocol);
	if (UDPSocket == INVALID_SOCKET) {
		printf("Socket failed with error %d\n", WSAGetLastError());
		closesocket(ConnectSocket);
		return 1;
	}

	// send file data

	std::ifstream myfile (filename);
	if (!myfile) {
		printf("Unable to open file");
		closesocket(ConnectSocket);
		closesocket(UDPSocket);
		WSACleanup();
		return 1;
	}

	ZeroMemory(&sendbuf, sizeof(sendbuf));

	reserve_block_i16 = (int16_t*)(sendbuf);

	for (;;) {

		*reserve_block_i16 = static_cast<int16_t>(blockNb);
		
		printf("block nb %d\n", blockNb);

		myfile.read(&sendbuf[RESERVE_BLOCK_LENGTH], MSG_LEN - 1);

		for (;;) {
			FD_ZERO(&rset);
			FD_SET(ConnectSocket, &rset);

			iResult = sendto(UDPSocket, sendbuf, sizeof(sendbuf),
				0, result->ai_addr, result->ai_addrlen);
			if (iResult == SOCKET_ERROR) {
				printf("send failed: %d\n", WSAGetLastError());
				closesocket(ConnectSocket);
				closesocket(UDPSocket);
				WSACleanup();
				return 1;
			}
			pNumbers.insert(blockNb);
			printf("Bytes Sent: %ld\n", iResult);
		
			selectRes = select(0, &rset, NULL, NULL, &timeout);
			if (selectRes == SOCKET_ERROR) {
				printf("select error: %d\n", WSAGetLastError());
				closesocket(ConnectSocket);
				closesocket(UDPSocket);
				WSACleanup();
			}
			else if (selectRes == 0) {
				printf("timeout\n");
				continue;
			}
			else {
				receiveResult = recv(ConnectSocket, recvbuf, recvbuflen, 0);

				if (receiveResult > 0) {

					recvBlockNb = *((int16_t*)(recvbuf));
					printf("curr pnb %d | got %d\n", blockNb, recvBlockNb);

					if (pNumbers.find(recvBlockNb) != pNumbers.end()) {
						pNumbers.erase(recvBlockNb);
						printf("Bytes received: %d\n", receiveResult);
					}
					else {
						continue;
					}
				}
				else if (receiveResult == 0)
					printf("Connection closed\n");
				else
					printf("Recv failed: %d\n", WSAGetLastError());
				break;
			}
		}
		if (myfile.eof() && pNumbers.empty()) {
			sendResult = send(ConnectSocket, NULL, 0, 0);
			if (sendResult == SOCKET_ERROR) {
				printf("send failed: %d\n", WSAGetLastError());
				closesocket(UDPSocket);
				closesocket(ConnectSocket);
				WSACleanup();
				return 1;
			}
			printf("Bytes Sent: %ld\n", sendResult);
			break;

		}
		else {
			blockNb++;
			ZeroMemory(&sendbuf, sizeof(sendbuf));
		}
	}

	myfile.close();

	// end of transfer

	iResult = shutdown(ConnectSocket, SD_SEND);
	if (iResult == SOCKET_ERROR) {
		printf("shutdown failed: %d\n", WSAGetLastError());
		closesocket(UDPSocket);
		closesocket(ConnectSocket);
		WSACleanup();
		return 1;
	}
	closesocket(UDPSocket);
	closesocket(ConnectSocket);
	WSACleanup();

	return 0;
}