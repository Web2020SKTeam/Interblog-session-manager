#include <cstdio>
#include <map>
#include <set>
#include <chrono>
#include <thread>
#include <mutex>
#include <string>
#include <iostream>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <unistd.h>

#define PORT 3433
#define BACKLOG 1000
const int SESSION_ID_LENGTH = 16;
const int EXPIRE_MILISECONDS = 5 * 60 * 1000;
const int AUTO_EXPIRE_INTERVAL = 10;
const int MAX_STREAM_LENGTH = 128;
enum func { CREATE, QUERY, DELETE };

using namespace std;
using namespace std::chrono;
typedef time_point<steady_clock> stamp;

struct record {
	string session_id;
	string user_id;
	string role;
};

map<string, record *> session_map;
map<string, record *> user_map;
map<record *, stamp> record_time;
set<pair<stamp, record *> > time_record;
mutex mtx;

string get_session_id(string user, string role) {
	record *rec = nullptr;
	mtx.lock();
	auto item = user_map.find(user);
	if (item != user_map.end()) {
		rec = item->second;
		session_map.erase(rec->session_id);
		user_map.erase(item);
		auto record_time_item = record_time.find(rec);
		time_record.erase(make_pair(record_time_item->second, rec));
		record_time.erase(record_time_item);
	}
	if (!rec)
		rec = new record;
	string ss;
	ss.reserve(SESSION_ID_LENGTH);
	do {
		ss.clear();
		for (int i = 0; i < SESSION_ID_LENGTH; ++i) {
			char ch = (char)(rand() % 36);
			ch = char(ch < 10 ? ch + '0' : ch - 10 + 'a');
			ss.push_back(ch);
		}
	} while (session_map.find(ss) != session_map.end());
	rec->session_id = ss;
	rec->user_id = user;
	rec->role = role;
	session_map[ss] = rec;
	user_map[user] = rec;
	stamp expire = steady_clock::now() + milliseconds(EXPIRE_MILISECONDS);
	record_time[rec] = expire;
	time_record.insert(make_pair(expire, rec));
	mtx.unlock();
	return ss;
}

void expire() {
	auto now = steady_clock::now();
	set<pair<stamp, record *> >::iterator item;
	while (!time_record.empty() && (item = time_record.begin())->first < now) {
		record *rec = item->second;
		session_map.erase(rec->session_id);
		user_map.erase(rec->user_id);
		record_time.erase(rec);
		time_record.erase(item);
		delete rec;
	}
}

void auto_expire() {
	while (true) {
		this_thread::sleep_for(duration<double>(AUTO_EXPIRE_INTERVAL));
		mtx.lock();
		expire();
		mtx.unlock();
	}
}

void update_expire_time(record *rec) {
	auto record_time_item = record_time.find(rec);
	time_record.erase(make_pair(record_time_item->second, rec));
	stamp expire = steady_clock::now() + milliseconds(EXPIRE_MILISECONDS);
	record_time_item->second = expire;
	time_record.insert(make_pair(expire, rec));
}

pair<string, string> get_info(string session_id) {
	mtx.lock();
	auto item = session_map.find(session_id);
	if (item == session_map.end()) {
		mtx.unlock();
		return make_pair(string(), string());
	}
	record *rec = item->second;
	if (steady_clock::now() > record_time[rec]) {
		expire();
		mtx.unlock();
		return make_pair(string(), string());
	}
	auto result = make_pair(rec->user_id, rec->role);
	update_expire_time(rec);
	mtx.unlock();
	return result;
}

void end_session(string session_id) {
	mtx.lock();
	auto item = session_map.find(session_id);
	if (item == session_map.end()) {
		mtx.unlock();
		return;
	}
	record *rec = item->second;
	session_map.erase(item);
	user_map.erase(rec->user_id);
	auto record_time_item = record_time.find(rec);
	time_record.erase(make_pair(record_time_item->second, rec));
	record_time.erase(record_time_item);
	mtx.unlock();
	delete rec;
}

