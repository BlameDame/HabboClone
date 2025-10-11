#pragma once
#include <pqxx/pqxx>
#include <string>
#include <optional>
#include <unordered_set>
#include <vector>
#include <iostream>
#include "bcrypt.h"

using namespace std;

// ---------- Room-Related Structs ----------
struct RoomInfo {
    int id;
    string name;
    int ownerId;
    bool isPublic;
    optional<string> pinCode;
    int playerCount;
    string layoutJson;
};

struct RoomMetadata {
    int id;
    string name;
    float width;
    float height;
    float skewAngle;
    string texturePath;
    bool editable;
};

struct RoomObject {
    int id;
    string name;
    string spritePath;
    float x, y, rotation, scale;
    bool interactable;
};

struct RoomTemplate {
    int id;
    string name;
    float width;
    float height;
    float skewAngle;
    string texturePath;
    string defaultLayoutJson;
    bool editable;
};


// ---------- Player Position Struct ----------
struct PlayerPosition {
    int userId;
    int roomId;
    float x, y;
    string direction;
};

// ---------- Database Class ----------
class Database {
public:
    Database(const string& connStr) {
        try {
            conn = new pqxx::connection(connStr);
            if (conn->is_open()) {
                cout << "✅ Connected to database: " << conn->dbname() << endl;

                // Prepare common statements
                conn->prepare("get_user", "SELECT id, password_hash FROM users WHERE username=$1");
                conn->prepare("create_user", "INSERT INTO users(username, email, password_hash, role) VALUES($1, $2, $3, $4)");
                conn->prepare("get_room", "SELECT * FROM rooms WHERE name=$1");
                conn->prepare("get_room_by_owner", "SELECT * FROM rooms WHERE name=$1 AND owner_id=$2");
                conn->prepare("get_public_room_by_name", "SELECT * FROM rooms WHERE name=$1 AND is_public=true");
                conn->prepare("create_room", "INSERT INTO rooms(name, owner_id, is_public, pin_code, layout_json, editable, width, height, skew_angle, texture_path) " "VALUES ($1, $2, $3, NULLIF($4, ''), $5, $6, $7, $8, $9, $10) " "ON CONFLICT (name) DO NOTHING");
                conn->prepare("insert_furniture", "INSERT INTO room_objects(room_id, name, sprite_path, x, y, rotation, scale, interactable) " "VALUES ($1, $2, $3, $4, $5, $6, $7, $8)");
                conn->prepare("get_furniture_by_room", "SELECT * FROM room_objects WHERE room_id=$1");
                conn->prepare("update_player_position", "INSERT INTO player_positions(user_id, room_id, x, y, direction) " "VALUES ($1, $2, $3, $4, $5) " "ON CONFLICT (user_id) DO UPDATE " "SET room_id=EXCLUDED.room_id, x=EXCLUDED.x, y=EXCLUDED.y, direction=EXCLUDED.direction, last_updated=NOW()");
                conn->prepare("get_player_position", "SELECT * FROM player_positions WHERE user_id=$1");
            }
        } catch (const exception &e) {
            cerr << "❌ DB connection failed: " << e.what() << endl;
            exit(1);
        }
    }

    ~Database() {
        if (conn) {
            conn->disconnect();
            delete conn;
        }
    }

    // ----------------------
    // User authentication
    // ----------------------
    optional<int> authenticateUser(const string& username, const string& password) {
        try {
            pqxx::work W(*conn);
            pqxx::result R = W.exec_prepared("get_user", username);
            W.commit();

            if (R.size() != 1) return nullopt;

            string stored_hash = R[0]["password_hash"].c_str();
            if (bcrypt::validatePassword(password, stored_hash)) {
                return R[0]["id"].as<int>();
            }
            return nullopt;
        } catch (const exception &e) {
            cerr << "DB error (authenticateUser): " << e.what() << endl;
            return nullopt;
        }
    }

    bool createUser(const string& username, const string& email, const string& password, string role = "user") {
        try {
            string hashed = bcrypt::generateHash(password);
            pqxx::work W(*conn);
            W.exec_prepared("create_user", username, email, hashed, role);
            W.commit();
            return true;
        } catch (const exception &e) {
            cerr << "DB error (createUser): " << e.what() << endl;
            return false;
        }
    }

    bool isEmailRegistered(const string& email) {
        try {
            pqxx::work W(*conn);
            pqxx::result R = W.exec("SELECT 1 FROM users WHERE email=" + W.quote(email));
            W.commit();
            return !R.empty();
        } catch (const exception &e) {
            cerr << "DB error (isEmailRegistered): " << e.what() << endl;
            return true;
        }
    }

