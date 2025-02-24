/******************************************************************************

                              Online C++ Compiler.
               Code, Compile, Run and Debug C++ program online.
Write your code in this editor and press "Run" button to compile and execute it.

*******************************************************************************/

#include <iostream>
#include <thread>
#include <set>
#include <map>
#include <vector>
#include <mutex>
#include <cstring>
#include <unistd.h>
#include <arpa/inet.h>
#include <algorithm>
#include <sys/select.h>

std::set<std::string> peerList; // Stores active peers
std::mutex peerMutex; // Synchronization for peer list
std::map<std::string, std::string> peerTeamNames; // Stores peer's team names
std::map<std::string, int> peerSockets; // Stores peer socket file descriptors
std::mutex teamNameMutex; // Synchronization for peer team names

std::string teamName; // Stores the team name
int myPort; // Stores the assigned port

#define SERVER1_IP "10.206.4.122"
#define SERVER1_PORT 1255
#define SERVER2_IP "10.206.5.228"
#define SERVER2_PORT 6555

void receiveMessages() {
    int serverSock = socket(AF_INET, SOCK_STREAM, 0);
    if (serverSock == -1) {
        std::cerr << "Error creating server socket.\n";
        return;
    }

    struct sockaddr_in serverAddr = {};
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(myPort);
    serverAddr.sin_addr.s_addr = INADDR_ANY;

    if (bind(serverSock, (struct sockaddr*)&serverAddr, sizeof(serverAddr)) == -1) {
        std::cerr << "Binding failed!\n";
        close(serverSock);
        return;
    }

    if (listen(serverSock, 10) == -1) {
        std::cerr << "Listening failed!\n";
        close(serverSock);
        return;
    }

    std::cout << "Server listening on port " << myPort << "...\n";

    fd_set masterSet, readSet;
    FD_ZERO(&masterSet);
    FD_SET(serverSock, &masterSet);
    int maxFd = serverSock;

    while (true) {
        readSet = masterSet;
        if (select(maxFd + 1, &readSet, NULL, NULL, NULL) < 0) {
            std::cerr << "select() failed.\n";
            continue;
        }

        if (FD_ISSET(serverSock, &readSet)) {
            struct sockaddr_in clientAddr = {};
            socklen_t addrSize = sizeof(clientAddr);
            int clientSock = accept(serverSock, (struct sockaddr*)&clientAddr, &addrSize);
            if (clientSock == -1) {
                std::cerr << "Failed to accept connection.\n";
                continue;
            }

            char buffer[1024] = {0};
            recv(clientSock, buffer, sizeof(buffer), 0);

            std::string senderIP = inet_ntoa(clientAddr.sin_addr);
            std::string message(buffer);

            size_t firstSpace = message.find(' ');
            size_t secondSpace = message.find(' ', firstSpace + 1);

            std::string peerInfo = message.substr(0, firstSpace);
            std::string teamNameReceived = message.substr(firstSpace + 1, secondSpace - firstSpace - 1);
            std::string actualMessage = message.substr(secondSpace + 1);

            // Handle peer info
            {
                std::lock_guard<std::mutex> lock(peerMutex);
                if (actualMessage == "quit") {
                    peerList.erase(peerInfo);
                    peerSockets.erase(peerInfo);
                    std::lock_guard<std::mutex> teamLock(teamNameMutex);
                    peerTeamNames.erase(peerInfo);
                    std::cout << "\nPeer " << peerInfo << " (" << teamNameReceived << ") has disconnected.\n";
                } else {
                    peerList.insert(peerInfo);
                    peerSockets[peerInfo] = clientSock;
                    std::lock_guard<std::mutex> teamLock(teamNameMutex);
                    peerTeamNames[peerInfo] = teamNameReceived;
                    std::cout << "\nReceived from [" << peerInfo << "] (" << teamNameReceived << "): " << actualMessage << "\n";
                }
            }

            close(clientSock);
        }
    }
}

