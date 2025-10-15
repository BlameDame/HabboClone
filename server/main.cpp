#include <uWebSockets/App.h>
#include <iostream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <sstream>
#include <algorithm>
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

// ----------------------
// Tiny JSON-field helpers (string-based quick extraction)
// These are not a full JSON parser but are robust enough for
// our controlled message shapes: {"type":"X", "reqId":"...","roomId":123, ...}
// ----------------------
static std::string extract_string_field(const std::string& s, const std::string& key) {
    std::string pat = "\"" + key + "\"";
    auto pos = s.find(pat);
    if (pos == std::string::npos) return "";
    pos = s.find(':', pos + pat.size());
    if (pos == std::string::npos) return "";
    // find first quote after colon
    auto q1 = s.find('"', pos);
    if (q1 == std::string::npos) return "";
    auto q2 = s.find('"', q1 + 1);
    if (q2 == std::string::npos) return "";
    return s.substr(q1 + 1, q2 - (q1 + 1));
}

static long extract_int_field(const std::string& s, const std::string& key, long fallback = -1) {
    std::string pat = "\"" + key + "\"";
    auto pos = s.find(pat);
    if (pos == std::string::npos) return fallback;
    pos = s.find(':', pos + pat.size());
    if (pos == std::string::npos) return fallback;
    // substring from pos+1 until comma or closing brace
    auto start = pos + 1;
    while (start < (int)s.size() && isspace((unsigned char)s[start])) start++;
    // accept numbers (and negative)
    std::string num;
    if (start < (int)s.size() && (s[start] == '-' || isdigit((unsigned char)s[start]))) {
        int i = start;
        if (s[i] == '-') { num.push_back('-'); i++; }
        while (i < (int)s.size() && (isdigit((unsigned char)s[i]))) { num.push_back(s[i]); i++; }
        try { return std::stol(num); } catch (...) { return fallback; }
    }
    return fallback;
}

// Build JSON safely for text responses (escape simple quotes)
// Minimal escaping for control characters & quotes:
static std::string escape_json_string(const std::string& in) {
    std::string out;
    out.reserve(in.size() + 10);
    for (char c : in) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default: out += c; break;
        }
    }
    return out;
}

// Create a compact JSON array of room templates (from DB)
static std::string roomTemplatesToJson(const std::vector<RoomTemplate>& tmpls) {
    std::ostringstream ss;
    ss << "[";
    bool first = true;
    for (const auto& t : tmpls) {
        if (!first) ss << ",";
        first = false;
        ss << "{";
        ss << "\"id\":" << t.id << ",";
        ss << "\"name\":\"" << escape_json_string(t.name) << "\",";
        ss << "\"width\":" << t.width << ",";
        ss << "\"height\":" << t.height << ",";
        ss << "\"skew_angle\":" << t.skewAngle << ",";
        ss << "\"texture_path\":\"" << escape_json_string(t.texturePath) << "\",";
        // defaultLayoutJson stored as string (may already be JSON). We will include as string.
        ss << "\"default_layout_json\":\"" << escape_json_string(t.defaultLayoutJson) << "\",";
        ss << "\"editable\":" << (t.editable ? "true" : "false");
        ss << "}";
    }
    ss << "]";
    return ss.str();
}

static std::string roomObjectsToJson(const std::vector<RoomObject>& objs) {
    std::ostringstream ss;
    ss << "[";
    bool first = true;
    for (const auto& o : objs) {
        if (!first) ss << ",";
        first = false;
        ss << "{";
        ss << "\"id\":" << o.id << ",";
        ss << "\"name\":\"" << escape_json_string(o.name) << "\",";
        ss << "\"sprite_path\":\"" << escape_json_string(o.spritePath) << "\",";
        // we store tx/ty as x/y
        ss << "\"tx\":" << o.x << ",";
        ss << "\"ty\":" << o.y << ",";
        ss << "\"rotation\":" << o.rotation << ",";
        ss << "\"scale\":" << o.scale << ",";
        ss << "\"interactable\":" << (o.interactable ? "true" : "false");
        ss << "}";
    }
    ss << "]";
    return ss.str();
}

