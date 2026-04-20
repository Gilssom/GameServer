#include "pch.h"
#include <iostream>

#include <WinSock2.h>
#include <MSWSock.h>
#include <WS2tcpip.h>
#pragma comment(lib, "ws2_32.lib")

void HandleError(const char* cause)
{
	int32 errCode = ::WSAGetLastError();
	cout << "Send Error Code = " << errCode << endl;
}

int main()
{
	WSAData wsaData;
	if (::WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
		return 0;

	SOCKET clientSocket = ::socket(AF_INET, SOCK_DGRAM, 0);
	if (clientSocket == INVALID_SOCKET)
	{
		HandleError("Socket");
		return 0;
	}

	SOCKADDR_IN serverAddr;
	::memset(&serverAddr, 0, sizeof(serverAddr));
	serverAddr.sin_family = AF_INET;
	::inet_pton(AF_INET, "127.0.0.1", &serverAddr.sin_addr);
	serverAddr.sin_port = ::htons(7777);

	// Connected UDP
	::connect(clientSocket, (SOCKADDR*)&serverAddr, sizeof(serverAddr));

	// -----------------------------
	while (true)
	{
		// TODO
		char sendBuffer[100] = "Hello World !";

		// IP 주소 및 Port 번호 자동 설정

		// Unconnected UDP
		//int32 resultCode = ::sendto(clientSocket, sendBuffer, sizeof(sendBuffer), 0,
		//	(SOCKADDR*)&serverAddr, sizeof(serverAddr));

		// Connected UDP
		int32 resultCode = ::send(clientSocket, sendBuffer, sizeof(sendBuffer), 0);

		if (resultCode == SOCKET_ERROR)
		{
			HandleError("send Error");
			return 0;
		}

		cout << "Send Data! Len = " << sizeof(sendBuffer) << endl;

		SOCKADDR_IN recvAddr;
		::memset(&recvAddr, 0, sizeof(recvAddr));
		int32 addrLen = sizeof(recvAddr);

		char recvBuffer[100];

		// Unconnected UDP
		//int32 recvLen = ::recvfrom(clientSocket, recvBuffer, sizeof(recvBuffer), 0,
		//	(SOCKADDR*)&recvAddr, &addrLen);

		// Connected UDP
		int32 recvLen = ::recv(clientSocket, recvBuffer, sizeof(recvBuffer), 0);

		if (recvLen <= 0)
		{
			HandleError("recv Error");
			return 0;
		}

		cout << "Recv Data ! Data = " << recvBuffer << endl;
		cout << "Recv Data ! Len = " << recvLen << endl;

		this_thread::sleep_for(1s);
	}
	// -----------------------------
	
	// Socket Resource 반환
	::closesocket(clientSocket);

	// Winsock 종료
	::WSACleanup();
}