    bool isUsernameRegistered(const string& username) {
        try {
            pqxx::work W(*conn);
            pqxx::result R = W.exec("SELECT 1 FROM users WHERE username=" + W.quote(username));
            W.commit();
            return !R.empty();
        } catch (const exception &e) {
            cerr << "DB error (isUsernameRegistered): " << e.what() << endl;
            return true;
        }
    }

    // ----------------------
    // Room management
    // ----------------------
    int createRoom(const string& roomName, int ownerId, bool isPublic = true, const optional<string>& pinCode = nullopt, const string& layoutJson = "{}", bool editable = true, int width = 10, int height = 10, int skewAngle = 30, const string& texturePath = "") {
        try {
            pqxx::work W(*conn);
            string pinValue = pinCode.has_value() ? pinCode.value() : "";
            W.exec_prepared("create_room", roomName, ownerId, isPublic, pinValue, layoutJson, editable, width, height, skewAngle, texturePath);
            W.commit();
            return getRoomIdByOwner(roomName, ownerId, pinCode);
        } catch (const exception &e) {
            cerr << "DB error (createRoom): " << e.what() << endl;
            return -1;
        }
    }
    int createRoomFromTemplate(int ownerId, int templateId, const string& roomName, const optional<string>& pinCode = nullopt) {
        auto tplOpt = getRoomTemplateById(templateId);
        if (!tplOpt.has_value()) return -1;
        RoomTemplate tpl = tplOpt.value();
        return createRoom(roomName, ownerId, true, pinCode, tpl.defaultLayoutJson, tpl.editable, tpl.width, tpl.height, tpl.skewAngle, tpl.texturePath);
}

    optional<string> getRoomLayout(int roomId) {
        try {
            pqxx::work W(*conn);
            pqxx::result R = W.exec("SELECT layout_json FROM rooms WHERE id=" + to_string(roomId));
            W.commit();
            if (!R.empty()) return R[0]["layout_json"].c_str();
            return nullopt;
        } catch (const exception &e) {
            cerr << "DB error (getRoomLayout): " << e.what() << endl;
            return nullopt;
        }
    }

    void updateRoomLayout(int roomId, const string& layoutJson) {
        try {
            pqxx::work W(*conn);
            W.exec("UPDATE rooms SET layout_json=" + W.quote(layoutJson) + " WHERE id=" + to_string(roomId));
            W.commit();
        } catch (const exception &e) {
            cerr << "DB error (updateRoomLayout): " << e.what() << endl;
        }
    }

    int getRoomIdByOwner(const string& roomName, int ownerId, const optional<string>& pinCode = nullopt) {
        try {
            pqxx::work W(*conn);
            pqxx::result R = W.exec_prepared("get_room_by_owner", roomName, ownerId);
            W.commit();

            if (R.size() != 1) return -1;

            if (!R[0]["pin_code"].is_null()) {
                string roomPin = R[0]["pin_code"].c_str();
                if (!pinCode.has_value() || pinCode.value() != roomPin) return -1;
            }

            return R[0]["id"].as<int>();
        } catch (const exception &e) {
            cerr << "DB error (getRoomIdByOwner): " << e.what() << endl;
            return -1;
        }
    }

    int getPublicRoomIdByName(const string& roomName) {
        try {
            pqxx::work W(*conn);
            pqxx::result R = W.exec_prepared("get_public_room_by_name", roomName);
            W.commit();
            if (R.size() == 1) return R[0]["id"].as<int>();
            return -1;
        } catch (const exception &e) {
            cerr << "DB error (getPublicRoomIdByName): " << e.what() << endl;
            return -1;
        }
    }

