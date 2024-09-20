#include <iostream>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <string.h>
#include <string>
#include <arpa/inet.h>
#include <atomic>
#include <ctime>
#include <chrono>
#include <semaphore.h>
#include <unordered_map>
#include <thread>

#include "json.hpp"
#include "public.hpp"
#include "user.hpp"
#include "group.hpp"

using namespace std;
using json = nlohmann::json;
bool isMainMenuRunning = false;
sem_t rwsem;
atomic_bool g_isLoginSuccess(false);

//记录当前系统登录的用户信息
User g_currentUser;
vector<User> g_currentUserFriendList;

vector<Group> g_currentUserGroupList;


// 接收线程
void readTaskHandler(int clientfd);
// 获取系统时间（聊天信息需要添加时间信息）
string getCurrentTime();
// 主聊天页面程序
void mainMenu(int);
// 显示当前登录成功用户的基本信息
void showCurrentUserData();
//聊天客户端程序实现  main线程用作发送线程  子线程用作接收线程
int main(int argc, char** argv) {
    if (argc < 3) {
        cerr << "command invalid! example:./ChatClient 127.0.0.1 6000" << endl;
        exit(-1);
    }
    //解析通过命令行参数传递的ip和port
    char* ip = argv[1];
    u_int16_t port = atoi(argv[2]);

    //创建client端的socket
    int clientfd = socket(AF_INET, SOCK_STREAM, 0);
    if (-1 == clientfd) {
        cerr << "socket create error" << endl;
        exit(-1);
    }

    //填写client需要连接的server信息ip+port
    sockaddr_in server;
    memset(&server, 0, sizeof(sockaddr_in));

    server.sin_family = AF_INET;
    server.sin_port = htons(port);
    server.sin_addr.s_addr = inet_addr(ip);

    //client和server进行连接
    if (-1 == connect(clientfd, (sockaddr*)&server, sizeof(sockaddr_in))) {
        cerr << "connect server error" << endl;
        close(clientfd);
        exit(-1);
    }
    sem_init(&rwsem, 0, 0);

    std::thread readTask(readTaskHandler, clientfd);
    readTask.detach();

    //main 函数用户接收用户的输入  负责发送数据
    for (;;) {
        //显示首页菜单 登录、注册、退出
        cout << "============================" << endl;
        cout << "1、login" << endl;
        cout << "2、register" << endl;
        cout << "3、quit" << endl;
        cout << "==============================" << endl;
        cout << "choice:";
        int choice;
        cin >> choice;
        cin.get();//读掉缓冲区残留的回车

        switch (choice) {
        case 1: {//login 业务
            int id = 0;
            char pwd[50] = { 0 };
            cout << "userid:";
            cin >> id;
            cin.get();
            cout << "userpassword:";
            cin.getline(pwd, 50);

            json js;
            js["msgid"] = LOGIN_MSG;
            js["id"] = id;
            js["password"] = pwd;
            string request = js.dump();

            g_isLoginSuccess = false;

            int len = send(clientfd, request.c_str(), strlen(request.c_str()) + 1, 0);
            if (len == -1) {
                cerr << "send login msg error:" << request << endl;
            }
            sem_wait(&rwsem);
            if (g_isLoginSuccess) {
                isMainMenuRunning = true;
                mainMenu(clientfd);
            }

        }
              break;
        case 2: {//register业务
            char name[50] = { 0 };
            char pwd[50] = { 0 };
            cout << "username:";
            cin.getline(name, 50);
            cout << "userpassword:";

            cin.getline(pwd, 50);

            json js;
            js["msgid"] = REG_MSG;
            js["name"] = name;
            js["password"] = pwd;
            string request = js.dump();

            int len = send(clientfd, request.c_str(), strlen(request.c_str()) + 1, 0);
            if (len == -1) {
                cerr << "send reg msg error" << request << endl;
            }
            sem_wait(&rwsem);

        }
              break;
        case 3: {//quit 业务
            close(clientfd);
            sem_destroy(&rwsem);
            exit(0);

        }default:
            cerr << "invalid input!" << endl;
            break;
        }

    }
    return 0;
}


void doRegResponse(json& responsejs) {
    if (0 != responsejs["errno"].get<int>())//注册失败
    {
        cerr << "name is already exist,register error!" << endl;
    }
    else {
        cout << "name register success,userid is" << responsejs["id"] << ", do not forget it!" << endl;
    }
}

