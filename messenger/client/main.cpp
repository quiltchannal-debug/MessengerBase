#include <iostream>
#include <string>
#include <thread>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include "../common/protocol.h"

class MessengerClient {
public:
    MessengerClient(const std::string& ip, int port) : ip(ip), port(port) {}

    bool connect_to_server() {
        sock_fd = socket(AF_INET, SOCK_STREAM, 0);
        if (sock_fd < 0) return false;

        sockaddr_in serv_addr;
        serv_addr.sin_family = AF_INET;
        serv_addr.sin_port = htons(port);
        inet_pton(AF_INET, ip.c_str(), &serv_addr.sin_addr);

        if (connect(sock_fd, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) < 0) {
            return false;
        }
        return true;
    }

    void run() {
        std::cout << "1. Register\n2. Login\nChoice: ";
        int choice;
        std::cin >> choice;
        
        std::string user, pass;
        std::cout << "Username: "; std::cin >> user;
        std::cout << "Password: "; std::cin >> pass;

        NetworkMessage msg;
        msg.cmd = (choice == 1) ? Command::REGISTER : Command::LOGIN;
        msg.sender = user;
        msg.content = pass;

        std::string data = msg.serialize();
        send(sock_fd, data.c_str(), data.size(), 0);

        char buffer[1024] = {0};
        read(sock_fd, buffer, 1024);
        
        if (buffer[0] == '1') {
            std::cout << "Success!\n";
            username = user;
            
            // Start thread to receive messages
            std::thread(&MessengerClient::receive_thread, this).detach();
            
            main_loop();
        } else {
            std::cout << "Failed.\n";
        }
    }

private:
    int sock_fd;
    std::string ip;
    int port;
    std::string username;

    void receive_thread() {
        char buffer[4096];
        while (true) {
            int valread = read(sock_fd, buffer, 4096);
            if (valread <= 0) break;
            
            std::string data(buffer, valread);
            if (data.find("|") != std::string::npos) {
                NetworkMessage msg = NetworkMessage::deserialize(data);
                if (msg.cmd == Command::SEND) {
                    std::cout << "\n[" << msg.sender << "]: " << msg.content << "\n> " << std::flush;
                }
            } else {
                // Could be history data or other raw text
                std::cout << "\n--- History ---\n" << data << "---------------\n> " << std::flush;
            }
        }
    }

    void main_loop() {
        while (true) {
            std::cout << "\nCommands: /msg <user> <text>, /history <user>, /exit\n> ";
            std::string input;
            std::getline(std::cin >> std::ws, input);

            if (input.substr(0, 5) == "/msg ") {
                size_t space1 = 5;
                size_t space2 = input.find(" ", space1);
                if (space2 != std::string::npos) {
                    NetworkMessage msg;
                    msg.cmd = Command::SEND;
                    msg.sender = username;
                    msg.receiver = input.substr(space1, space2 - space1);
                    msg.content = input.substr(space2 + 1);
                    std::string data = msg.serialize();
                    send(sock_fd, data.c_str(), data.size(), 0);
                }
            } else if (input.substr(0, 9) == "/history ") {
                NetworkMessage msg;
                msg.cmd = Command::GET_HISTORY;
                msg.sender = username;
                msg.receiver = input.substr(9);
                std::string data = msg.serialize();
                send(sock_fd, data.c_str(), data.size(), 0);
            } else if (input == "/exit") {
                break;
            }
        }
    }
};

int main() {
    MessengerClient client("213.108.1.226", 12345);
    if (client.connect_to_server()) {
        client.run();
    } else {
        std::cerr << "Could not connect to server.\n";
    }
    return 0;
}