    vector<RoomInfo> getAllRoomsOrderedByPlayers() {
        vector<RoomInfo> rooms;
        try {
            pqxx::work W(*conn);
            pqxx::result R = W.exec("SELECT * FROM rooms ORDER BY player_count DESC");
            W.commit();
            for (auto row : R) {
                RoomInfo r;
                r.id = row["id"].as<int>();
                r.name = row["name"].c_str();
                r.ownerId = row["owner_id"].as<int>();
                r.isPublic = row["is_public"].as<bool>();
                r.pinCode = row["pin_code"].is_null() ? nullopt : optional<string>{row["pin_code"].c_str()};
                r.playerCount = row["player_count"].as<int>();
                r.layoutJson = row["layout_json"].c_str();
                rooms.push_back(r);
            }
        } catch (const exception &e) {
            cerr << "DB error (getAllRoomsOrderedByPlayers): " << e.what() << endl;
        }
        return rooms;
    }

// ----------------------
// Room Templates (Default Layouts)
// ----------------------
vector<RoomTemplate> getAllRoomTemplates() {
    vector<RoomTemplate> templates;
    try {
        pqxx::work W(*conn);
        pqxx::result R = W.exec("SELECT * FROM room_templates ORDER BY name ASC");
        W.commit();

        for (auto row : R) {
            RoomTemplate t;
            t.id = row["id"].as<int>();
            t.name = row["name"].c_str();
            t.width = row["width"].as<float>();
            t.height = row["height"].as<float>();
            t.skewAngle = row["skew_angle"].as<float>();
            t.texturePath = row["texture_path"].c_str();
            t.defaultLayoutJson = row["default_layout_json"].c_str();
            t.editable = row["editable"].as<bool>();
            templates.push_back(t);
        }
    } catch (const exception &e) {
        cerr << "DB error (getAllRoomTemplates): " << e.what() << endl;
    }
    return templates;
}

optional<RoomTemplate> getRoomTemplateById(int templateId) {
    try {
        pqxx::work W(*conn);
        pqxx::result R = W.exec("SELECT * FROM room_templates WHERE id=" + W.quote(templateId));
        W.commit();
        if (R.empty()) return nullopt;

        RoomTemplate t;
        t.id = R[0]["id"].as<int>();
        t.name = R[0]["name"].c_str();
        t.width = R[0]["width"].as<float>();
        t.height = R[0]["height"].as<float>();
        t.skewAngle = R[0]["skew_angle"].as<float>();
        t.texturePath = R[0]["texture_path"].c_str();
        t.defaultLayoutJson = R[0]["default_layout_json"].c_str();
        t.editable = R[0]["editable"].as<bool>();
        return t;
    } catch (const exception &e) {
        cerr << "DB error (getRoomTemplateById): " << e.what() << endl;
        return nullopt;
    }
}
    // ----------------------
    // Room Objects (Furniture)
    // ----------------------
    vector<RoomObject> getRoomObjects(int roomId) {
        vector<RoomObject> objects;
        try {
            pqxx::work W(*conn);
            pqxx::result R = W.exec_prepared("get_furniture_by_room", roomId);
            W.commit();
            for (auto row : R) {
                RoomObject o;
                o.id = row["id"].as<int>();
                o.name = row["name"].c_str();
                o.spritePath = row["sprite_path"].c_str();
                o.x = row["x"].as<float>();
                o.y = row["y"].as<float>();
                o.rotation = row["rotation"].as<float>();
                o.scale = row["scale"].as<float>();
                o.interactable = row["interactable"].as<bool>();
                objects.push_back(o);
            }
        } catch (const exception &e) {
            cerr << "DB error (getRoomObjects): " << e.what() << endl;
        }
        return objects;
    }

    bool addRoomObject(int roomId, const string& name, const string& spritePath,
                       float x, float y, float rotation = 0, float scale = 1.0, bool interactable = false) {
        try {
            pqxx::work W(*conn);
            W.exec_prepared("insert_furniture", roomId, name, spritePath, x, y, rotation, scale, interactable);
            W.commit();
            return true;
        } catch (const exception &e) {
            cerr << "DB error (addRoomObject): " << e.what() << endl;
            return false;
        }
    }

    void clearRoomObjects(int roomId) {
        try {
            pqxx::work W(*conn);
            W.exec("DELETE FROM room_objects WHERE room_id=" + W.quote(roomId));
            W.commit();
        } catch (const exception &e) {
            cerr << "DB error (clearRoomObjects): " << e.what() << endl;
        }
    }
    // ----------------------
    // Room Metadata
    // ----------------------
    optional<RoomMetadata> getRoomMetadata(int roomId) {
        try {
            pqxx::work W(*conn);
            pqxx::result R = W.exec(
                "SELECT id, name, width, height, skew_angle, texture_path, editable FROM rooms WHERE id=" + W.quote(roomId)
            );
            W.commit();

            if (R.empty()) return nullopt;

            RoomMetadata rm;
            rm.id = R[0]["id"].as<int>();
            rm.name = R[0]["name"].c_str();
            rm.width = R[0]["width"].as<float>();
            rm.height = R[0]["height"].as<float>();
            rm.skewAngle = R[0]["skew_angle"].as<float>();
            rm.texturePath = R[0]["texture_path"].c_str();
            rm.editable = R[0]["editable"].as<bool>();
            return rm;
        } catch (const exception &e) {
            cerr << "DB error (getRoomMetadata): " << e.what() << endl;
            return nullopt;
        }
    }