void doLoginResponse(json& responsejs) {
    if (0 != responsejs["errno"].get<int>()) {
        cerr << responsejs["errmsg"] << endl;
        g_isLoginSuccess = false;
    }
    else {
        g_currentUser.setId(responsejs["id"].get<int>());
        g_currentUser.setName(responsejs["name"]);

        if (responsejs.contains("friends")) {
            g_currentUserFriendList.clear();
            vector<string> vec = responsejs["friends"];
            for (string& str : vec) {
                json js = json::parse(str);
                User user;
                user.setId(js["id"].get<int>());
                user.setName(js["name"]);
                user.setState(js["state"]);
                g_currentUserFriendList.push_back(user);
            }
        }
        if (responsejs.contains("group")) {
            g_currentUserGroupList.clear();

            vector<string> vec1 = responsejs["groups"];
            for (string& groupstr : vec1) {
                json grpjs = json::parse(groupstr);
                Group group;
                group.setId(grpjs["id"].get<int>());
                group.setName(grpjs["groupname"]);
                group.setDesc(grpjs["groupdesc"]);

                vector<string> vec2 = grpjs["users"];
                for (string& userstr : vec2) {
                    GroupUser user;
                    json js = json::parse(userstr);
                    user.setId(js["id"].get<int>());
                    user.setName(js["name"]);
                    user.setState(js["state"]);
                    user.setRole(js["role"]);
                    group.getUsers().push_back(user);
                }
                g_currentUserGroupList.push_back(group);
            }
        }
        showCurrentUserData();

        if (responsejs.contains("offlinemsg")) {
            vector<string> vec = responsejs["offlinemsg"];
            for (string& str : vec) {
                json js = json::parse(str);
                if (ONE_CHAT_MSG == js["msgid"].get<int>()) {
                    cout << js["time"].get<string>() << "[" << js["id"] << "]" << js["name"].get<string>() << "said:" << js["msg"].get<string>() << endl;
                }
                else {
                    cout << "群消息[" << js["groupid"] << "]:" << js["time"].get<string>() << " [" << js["id"] << "]" << js["name"].get<string>()
                        << " said: " << js["msg"].get<string>() << endl;
                }
            }
        }
        g_isLoginSuccess = true;
    }
}

void readTaskHandler(int clientfd) {
    for (;;) {
        char buffer[1024] = { 0 };
        int len = recv(clientfd, buffer, 1024, 0);
        if (-1 == len || 0 == len) {
            close(clientfd);
            exit(-1);
        }

        //接收ChatServer转发的数据   反序列话生成json数据对象
        json js = json::parse(buffer);
        int msgtype = js["msgid"].get<int>();
        if (msgtype == ONE_CHAT_MSG) {
            cout << js["time"].get<string>() << "[" << js["id"] << "]" << js["name"].get<string>() << "said:" << js["msg"].get<string>() << endl;
            continue;
        }
        if (msgtype == GROUP_CHAT_MSG) {
            cout << "群信息[" << js["groupid"] << "]" << js["time"].get<string>() << "[" << js["id"] << "]" << js["name"].get<string>() << "said:" << js["msg"].get<string>() << endl;
            continue;
        }
        if (msgtype == LOGIN_MSG_ACK) {
            doLoginResponse(js);
            sem_post(&rwsem);
            continue;
        }
        if (msgtype == REG_MSG_ACK) {
            doRegResponse(js);
            sem_post(&rwsem);
            continue;
        }

    }
}

void showCurrentUserData() {
    cout << "========================login user=========================" << endl;
    cout << "current login user => id" << g_currentUser.getId() << "name:" << g_currentUser.getName() << endl;
    cout << "-----------------------friendlist--------------------------" << endl;

    if (!g_currentUserFriendList.empty()) {
        for (User& user : g_currentUserFriendList) {
            cout << user.getId() << " " << user.getName() << " " << user.getState() << endl;
        }
    }

    cout << "------------------------grouplist---------------------------" << endl;
    if (!g_currentUserGroupList.empty()) {
        for (Group& group : g_currentUserGroupList) {
            cout << group.getId() << " " << group.getName() << " " << group.getDesc() << endl;
            for (GroupUser& user : group.getUsers()) {
                cout << user.getId() << " " << user.getName() << " " << user.getState() << " " << user.getRole() << endl;
            }
        }
    }
    cout << "======================================================" << endl;
}
// "help" command handler
void help(int fd = 0, string str = "");
// "chat" command handler
void chat(int, string);
// "addfriend" command handler
void addfriend(int, string);
// "creategroup" command handler
void creategroup(int, string);
// "addgroup" command handler
void addgroup(int, string);
// "groupchat" command handler
void groupchat(int, string);
// "loginout" command handler
void loginout(int, string);
unordered_map<string, string> commandMap = {
    {"help","显示所有支持的命令，格式help"},
    {"chat","一对一聊天，格式chat：friendid：message"},
    {"addfriend","添加好友，格式addfriend：friendid"},
    {"creategroup","创建群组，格式creategroup：groupname：groupdesc"},
    {"addgroup","加入群聊，格式addgroup：groupid"},
    {"groupchat","群聊，格式groupchat：groupid：message"},
    {"loginout","注销，格式loginout"}
};

unordered_map<string, function<void(int, string)>> commandHanderMap = {
    {"help",help},
    {"chat",chat},
    {"addfriend",addfriend},
    {"creategroup",creategroup},
    {"addgroup",addgroup},
    {"groupchat",groupchat},
    {"loginout",loginout}
};

