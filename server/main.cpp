#include <uWebSockets/App.h>
#include <iostream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include "core/Database.hpp"

struct User {
    int id = -1;                    // DB user ID
    std::string username;
    int currentRoomId = -1;         // DB room ID
    std::string currentRoomName;    // Room name for chat display
    std::unordered_set<std::string> roles; // e.g., admin, helper
    std::vector<std::string> inventory;    // item names for now
};

// ----------------------
// Global state
// ----------------------
std::unordered_set<uWS::WebSocket<false, true, User>*> clients;
std::unordered_map<std::string, std::unordered_set<uWS::WebSocket<false, true, User>*>> rooms;

int main() {
    Database db("dbname=hobo user=dame password=swaa2213 host=localhost");

    auto id = db.authenticateUser("dame", "swaa2213");
    if (id.has_value()) {
        std::cout << "✅ Authenticated user ID: " << id.value() << std::endl;
    } else {
        std::cout << "❌ Invalid login" << std::endl;
    }

    uWS::App()
        .ws<User>("/*", {
            // ----------------------
            // New connection
            // ----------------------
            .open = [&](auto* ws) {
                clients.insert(ws);
                ws->getUserData()->id = -1; // not logged in
            },

            // ----------------------
            // Incoming messages
            // ----------------------
            .message = [&](auto* ws, std::string_view message, uWS::OpCode opCode) {
                std::string msg(message);

                if (!msg.empty() && msg[0] == '/') {
                    // Command processing
                    if (msg.find("/login ") == 0) {
                        auto splitPos = msg.find(' ', 7);
                        if (splitPos == std::string::npos) {
                            ws->send("❌ Usage: /login <username> <password>", opCode);
                            return;
                        }
                    
                        std::string username = msg.substr(7, splitPos - 7);
                        std::string password = msg.substr(splitPos + 1);
                    
                        auto userIdOpt = db.authenticateUser(username, password);
                        if (userIdOpt.has_value()) {
                            int userId = userIdOpt.value();
                            ws->getUserData()->id = userId;
                            ws->getUserData()->username = username;
                            ws->getUserData()->roles = db.getUserRoles(userId);
                            ws->getUserData()->inventory = db.getUserInventory(userId);
                        
                            ws->send("✅ Logged in as: " + username, opCode);
                        } else {
                            ws->send("❌ Invalid credentials", opCode);
                        }
                    }else if (msg.find("/register ") == 0) {
                        std::istringstream iss(msg.substr(10));
                        std::string email, username, password;
                        iss >> email >> username >> password;

                        if (email.empty() || username.empty() || password.empty()) {
                            ws->send("❌ Please fill all fields", opCode);
                            return;
                        }
                    
                        if (!db.createUser(username, email, password)) {  // Assuming createUser overload with email
                            ws->send("❌ Registration failed (username/email may already exist)", opCode);
                            return;
                        }
                    
                        ws->send("✅ Registration successful! You can now log in.", opCode);
                    } else if (msg.find("/join ") == 0) {
                        // Example command formats:
                        // /join Lobby             -> joins public room "Lobby"
                        // /join SecretRoom 1234   -> joins private room "SecretRoom" with pin "1234"
                        // Parse command
                    std::istringstream iss(msg.substr(6));
                    std::string roomName, pin;
                    iss >> roomName >> pin;
                
                    int roomId = db.getPublicRoomIdByName(roomName);
                
                    if (roomId == -1 && !pin.empty()) {
                        // If public room not found, try private room with pin
                        roomId = db.getRoomIdByOwner(roomName, ws->getUserData()->id, pin);
                        if (roomId == -1) {
                            ws->send("❌ No private room found with that name or incorrect pin.", opCode);
                            return;
                        }
                    } else if (roomId == -1) {
                        ws->send("❌ No public room found with that name.", opCode);
                        return;
                    }
                
                    // Leave previous room
                    if (ws->getUserData()->currentRoomId != -1) {
                        std::string prevRoom = ws->getUserData()->currentRoomName;
                        rooms[prevRoom].erase(ws);
                        db.removePlayerFromRoom(ws->getUserData()->id, ws->getUserData()->currentRoomId);
                    
                        for (auto client : rooms[prevRoom])
                            client->send(ws->getUserData()->username + " has left the room.", opCode);
                    }
                
                    // Join new room
                    ws->getUserData()->currentRoomId = roomId;
                    ws->getUserData()->currentRoomName = roomName;
                    rooms[roomName].insert(ws);
                    db.addPlayerToRoom(ws->getUserData()->id, roomId);
                
                    ws->send("✅ Joined room: " + roomName, opCode);
                    for (auto client : rooms[roomName])
                        if (client != ws)
                            client->send(ws->getUserData()->username + " has joined the room.", opCode);
                }
                else if (msg == "/leave") {
                        std::string room = ws->getUserData()->currentRoomName;
                        int roomId = ws->getUserData()->currentRoomId;

                        if (!room.empty() && roomId != -1) {
                            rooms[room].erase(ws);
                            db.removePlayerFromRoom(ws->getUserData()->id, roomId);

                            ws->getUserData()->currentRoomId = -1;
                            ws->getUserData()->currentRoomName = "";
                            ws->send("✅ Left room: " + room, opCode);

                            for (auto client : rooms[room])
                                client->send(ws->getUserData()->username + " has left the room.", opCode);
                        }

                    } else if (msg.find("/kick ") == 0) {
                        if (!ws->getUserData()->roles.count("admin")) {
                            ws->send("❌ You do not have permission to kick users.", opCode);
                            return;
                        }
                        std::string targetUser = msg.substr(6);
                        for (auto client : clients) {
                            if (client->getUserData()->username == targetUser) {
                                client->send("⚠️ You have been kicked by an admin.", opCode);
                                client->close();
                                break;
                            }
                        }

                    } else if (msg.find("/check_email ") == 0) {
                        std::string email = msg.substr(13);
                        if (email.empty()) {
                            ws->send("❌ Email cannot be empty", opCode);
                            return;
                        }
                    
                        bool exists = db.isEmailRegistered(email);
                        if (exists) {
                            ws->send("❌ This email is already registered", opCode);
                        } else {
                            ws->send("✅ Email is available", opCode);
                        }
                } else {
                        ws->send("❌ Unknown command", opCode);
                    }

                } else {
                    // Room chat
                    std::string room = ws->getUserData()->currentRoomName;
                    if (!room.empty()) {
                        for (auto client : rooms[room])
                            if (client != ws)
                                client->send(ws->getUserData()->username + ": " + msg, opCode);
                    } else {
                        ws->send("❌ You are not in a room. Use /join <room_name> [pin]", opCode);
                    }
                }
            },

            // ----------------------
            // Connection closed
            // ----------------------
            .close = [&](auto* ws, int, std::string_view) {
                clients.erase(ws);
                std::string room = ws->getUserData()->currentRoomName;
                int roomId = ws->getUserData()->currentRoomId;

                if (!room.empty() && roomId != -1) {
                    rooms[room].erase(ws);
                    db.removePlayerFromRoom(ws->getUserData()->id, roomId);

                    for (auto client : rooms[room])
                        client->send(ws->getUserData()->username + " has disconnected.", uWS::OpCode::TEXT);
                }
            }
        })
        .listen(9001, [](auto* token) {
            if (token) std::cout << "✅ Server listening on port 9001\n";
            else std::cerr << "❌ Failed to bind port 9001\n";
        })
        .run();

    std::cout << "Server stopped.\n";
    return 0;
}