void sendMessage(const std::string& ip, int port, const std::string& message) {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock == -1) {
        std::cerr << "Socket creation failed!\n";
        return;
    }

    struct sockaddr_in serverAddr = {};
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(port);
    serverAddr.sin_addr.s_addr = inet_addr(ip.c_str());

    if (connect(sock, (struct sockaddr*)&serverAddr, sizeof(serverAddr)) == -1) {
        std::cerr << "Connection to " << ip << " failed!\n";
        close(sock);
        return;
    }

    std::string formattedMessage = "<" + ip + ":" + std::to_string(myPort) + "> " + teamName + " " + message;
    send(sock, formattedMessage.c_str(), formattedMessage.length(), 0);

    std::cout << "Message sent to " << ip << ":" << port << "\n";

    close(sock);
}

void sendToAll(const std::string& message) {
    std::lock_guard<std::mutex> lock(peerMutex);
    if (peerList.empty()) {
        std::cout << "No peers connected to send message.\n";
        return;
    }

    for (const std::string& peer : peerList) {
        size_t colonPos = peer.find(':');
        std::string ipAddress = peer.substr(1, colonPos - 1);
        int port = std::stoi(peer.substr(colonPos + 1, peer.length() - colonPos - 2));

        sendMessage(ipAddress, port, message);
    }
}

void exitAndNotifyPeers() {
    std::lock_guard<std::mutex> lock(peerMutex);
    for (const auto& peer : peerList) {
        size_t colonPos = peer.find(':');
        std::string ipAddress = peer.substr(1, colonPos - 1);
        int port = std::stoi(peer.substr(colonPos + 1, peer.length() - colonPos - 2));
        sendMessage(ipAddress, port, "quit");
    }
    // Clear the peer list after notifying all peers
    peerList.clear();
    peerSockets.clear();
    peerTeamNames.clear();
}

void showPeers() {
    std::lock_guard<std::mutex> lock(peerMutex);
    std::vector<std::pair<std::string, std::string> > sortedPeers;

    if (peerList.empty()) {
        std::cout << "No active peers.\n";
    } else {
        {
            std::lock_guard<std::mutex> teamLock(teamNameMutex);
            for (const std::string& peer : peerList) {
                sortedPeers.push_back(std::make_pair(peerTeamNames[peer], peer));
            }
        }

        std::sort(sortedPeers.begin(), sortedPeers.end());

        std::cout << "Connected Peers (Sorted by Team Name):\n";
        for (const auto& peerPair : sortedPeers) {
            size_t colonPos = peerPair.second.find(':');
            std::string ipAddress = peerPair.second.substr(1, colonPos - 1);
            std::string portNumber = peerPair.second.substr(colonPos + 1, peerPair.second.length() - colonPos - 2);

            std::cout << " Team: " << peerPair.first << " IP: " << ipAddress << " Port: " << portNumber << "\n";
        }
    }
}

void connectToPeer(const std::string& ip, int port) {
    std::cout << "Attempting to connect to " << ip << ":" << port << "...\n";
    sendMessage(ip, port, "CONNECT");
}

int main() {
    std::cout << "Enter your name: ";
    std::cin >> teamName;
    std::cout << "Enter your port number: ";
    std::cin >> myPort;

    std::thread serverThread(receiveMessages);
    serverThread.detach();

    while (true) {
        std::cout << "\n** Menu **\n";
        std::cout << "1. Send message to an individual\n";
        std::cout << "2. Send message to all connected peers\n";
        std::cout << "3. Query active peers\n";
        std::cout << "4. Connect to a peer\n";
        std::cout << "0. Quit\n";
        std::cout << "Enter choice: ";

        int choice;
        std::cin >> choice;

        if (choice == 1) {
            std::string ip, message;
            int port;
            std::cout << "Enter recipient's IP address: ";
            std::cin >> ip;
            std::cout << "Enter recipient's port number: ";
            std::cin >> port;
            std::cin.ignore();
            std::cout << "Enter your message: ";
            std::getline(std::cin, message);
            sendMessage(ip, port, message);
        } else if (choice == 2) {
            std::cin.ignore();
            std::cout << "Enter your message: ";
            std::string message;
            std::getline(std::cin, message);
            sendToAll(message);
        } else if (choice == 3) {
            showPeers();
        } else if (choice == 4) {
            std::string ip;
            int port;
            std::cout << "Enter peer's IP address: ";
            std::cin >> ip;
            std::cout << "Enter peer's port number: ";
            std::cin >> port;
            connectToPeer(ip, port);
        } else if (choice == 0) {
            std::cout << "Exiting...\n";
            exitAndNotifyPeers();
            break;
        }
    }

    return 0;
}