void mainMenu(int clientfd) {
    help();
    char buffer[1024] = { 0 };
    while (isMainMenuRunning) {
        cin.getline(buffer, 1024);
        string commandbuf(buffer);
        string command;
        int idx = commandbuf.find(":");
        if (idx == -1) {
            command = commandbuf;
        }
        else {
            command = commandbuf.substr(0, idx);
        }
        auto it = commandHanderMap.find(command);
        if (it == commandHanderMap.end()) {
            cerr << "invalid input command!" << endl;
            continue;
        }
        //调用相应命令的事件处理回调，mainMenu对修改封闭，添加新功能不需要修改该函数
        it->second(clientfd, commandbuf.substr(idx + 1, commandbuf.size() - idx));
    }
}

void help(int, string) {
    cout << "show command list>>>>>" << endl;
    for (auto& p : commandMap) {
        cout << p.first << ":" << p.second << endl;
    }
    cout << endl;
}

//"chat"  command handler
void chat(int clientfd, string str) {
    int idx = str.find(":");//friendid:message
    if (-1 == idx) {
        cerr << "chat command invalid!" << endl;
        return;
    }
    int friendid = atoi(str.substr(0, idx).c_str());
    string message = str.substr(idx + 1, str.size() - idx);

    json js;
    js["msgid"] = ONE_CHAT_MSG;
    js["id"] = g_currentUser.getId();
    js["name"] = g_currentUser.getName();
    js["toid"] = friendid;
    js["msg"] = message;
    js["time"] = getCurrentTime();
    string buffer = js.dump();

    int len = send(clientfd, buffer.c_str(), strlen(buffer.c_str()) + 1, 0);
    if (len == -1) {
        cerr << "send chat msg error ->" << buffer << endl;
    }
}

void addfriend(int clientfd, string str) {
    int friendid = atoi(str.c_str());
    json js;
    js["msgid"] = ADD_FRIEDN_MSG;
    js["id"] = g_currentUser.getId();
    js["friendid"] = friendid;
    string buffer = js.dump();

    int len = send(clientfd, buffer.c_str(), strlen(buffer.c_str()) + 1, 0);
    if (len == -1) {
        cerr << "send addfriend msg error  ->" << buffer << endl;
    }
}

void creategroup(int clientfd, string str) {
    int idx = str.find(":");
    if (idx == -1) {
        cerr << "creategroup command invalid!" << endl;
        return;
    }
    string groupname = str.substr(0, idx);
    string groupdesc = str.substr(idx + 1, str.size() - idx);

    json js;
    js["msgid"] = CREATE_GROUP_MSG;
    js["id"] = g_currentUser.getId();
    js["groupname"] = groupname;
    js["groupdesc"] = groupdesc;
    string buffer = js.dump();

    int len = send(clientfd, buffer.c_str(), strlen(buffer.c_str()) + 1, 0);
    if (len == -1) {
        cerr << "send creategroup msg error ->" << endl;
    }
}

void addgroup(int clientfd, string str) {
    int groupid = atoi(str.c_str());
    json js;
    js["msgid"] = ADD_GROUP_MSG;
    js["id"] = g_currentUser.getId();
    js["groupid"] = groupid;
    string buffer = js.dump();

    int len = send(clientfd, buffer.c_str(), strlen(buffer.c_str()) + 1, 0);
    if (len == -1) {
        cerr << "send addgroup msg error" << endl;
    }
}
void groupchat(int clientfd, string str) {
    int idx = str.find(":");
    if (idx == -1) {
        cerr << "groupchat command invalid!" << endl;
    }
    int groupid = atoi(str.substr(0, idx).c_str());
    string message = str.substr(idx, strlen(str.c_str()) - idx);

    json js;
    js["msgid"] = GROUP_CHAT_MSG;
    js["id"] = g_currentUser.getId();
    js["name"] = g_currentUser.getName();
    js["groupid"] = groupid;
    js["msg"] = message;
    js["time"] = getCurrentTime();
    string buffer = js.dump();

    int len = send(clientfd, buffer.c_str(), strlen(buffer.c_str()) + 1, 0);
    if (len == -1) {
        cerr << "send groupchat msg error ->" << buffer << endl;
    }
}

void loginout(int clientfd, string) {
    json js;
    js["msgid"] = LOGINOUT_MSG;
    js["id"] = g_currentUser.getId();
    string buffer = js.dump();

    int len = send(clientfd, buffer.c_str(), strlen(buffer.c_str()) + 1, 0);
    if (len == -1) {
        cerr << "send loginout msg error ->" << buffer << endl;
    }
    else {
        isMainMenuRunning = false;
    }
}
string getCurrentTime() {
    auto tt = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    struct tm* ptm = localtime(&tt);
    char date[60] = { 0 };
    sprintf(date, "%d-%02d-%02d %02d:%02d:%02d", (int)ptm->tm_year + 1990, (int)ptm->tm_mon + 1, (int)ptm->tm_mday,
        (int)ptm->tm_hour, (int)ptm->tm_min, (int)ptm->tm_sec);
    return std::string(date);
}