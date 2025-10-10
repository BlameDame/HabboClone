#pragma once
#include <pqxx/pqxx>
#include <string>
#include <optional>
#include <unordered_set>
#include <vector>
#include <iostream>
#include "bcrypt/bcrypt.h"

using namespace std;

struct RoomInfo {
    int id;
    string name;
    int ownerId;
    bool isPublic;
    optional<string> pinCode;
    int playerCount;
};

class Database {
public:
    Database(const string& connStr) {
        try {
            conn = new pqxx::connection(connStr);
            if (conn->is_open()) {
                cout << "✅ Connected to database: " << conn->dbname() << endl;

                // Prepare commonly used statements
                conn->prepare("get_user", "SELECT id, password_hash FROM users WHERE username=$1");
                conn->prepare("create_user", "INSERT INTO users(username, email, password_hash, role) VALUES($1, $2, $3, $4)");
                conn->prepare("get_room", "SELECT * FROM rooms WHERE name=$1");
                conn->prepare("get_room_by_owner", "SELECT * FROM rooms WHERE name=$1 AND owner_id=$2");
                conn->prepare("get_public_room_by_name", "SELECT * FROM rooms WHERE name=$1 AND is_public=true");
                conn->prepare("create_room", "INSERT INTO rooms(name, owner_id, is_public, pin_code, players_connected, player_count) " "VALUES ($1, $2, $3, NULLIF($4, ''), '[]'::jsonb, 0) " "ON CONFLICT (name) DO NOTHING");
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

    bool isEmailRegistered(const std::string& email) {
        try {
            pqxx::work W(*conn);
            pqxx::result R = W.exec("SELECT 1 FROM users WHERE email=" + W.quote(email));
            W.commit();
            return !R.empty();
        } catch (const std::exception &e) {
            std::cerr << "DB error (isEmailRegistered): " << e.what() << std::endl;
            return true; // Treat as taken if error occurs
        }
    }


    // ----------------------
    // Room management
    // ----------------------
int createRoom(const string& roomName, int ownerId, bool isPublic = true, const optional<string>& pinCode = nullopt) {
        try {
            pqxx::work W(*conn);
            string pinValue = pinCode.has_value() ? pinCode.value() : "";
            W.exec_prepared("create_room", roomName, ownerId, isPublic, pinValue);
            W.commit();
            return getRoomIdByOwner(roomName, ownerId, pinCode);
        } catch (const exception &e) {
            cerr << "DB error (createRoom): " << e.what() << endl;
            return -1;
        }
    }

    // Join private/public room by owner
    int getRoomIdByOwner(const string& roomName, int ownerId, const optional<string>& pinCode = nullopt) {
        try {
            pqxx::work W(*conn);
            pqxx::result R = W.exec_prepared("get_room_by_owner", roomName, ownerId);
            W.commit();

            if (R.size() != 1) return -1;

            if (!R[0]["pin_code"].is_null()) {
                string roomPin = R[0]["pin_code"].c_str();
                if (!pinCode.has_value() || pinCode.value() != roomPin) return -1; // invalid pin
            }

            return R[0]["id"].as<int>();
        } catch (const exception &e) {
            cerr << "DB error (getRoomIdByOwner): " << e.what() << endl;
            return -1;
        }
    }

    // Get public room by name
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

    // Get all rooms (public + private) ordered by player count
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
                rooms.push_back(r);
            }
        } catch (const exception &e) {
            cerr << "DB error (getAllRoomsOrderedByPlayers): " << e.what() << endl;
        }
        return rooms;
    }
    void addPlayerToRoom(int userId, int roomId) {
        try {
            pqxx::work W(*conn);

            // Check if already in array
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
    // User roles
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
    // ----------------------
    // Inventory
    // ----------------------
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