    // ----------------------
    // Player Position
    // ----------------------
    void updatePlayerPosition(int userId, int roomId, float x, float y, const string& direction) {
        try {
            pqxx::work W(*conn);
            W.exec_prepared("update_player_position", userId, roomId, x, y, direction);
            W.commit();
        } catch (const exception &e) {
            cerr << "DB error (updatePlayerPosition): " << e.what() << endl;
        }
    }

    optional<PlayerPosition> getPlayerPosition(int userId) {
        try {
            pqxx::work W(*conn);
            pqxx::result R = W.exec_prepared("get_player_position", userId);
            W.commit();
            if (R.empty()) return nullopt;

            PlayerPosition pp;
            pp.userId = R[0]["user_id"].as<int>();
            pp.roomId = R[0]["room_id"].as<int>();
            pp.x = R[0]["x"].as<float>();
            pp.y = R[0]["y"].as<float>();
            pp.direction = R[0]["direction"].c_str();
            return pp;
        } catch (const exception &e) {
            cerr << "DB error (getPlayerPosition): " << e.what() << endl;
            return nullopt;
        }
    }

    // ----------------------
    // Player management
    // ----------------------
    void addPlayerToRoom(int userId, int roomId) {
        try {
            pqxx::work W(*conn);
            string checkSql = "SELECT (players_connected @> to_jsonb(ARRAY[" + to_string(userId) + "]::int[])) AS exists "
                              "FROM rooms WHERE id = " + to_string(roomId) + ";";
            pqxx::result checkR = W.exec(checkSql);
            bool already = !checkR.empty() && checkR[0]["exists"].as<bool>();

            if (!already) {
                string updateSql =
                    "UPDATE rooms SET "
                    "players_connected = players_connected || to_jsonb(ARRAY[" + to_string(userId) + "]::int[]), "
                    "player_count = jsonb_array_length(players_connected || to_jsonb(ARRAY[" + to_string(userId) + "]::int[])) "
                    "WHERE id = " + to_string(roomId) + ";";
                W.exec(updateSql);
            }

            W.commit();
        } catch (const exception &e) {
            cerr << "DB error (addPlayerToRoom): " << e.what() << endl;
        }
    }

    void removePlayerFromRoom(int userId, int roomId) {
        try {
            pqxx::work W(*conn);
            string updateSql =
                "UPDATE rooms SET "
                "players_connected = (SELECT COALESCE(jsonb_agg(elem), '[]'::jsonb) "
                "FROM jsonb_array_elements(players_connected) AS elems(elem) WHERE elem <> to_jsonb(" + to_string(userId) + ")), "
                "player_count = (SELECT jsonb_array_length(COALESCE(jsonb_agg(elem), '[]'::jsonb)) "
                "FROM jsonb_array_elements(players_connected) AS elems(elem) WHERE elem <> to_jsonb(" + to_string(userId) + ")) "
                "WHERE id = " + to_string(roomId) + ";";
            W.exec(updateSql);
            W.commit();
        } catch (const exception &e) {
            cerr << "DB error (removePlayerFromRoom): " << e.what() << endl;
        }
    }

    vector<int> getPlayersInRoom(int roomId) {
        vector<int> users;
        try {
            pqxx::work W(*conn);
            string q = "SELECT jsonb_array_elements_text(players_connected) AS uid FROM rooms WHERE id = " + to_string(roomId) + ";";
            pqxx::result R = W.exec(q);
            W.commit();
            for (auto row : R) {
                try { users.push_back(stoi(row["uid"].c_str())); } catch (...) {}
            }
        } catch (const exception &e) {
            cerr << "DB error (getPlayersInRoom): " << e.what() << endl;
        }
        return users;
    }

    // ----------------------
    // Roles & Inventory
    // ----------------------
    unordered_set<string> getUserRoles(int userId) {
        unordered_set<string> roles;
        try {
            pqxx::work W(*conn);
            pqxx::result R = W.exec(
                "SELECT r.name FROM roles r "
                "JOIN user_roles ur ON r.id = ur.role_id "
                "WHERE ur.user_id=" + W.quote(userId) + ";"
            );
            W.commit();

            for (auto row : R) roles.insert(row["name"].c_str());
        } catch (const exception &e) {
            cerr << "DB error fetching roles: " << e.what() << endl;
        }
        return roles;
    }

    vector<string> getUserInventory(int userId) {
        vector<string> items;
        try {
            pqxx::work W(*conn);
            pqxx::result R = W.exec(
                "SELECT item_name FROM inventory WHERE user_id=" + W.quote(userId) + ";"
            );
            W.commit();

            for (auto row : R) items.push_back(row["item_name"].c_str());
        } catch (const exception &e) {
            cerr << "DB error fetching inventory: " << e.what() << endl;
        }
        return items;
    }

private:
    pqxx::connection* conn;
};
