#include <cstdio>
#include <string>
#include <iostream>
#include <stdio.h>
#include <string.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <unistd.h>

#define PORT 3433			//Ŀ���ַ�˿ں�
#define ADDR "localhost" //Ŀ���ַIP

enum func { CREATE, QUERY, DELETE };
const int MAX_STREAM_LENGTH = 128;

using namespace std;

struct Helper {
	int fd;
	int failed;
	char buf[MAX_STREAM_LENGTH];
	Helper(int fd) : fd(fd), failed(0) {}
	void send_str(const string &str) {
		int res;
		int n = 0;
		while (n < str.length()) {
			res = send(fd, str.c_str() + n, str.length() - n, 0);
			if (res == 0) {
				cout << "Connection closed when sending response." << endl;
				break;
			}
			if (res == -1 && errno != EINTR) {
				cout << "Error when sending response." << endl;
				break;
			}
			if (res > 0)
				n += res;
		}
	}
	void read() {
		int res;
		int pos = 0;
		do {
			res = recv(fd, buf + pos, MAX_STREAM_LENGTH - pos, 0);
			if (res == -1 && errno != EINTR) {
				failed = 1;
				cerr << "Error when reading from socket." << endl;
				return;
			}
			if (res > 0)
				pos += res;
		} while (res != 0 && pos != MAX_STREAM_LENGTH);
		if (pos == MAX_STREAM_LENGTH) {
			failed = 1;
			cerr << "Stream length exceeded the max length." << endl;
			return;
		}
		cout << "Received: ";
		for (int i = 0; i < pos; ++i) {
			if (buf[i])
				cout << buf[i];
			else
				cout << ' ';
		}
		cout << endl;
	}
};

void job(string& msg) {
	int iSocketFD = 0; //socket���
	unsigned int iRemoteAddr = 0;
	struct sockaddr_in stRemoteAddr = { 0 }; //�Զˣ���Ŀ���ַ��Ϣ
	socklen_t socklen = 0;
	char buf[4096] = { 0 }; //�洢���յ�������

	iSocketFD = socket(AF_INET, SOCK_STREAM, 0); //����socket
	if (0 > iSocketFD)
	{
		printf("����socketʧ�ܣ�\n");
		return;
	}

	stRemoteAddr.sin_family = AF_INET;
	stRemoteAddr.sin_port = htons(PORT);
	inet_pton(AF_INET, ADDR, &iRemoteAddr);
	stRemoteAddr.sin_addr.s_addr = iRemoteAddr;

	//���ӷ����� ��������Ŀ���ַ���ʹ�С
	if (0 > connect(iSocketFD, (sockaddr *)&stRemoteAddr, sizeof(stRemoteAddr)))
	{
		printf("����ʧ�ܣ�\n");
		//printf("connect failed:%d",errno);//ʧ��ʱҲ�ɴ�ӡerrno
	}
	else {
		Helper hpr(iSocketFD);
		msg.insert(msg.begin(), (char)(msg.length() + 1));
		hpr.send_str(msg);
		hpr.read();
		cout << "Fail: " << hpr.failed << endl;
	}

	close(iSocketFD);//�ر�socket	

}

int main()
{
	char cmd;
	while (true) {
		cin >> cmd;
		switch (cmd) {
		case 'c': {
			cout << "User id >> ";
			string user_id;
			cin >> user_id;
			cout << "Role >> ";
			string role;
			cin >> role;
			string msg(1, char(0));
			msg += string(user_id.c_str(), user_id.length() + 1);
			msg += string(role.c_str(), role.length() + 1);
			job(msg);
			break;
		}
		case 'q': {
			cout << "Session id >> ";
			string ss;
			cin >> ss;
			string msg(1, char(1));
			msg += string(ss.c_str(), ss.length() + 1);
			job(msg);
			break;
		}
		case 'd': {
			cout << "Session id >> ";
			string ss;
			cin >> ss;
			string msg(1, char(2));
			msg += string(ss.c_str(), ss.length() + 1);
			job(msg);
			break;
		}
		default:
			cout << "Unknown command '" << cmd << "'." << endl;
		}
	}
	return 0;
}