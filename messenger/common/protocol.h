#pragma once
#include <string>
#include <vector>
#include <sstream>

enum class Command {
    REGISTER,
    LOGIN,
    SEND,
    GET_HISTORY,
    EXIT,
    UNKNOWN
};

struct NetworkMessage {
    Command cmd;
    std::string sender;
    std::string receiver;
    std::string content;

    std::string serialize() const {
        std::stringstream ss;
        ss << static_cast<int>(cmd) << "|" << sender << "|" << receiver << "|" << content;
        return ss.str();
    }

    static NetworkMessage deserialize(const std::string& data) {
        std::stringstream ss(data);
        std::string item;
        NetworkMessage msg;
        
        if (std::getline(ss, item, '|')) msg.cmd = static_cast<Command>(std::stoi(item));
        if (std::getline(ss, item, '|')) msg.sender = item;
        if (std::getline(ss, item, '|')) msg.receiver = item;
        if (std::getline(ss, item, '|')) msg.content = item;
        
        return msg;
    }
};
