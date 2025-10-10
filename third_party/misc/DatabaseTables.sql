CREATE TABLE roles (
    id SERIAL PRIMARY KEY,
    name TEXT UNIQUE NOT NULL
);

-- Assign roles to users
CREATE TABLE user_roles (
    user_id INT REFERENCES users(id) ON DELETE CASCADE,
    role_id INT REFERENCES roles(id) ON DELETE CASCADE,
    PRIMARY KEY(user_id, role_id)
);

-- Inventory table (for avatar items, furniture, etc.)
CREATE TABLE inventory (
    id SERIAL PRIMARY KEY,
    user_id INT REFERENCES users(id) ON DELETE CASCADE,
    item_name TEXT NOT NULL,
    quantity INT DEFAULT 1
);

CREATE TABLE users (
    id SERIAL PRIMARY KEY,
    username VARCHAR(50) UNIQUE NOT NULL,
    password_hash VARCHAR(256) NOT NULL,
    role VARCHAR(20) DEFAULT 'player'
);                      
                         
-- Rooms table                                         
CREATE TABLE rooms (
    id SERIAL PRIMARY KEY,
    name VARCHAR(50) UNIQUE NOT NULL
    owner_id INT REFERENCES users(id) ON DELETE SET NULL
);
                                                      
-- Optional: users in rooms
CREATE TABLE user_rooms (
    user_id INT REFERENCES users(id),                  
    room_id INT REFERENCES rooms(id),
    PRIMARY KEY(user_id, room_id)
);