int main() {
    Database db("dbname=hobo user=dame password=swaa2213 host=localhost");

    // Ensure default rooms from templates exist (safe to call repeatedly)
    db.createRoomFromTemplate(1, 1, "Lobby"); // Create a default Lobby room if not exists
    db.createRoomFromTemplate(1, 2, "Chill Zone");
    db.createRoomFromTemplate(1, 3, "Gaming Room");

    // Quick test authenticate (you already had this)
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

                // If it looks like JSON (starts with '{'), attempt to handle JSON messages using "type" field
                if (!msg.empty() && msg.front() == '{') {
                    // extract a few common fields
                    std::string type = extract_string_field(msg, "type");
                    std::string reqId = extract_string_field(msg, "reqId");
                    // ---------- GET_ROOM_TEMPLATES ----------
                    if (type == "GET_ROOM_TEMPLATES") {
                        auto tmpls = db.getAllRoomTemplates();
                        std::ostringstream out;
                        out << "{";
                        out << "\"type\":\"ROOM_TEMPLATES\",";
                        if (!reqId.empty()) out << "\"reqId\":\"" << escape_json_string(reqId) << "\",";
                        out << "\"data\":" << roomTemplatesToJson(tmpls);
                        out << "}";
                        ws->send(out.str(), opCode);
                        return;
                    }

                    // ---------- GET_ROOM_TEMPLATE (single) ----------
                    if (type == "GET_ROOM_TEMPLATE") {
                        long templateId = extract_int_field(msg, "templateId", -1);
                        auto tplOpt = db.getRoomTemplateById((int)templateId);
                        if (!tplOpt.has_value()) {
                            std::ostringstream out;
                            out << "{";
                            out << "\"type\":\"ROOM_TEMPLATE\",";
                            if (!reqId.empty()) out << "\"reqId\":\"" << escape_json_string(reqId) << "\",";
                            out << "\"error\":\"not_found\"";
                            out << "}";
                            ws->send(out.str(), opCode);
                            return;
                        }
                        const auto tpl = tplOpt.value();
                        std::ostringstream out;
                        out << "{";
                        out << "\"type\":\"ROOM_TEMPLATE\",";
                        if (!reqId.empty()) out << "\"reqId\":\"" << escape_json_string(reqId) << "\",";
                        out << "\"data\":{";
                        out << "\"id\":" << tpl.id << ",";
                        out << "\"name\":\"" << escape_json_string(tpl.name) << "\",";
                        out << "\"width\":" << tpl.width << ",";
                        out << "\"height\":" << tpl.height << ",";
                        out << "\"skew_angle\":" << tpl.skewAngle << ",";
                        out << "\"texture_path\":\"" << escape_json_string(tpl.texturePath) << "\",";
                        out << "\"default_layout_json\":\"" << escape_json_string(tpl.defaultLayoutJson) << "\",";
                        out << "\"editable\":" << (tpl.editable ? "true" : "false");
                        out << "}}";
                        ws->send(out.str(), opCode);
                        return;
                    }

                    // ---------- GET_ROOM_FURNITURE ----------
                    if (type == "GET_ROOM_FURNITURE") {
                        long roomId = extract_int_field(msg, "roomId", -1);
                        if (roomId == -1) {
                            std::ostringstream out;
                            out << "{";
                            out << "\"type\":\"ROOM_FURNITURE\",";
                            if (!reqId.empty()) out << "\"reqId\":\"" << escape_json_string(reqId) << "\",";
                            out << "\"data\":[]";
                            out << "}";
                            ws->send(out.str(), opCode);
                            return;
                        }
                        auto objs = db.getRoomObjects((int)roomId);
                        std::ostringstream out;
                        out << "{";
                        out << "\"type\":\"ROOM_FURNITURE\",";
                        if (!reqId.empty()) out << "\"reqId\":\"" << escape_json_string(reqId) << "\",";
                        out << "\"data\":" << roomObjectsToJson(objs);
                        out << "}";
                        ws->send(out.str(), opCode);
                        return;
                    }

                    // ---------- SUBSCRIBE_ROOM ----------
                    // Client requests to be added to broadcast list for a named room
                    if (type == "SUBSCRIBE_ROOM") {
                        std::string roomName = extract_string_field(msg, "room");
                        if (!roomName.empty()) {
                            rooms[roomName].insert(ws);
                            // send back current room state (layout + furniture)
                            int roomId = db.getPublicRoomIdByName(roomName);
                            std::vector<RoomObject> objs;
                            if (roomId != -1) objs = db.getRoomObjects(roomId);

                            std::ostringstream out;
                            out << "{";
                            out << "\"type\":\"ROOM_STATE\",";
                            if (!reqId.empty()) out << "\"reqId\":\"" << escape_json_string(reqId) << "\",";
                            out << "\"room\":\"" << escape_json_string(roomName) << "\",";
                            out << "\"furniture\":" << roomObjectsToJson(objs);
                            out << "}";
                            ws->send(out.str(), opCode);
                        } else {
                            std::ostringstream out;
                            out << "{";
                            out << "\"type\":\"SUBSCRIBE_ROOM_RESPONSE\",";
                            if (!reqId.empty()) out << "\"reqId\":\"" << escape_json_string(reqId) << "\",";
                            out << "\"error\":\"missing_room\"";
                            out << "}";
                            ws->send(out.str(), opCode);
                        }
                        return;
                    }

                    // ---------- CREATE_FURNITURE ----------
                    // Persist new furniture to DB. Expect fields: room (name) and furniture object with proto_id, tx, ty
                    if (type == "CREATE_FURNITURE") {
                        std::string roomName = extract_string_field(msg, "room");
                        std::string uid = extract_string_field(msg, "uid"); // client's local uid (we echo it back)
                        // naive extraction of nested "furniture":{"proto_id":"sofa","tx":4,"ty":3,"color":...}
                        std::string proto = extract_string_field(msg, "proto_id");
                        long tx = extract_int_field(msg, "tx", 0);
                        long ty = extract_int_field(msg, "ty", 0);

                        int roomId = -1;
                        if (!roomName.empty()) roomId = db.getPublicRoomIdByName(roomName);
                        if (roomId == -1) {
                            // attempt to find by current user's room id
                            if (ws->getUserData()->currentRoomId != -1) roomId = ws->getUserData()->currentRoomId;
                        }

                        if (roomId == -1) {
                            std::ostringstream out;
                            out << "{";
                            out << "\"type\":\"CREATE_FURNITURE_RESPONSE\",";
                            if (!reqId.empty()) out << "\"reqId\":\"" << escape_json_string(reqId) << "\",";
                            out << "\"error\":\"room_not_found\"";
                            out << "}";
                            ws->send(out.str(), opCode);
                            return;
                        }

                        // Persist: we map proto -> name, leave sprite_path empty for now
                        bool ok = db.addRoomObject(roomId, proto.empty() ? "furniture" : proto, "", (float)tx, (float)ty, 0.0f, 1.0f, false);

                        // fetch fresh furniture and broadcast ROOM_STATE to room
                        auto objs = db.getRoomObjects(roomId);
                        std::ostringstream broadcast;
                        broadcast << "{";
                        broadcast << "\"type\":\"ROOM_STATE\",";
                        broadcast << "\"room\":\"" << escape_json_string(roomName) << "\",";
                        broadcast << "\"furniture\":" << roomObjectsToJson(objs);
                        broadcast << "}";
                        // broadcast to sockets subscribed to that room
                        if (!roomName.empty()) {
                            for (auto client : rooms[roomName]) {
                                client->send(broadcast.str(), uWS::OpCode::TEXT);
                            }
                        }

                        // reply to the originator (include original uid so client can map)
                        std::ostringstream out;
                        out << "{";
                        out << "\"type\":\"CREATE_FURNITURE_RESPONSE\",";
                        if (!reqId.empty()) out << "\"reqId\":\"" << escape_json_string(reqId) << "\",";
                        out << "\"ok\":" << (ok ? "true" : "false") << ",";
                        out << "\"uid\":\"" << escape_json_string(uid) << "\"";
                        out << "}";
                        ws->send(out.str(), opCode);
                        return;
                    }

                    // ---------- UPDATE_FURNITURE ----------
                    // We broadcast update to room clients. Persistence of updates is left as-is (see note).
                    if (type == "UPDATE_FURNITURE") {
                        std::string roomName = extract_string_field(msg, "room");
                        std::string uid = extract_string_field(msg, "uid");
                        long tx = extract_int_field(msg, "tx", 0);
                        long ty = extract_int_field(msg, "ty", 0);

                        // build update payload
                        std::ostringstream broadcast;
                        broadcast << "{";
                        broadcast << "\"type\":\"FURNITURE_UPDATED\",";
                        if (!reqId.empty()) broadcast << "\"reqId\":\"" << escape_json_string(reqId) << "\",";
                        broadcast << "\"room\":\"" << escape_json_string(roomName) << "\",";
                        broadcast << "\"furniture\":{";
                        broadcast << "\"uid\":\"" << escape_json_string(uid) << "\",";
                        broadcast << "\"tx\":" << tx << ",";
                        broadcast << "\"ty\":" << ty;
                        broadcast << "}}";

                        // broadcast to sockets subscribed to that room
                        if (!roomName.empty()) {
                            for (auto client : rooms[roomName]) {
                                client->send(broadcast.str(), uWS::OpCode::TEXT);
                            }
                        }

                        // reply ack
                        std::ostringstream out;
                        out << "{";
                        out << "\"type\":\"UPDATE_FURNITURE_RESPONSE\",";
                        if (!reqId.empty()) out << "\"reqId\":\"" << escape_json_string(reqId) << "\",";
                        out << "\"ok\":true";
                        out << "}";
                        ws->send(out.str(), opCode);
                        return;
                    }

                    // unknown JSON type -> return an error envelope (or ignore)
                    {
                        std::ostringstream out;
                        out << "{";
                        out << "\"type\":\"ERROR\",";
                        if (!reqId.empty()) out << "\"reqId\":\"" << escape_json_string(reqId) << "\",";
                        out << "\"message\":\"unknown_type\"";
                        out << "}";
                        ws->send(out.str(), opCode);
                        return;
                    }
                } // end JSON handling

                // ---------- FALLBACK: old slash command text handling ----------
                if (!msg.empty() && msg[0] == '/') {
                    // Command processing (kept as you had it)
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

                            ws->send("✅ Logged in as: " + std::to_string(userId) + " " + username, opCode);
                        } else {
                            ws->send("❌ Invalid credentials", opCode);
                        }
                    } else if (msg.find("/register ") == 0) {
                        std::istringstream iss(msg.substr(10));
                        std::string email, username, password;
                        iss >> username >> email >> password;

                        if (email.empty() || username.empty() || password.empty()) {
                            ws->send("❌ Please fill all fields", opCode);
                            return;
                        }

                        if (!db.createUser(username, email, password)) {
                            ws->send("❌ Registration failed (username/email may already exist)", opCode);
                            return;
                        }

                        ws->send("✅ Registration successful! You can now log in.", opCode);
                    } else if (msg.find("/join ") == 0) {
                        std::istringstream iss(msg.substr(6));
                        std::string roomName, pin;
                        iss >> roomName >> pin;

                        int roomId = db.getPublicRoomIdByName(roomName);

                        if (roomId == -1 && !pin.empty()) {
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
                    } else if (msg == "/leave") {
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
                    } else if (msg.find("/check_username ") == 0) {
                        std::string username = msg.substr(16);
                        if (username.empty()) {
                            ws->send("❌ Username cannot be empty", opCode);
                            return;
                        }

                        bool exists = db.isUsernameRegistered(username);
                        if (exists) {
                            ws->send("❌ This username is already taken", opCode);
                        } else {
                            ws->send("✅ Username is available", opCode);
                        }
                    } else {
                        ws->send("❌ Unknown command", opCode);
                    }
                } else { // ROOM CHAT //
                    // Simple chat message to current room
                    std::string room = ws->getUserData()->currentRoomName;
                    int roomId = db.getPublicRoomIdByName(room);
                    std::string username = ws->getUserData()->username;

                    if (!room.empty()) {
                        int room_id = db.getPublicRoomIdByName(room); // you likely already have this or a similar function
                        if (room_id != -1) {
                            db.insertChatMessage(room_id, username, msg);
                        }
                    
                        for (auto client : rooms[room]) {
                            if (client != ws) {
                                client->send(username + ": " + msg, opCode);
                            }
                        }
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