int init_socket() {
	cout << "Initializing socket..." << endl;
	int iSocketFD = 0;
	struct sockaddr_in stLocalAddr = { 0 };

	iSocketFD = socket(AF_INET, SOCK_STREAM, 0);
	if (iSocketFD < 0) {
		cout << "Failed to create the socket." << endl;
		exit(1);
	}
	stLocalAddr.sin_family = AF_INET;
	stLocalAddr.sin_port = htons(PORT);
	stLocalAddr.sin_addr.s_addr = htonl(INADDR_ANY);
	if (bind(iSocketFD, (const sockaddr*)&stLocalAddr, sizeof(stLocalAddr)) < 0) {
		cout << "Failed to bind the address to the socket. ERR " << errno << endl;
		exit(2);
	}
	if (listen(iSocketFD, BACKLOG) < 0) {
		cout << "Failed to listen." << endl;
		exit(3);
	}
	return iSocketFD;
}

struct Helper {
	int fd;
	char buf[MAX_STREAM_LENGTH];
	int length;
	int pos;
	int failed;
	Helper(int fd) : fd(fd), length(MAX_STREAM_LENGTH), pos(0), failed(0) {
		int res;
		do {
			res = recv(fd, buf + pos, MAX_STREAM_LENGTH - pos, 0);
			if (res == -1 && errno != EINTR) {
				failed = 1;
				cout << "Error when reading from socket." << endl;
				return;
			}
			if (res > 0) {
				if (!pos)
					length = (unsigned char)buf[0];
				pos += res;
			}
		} while (res != 0 && pos < length);
		if (pos > length) {
			cout << "Warning: Received more data than expected." << endl;
		}
		else if (pos < length) {
			cout << "Warning: Received less data than expected." << endl;
		}
		length = pos;
		pos = 1;
	}
	func recv_func() {
		if (pos >= length) {
			cout << "The stream ends when trying to read the function code." << endl;
			failed = 1;
			return CREATE;
		}
		char res = buf[pos++];
		if (res < CREATE || res > DELETE) {
			cout << "Invalid function code: " << (int)res << "." << endl;
			failed = 1;
			return CREATE;
		}
		return (func)res;
	}
	string recv_str() {
		int begin = pos;
		while (pos < length && buf[pos])
			++pos;
		if (pos < length)
			++pos;
		return string(buf + begin, pos - begin - 1);
	}
	void send_str(const string &str) {
		int res;
		int n = 0;
		int length = (int)(str.length() + 1);
		while (n < length) {
			res = send(fd, str.c_str() + n, length - n, 0);
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
};

void serve(int fd) {
	cout << "Started serving." << endl;
	struct sockaddr_in stRemoteAddr = { 0 };
	socklen_t socklen = 0;
	while (true) {
		int new_fd = accept(fd, (sockaddr *)&stRemoteAddr, &socklen);
		if (new_fd < 0) {
			cout << "Failed to accept a connection." << endl;
			continue;
		}
		Helper hpr(new_fd);
		if (hpr.failed) {
			close(new_fd);
			continue;
		}
		func fn = hpr.recv_func();
		if (hpr.failed) {
			close(new_fd);
			continue;
		}
		switch (fn) {
		case CREATE: {
			string user_id = hpr.recv_str();
			string role = hpr.recv_str();
			hpr.send_str(get_session_id(user_id, role));
			close(new_fd);
			break;
		}
		case QUERY: {
			auto result = get_info(hpr.recv_str());
			hpr.send_str(result.first);
			hpr.send_str(result.second);
			close(new_fd);
			break;
		}
		case DELETE: {
			close(new_fd);
			end_session(hpr.recv_str());
		}
		}

		/*auto prec = [](record *rec) {cout << "(" << rec->session_id << "," << rec->user_id << "," << rec->role << ")"; };
		cout << "state: " << endl;
		cout << "session map: " << endl;
		for (auto it : session_map) {
			cout << it.first << '\t';
			prec(it.second);
			cout << endl;
		}
		cout << endl << "user map: " << endl;
		for (auto it : user_map) {
			cout << it.first << '\t';
			prec(it.second);
			cout << endl;
		}
		cout << endl << "record time: " << endl;
		for (auto it : record_time) {
			prec(it.first);
			cout << '\t' << (it.second - steady_clock::now()).count() / 1e9 << endl;
		}
		cout << endl << "time record: " << endl;
		for (auto it : time_record) {
			cout << (it.first - steady_clock::now()).count() / 1e9 << '\t';
			prec(it.second);
			cout << endl;
		}
		cout << endl;*/
	}
}

int main()
{
	srand((unsigned)time(nullptr));
	int listen_fd = init_socket();
	thread thrd(auto_expire);
	serve(listen_fd);
    return 0;
}