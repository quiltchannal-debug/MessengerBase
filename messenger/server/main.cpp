#include <iostream>
#include <vector>
#include <string>
#include <thread>
#include <mutex>
#include <map>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <sqlite3.h>
#include "../common/protocol.h"

class MessengerServer {
public:
    MessengerServer(int port) : port(port) {
        init_db();
    }

    void start() {
        int server_fd = socket(AF_INET, SOCK_STREAM, 0);
        if (server_fd == -1) {
            perror("socket failed");
            return;
        }

        int opt = 1;
        setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

        sockaddr_in address;
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = INADDR_ANY;
        address.sin_port = htons(port);

        if (bind(server_fd, (struct sockaddr*)&address, sizeof(address)) < 0) {
            perror("bind failed");
            return;
        }

        if (listen(server_fd, 10) < 0) {
            perror("listen failed");
            return;
        }

        std::cout << "Server started on port " << port << std::endl;

        while (true) {
            int client_socket = accept(server_fd, nullptr, nullptr);
            if (client_socket >= 0) {
                std::thread(&MessengerServer::handle_client, this, client_socket).detach();
            }
        }
    }

private:
    int port;
    sqlite3* db;
    std::mutex db_mutex;
    std::map<std::string, int> active_clients;
    std::mutex clients_mutex;

    void init_db() {
        if (sqlite3_open("messenger.db", &db) != SQLITE_OK) {
            std::cerr << "Can't open database: " << sqlite3_errmsg(db) << std::endl;
            exit(1);
        }

        const char* sql_users = "CREATE TABLE IF NOT EXISTS users (id INTEGER PRIMARY KEY, username TEXT UNIQUE, password TEXT);";
        const char* sql_messages = "CREATE TABLE IF NOT EXISTS messages (id INTEGER PRIMARY KEY, sender TEXT, receiver TEXT, content TEXT, timestamp DATETIME DEFAULT CURRENT_TIMESTAMP);";
        
        sqlite3_exec(db, sql_users, 0, 0, 0);
        sqlite3_exec(db, sql_messages, 0, 0, 0);
    }

    void handle_client(int client_socket) {
        char buffer[4096];
        std::string current_user = "";

        while (true) {
            int valread = read(client_socket, buffer, 4096);
            if (valread <= 0) break;

            std::string data(buffer, valread);
            NetworkMessage msg = NetworkMessage::deserialize(data);

            if (msg.cmd == Command::REGISTER) {
                bool success = register_user(msg.sender, msg.content);
                std::string response = success ? "1" : "0";
                send(client_socket, response.c_str(), response.size(), 0);
            } else if (msg.cmd == Command::LOGIN) {
                bool success = login_user(msg.sender, msg.content);
                if (success) {
                    current_user = msg.sender;
                    std::lock_guard<std::mutex> lock(clients_mutex);
                    active_clients[current_user] = client_socket;
                }
                std::string response = success ? "1" : "0";
                send(client_socket, response.c_str(), response.size(), 0);
            } else if (msg.cmd == Command::SEND) {
                save_message(msg.sender, msg.receiver, msg.content);
                // Try to deliver immediately if receiver is online
                std::lock_guard<std::mutex> lock(clients_mutex);
                if (active_clients.count(msg.receiver)) {
                    std::string forward_data = msg.serialize();
                    send(active_clients[msg.receiver], forward_data.c_str(), forward_data.size(), 0);
                }
            } else if (msg.cmd == Command::GET_HISTORY) {
                std::string history = get_history(msg.sender, msg.receiver);
                send(client_socket, history.c_str(), history.size(), 0);
            } else if (msg.cmd == Command::EXIT) {
                break;
            }
        }

        if (!current_user.empty()) {
            std::lock_guard<std::mutex> lock(clients_mutex);
            active_clients.erase(current_user);
        }
        close(client_socket);
    }

    bool register_user(const std::string& user, const std::string& pass) {
        std::lock_guard<std::mutex> lock(db_mutex);
        sqlite3_stmt* stmt;
        const char* sql = "INSERT INTO users (username, password) VALUES (?, ?);";
        sqlite3_prepare_v2(db, sql, -1, &stmt, 0);
        sqlite3_bind_text(stmt, 1, user.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 2, pass.c_str(), -1, SQLITE_STATIC);
        int rc = sqlite3_step(stmt);
        sqlite3_finalize(stmt);
        return rc == SQLITE_DONE;
    }

    bool login_user(const std::string& user, const std::string& pass) {
        std::lock_guard<std::mutex> lock(db_mutex);
        sqlite3_stmt* stmt;
        const char* sql = "SELECT password FROM users WHERE username = ?;";
        sqlite3_prepare_v2(db, sql, -1, &stmt, 0);
        sqlite3_bind_text(stmt, 1, user.c_str(), -1, SQLITE_STATIC);
        
        bool success = false;
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            std::string stored_pass = (const char*)sqlite3_column_text(stmt, 0);
            if (stored_pass == pass) success = true;
        }
        sqlite3_finalize(stmt);
        return success;
    }

    void save_message(const std::string& from, const std::string& to, const std::string& content) {
        std::lock_guard<std::mutex> lock(db_mutex);
        sqlite3_stmt* stmt;
        const char* sql = "INSERT INTO messages (sender, receiver, content) VALUES (?, ?, ?);";
        sqlite3_prepare_v2(db, sql, -1, &stmt, 0);
        sqlite3_bind_text(stmt, 1, from.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 2, to.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 3, content.c_str(), -1, SQLITE_STATIC);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
    }

    std::string get_history(const std::string& u1, const std::string& u2) {
        std::lock_guard<std::mutex> lock(db_mutex);
        sqlite3_stmt* stmt;
        const char* sql = "SELECT sender, content, timestamp FROM messages WHERE (sender = ? AND receiver = ?) OR (sender = ? AND receiver = ?) ORDER BY timestamp;";
        sqlite3_prepare_v2(db, sql, -1, &stmt, 0);
        sqlite3_bind_text(stmt, 1, u1.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 2, u2.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 3, u2.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 4, u1.c_str(), -1, SQLITE_STATIC);

        std::string result = "";
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            result += (const char*)sqlite3_column_text(stmt, 0);
            result += ": ";
            result += (const char*)sqlite3_column_text(stmt, 1);
            result += " [";
            result += (const char*)sqlite3_column_text(stmt, 2);
            result += "]\n";
        }
        sqlite3_finalize(stmt);
        return result;
    }
};

int main() {
    MessengerServer server(12345);
    server.start();
    return 0;
}
