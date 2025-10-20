// main.js - Fixed version with proper furniture placement and tile clicking

// -------------- GLOBALS --------------
const GLOBAL = {
  tileW: 64,
  tileH: 32,
  originX: 0,
  originY: 0,
  wallColliders: [],
  activeBubbles: {}
};

// -------------- PHASER CONFIG --------------
const phaserConfig = {
  type: Phaser.AUTO,
  parent: 'game-container',
  width: window.innerWidth - 260,
  height: window.innerHeight,
  backgroundColor: '#1e1e1e',
  physics: { 
    default: 'arcade', 
    arcade: { 
      debug: false,
      gravity: { y: 0  }
    } 
  },
  scene: { preload, create, update }
};
const game = new Phaser.Game(phaserConfig);

const WS_URL = "ws://localhost:9001";
let sceneRef = null;
let ws = null;
const players = {};
let currentPlayer = null;
let currentPlayerPOS;
let currentRoom = null;
const furnitureGameObjects = {};
let _uidCounter = 1;
// -------------- DOM UI --------------
const roomButtonsDiv = document.getElementById('room-buttons');
const furnDiv = document.getElementById('furniture-list');
const furniList = document.getElementById('furni-list');
const furniSearch = document.getElementById('furni-search');

let furnitureData = null;
let currentScene = null;
let isDraggingFurniture = false;
let ghostFurniture = null;

furniSearch.addEventListener('input', () => {
  populateFurnitureList(furniSearch.value.toLowerCase());
});

function populateFurnitureList(filter = '') {
  // Load all furniture metadata once
  if (!furnitureData) {
    const furniture = currentScene.cache.json.get('furniture') || {};
    const objects = currentScene.cache.json.get('objects') || {};
    const walls = currentScene.cache.json.get('walls') || {};

    // Merge all into one object
    furnitureData = { ...furniture, ...objects, ...walls };
  }

  if (!furnitureData) return;

  // Clear list before repopulating
  furniList.innerHTML = '';

  for (const [key, info] of Object.entries(furnitureData)) {
    if (filter && !key.toLowerCase().includes(filter)) continue;

    const img = document.createElement('img');
    img.src = `assets/${info.sprite}`;
    img.title = key;
    img.style.cursor = 'grab';

    // Attach drag listener
    img.addEventListener('mousedown', (e) => {
      e.preventDefault();
      startFurnitureDrag(key, e);
    });

    furniList.appendChild(img);
  }
}


function startFurnitureDrag(protoId, mouseEvent) {
  if (!currentScene || isDraggingFurniture) return;
  
  isDraggingFurniture = true;
  const uid = generateUID();
  
  // Create ghost furniture that follows cursor
  const pointer = currentScene.input.activePointer;
  const worldPoint = currentScene.cameras.main.getWorldPoint(pointer.x, pointer.y);
  const { tx, ty } = screenToTile(worldPoint.x, worldPoint.y);
  
  const model = { 
    uid, 
    proto_id: protoId, 
    tx: Math.max(0, Math.min(tx, currentRoom.cols - 1)), 
    ty: Math.max(0, Math.min(ty, currentRoom.rows - 1)), 
    // color: 0x78c 
  };
  
  ghostFurniture = createGhostFurniture(currentScene, model);
  
  // Track mouse movement
  const moveHandler = (e) => {
    if (!ghostFurniture || !currentScene) return;
    
    const pointer = currentScene.input.activePointer;
    const worldPoint = currentScene.cameras.main.getWorldPoint(pointer.x, pointer.y);
    const { tx, ty } = screenToTile(worldPoint.x, worldPoint.y);
    
    if (insideRoom(tx, ty)) {
      const s = tileToScreen(tx, ty);
      ghostFurniture.x = s.x;
      ghostFurniture.y = s.y - (GLOBAL.tileH / 2) * 0.2;
      ghostFurniture.alpha = 0.7;
    } else {
      ghostFurniture.alpha = 0.3;
    }
  };
  
  const upHandler = (e) => {
    if (!ghostFurniture || !currentScene) return;
    
    const pointer = currentScene.input.activePointer;
    const worldPoint = currentScene.cameras.main.getWorldPoint(pointer.x, pointer.y);
    const { tx, ty } = screenToTile(worldPoint.x, worldPoint.y);
    
    if (insideRoom(tx, ty)) {
      // Place furniture
      model.tx = tx;
      model.ty = ty;
      currentRoom.furniture.push(model);
      createFurnitureGO(currentScene, model);
      
      // Send to server
      sendWS({
        type: 'CREATE_FURNITURE',
        room: currentRoom.name,
        uid: model.uid,
        proto_id: model.proto_id,
        tx,
        ty,
        color: model.color
      });
      
      log(`Placed ${protoId} at (${tx}, ${ty})`);
    }
    
    // Cleanup
    ghostFurniture.destroy();
    ghostFurniture = null;
    isDraggingFurniture = false;
    
    document.removeEventListener('mousemove', moveHandler);
    document.removeEventListener('mouseup', upHandler);
  };
  
  document.addEventListener('mousemove', moveHandler);
  document.addEventListener('mouseup', upHandler);
}

function createGhostFurniture(scene, model) {
  const s = tileToScreen(model.tx, model.ty);
  const y = s.y - (GLOBAL.tileH / 2) * 0.2;
  
  const container = scene.add.container(s.x, y);
  container.setDepth(1000);
  container.alpha = 0.7;
  
  if (scene.textures.exists(model.proto_id)) {
    const sprite = scene.add.image(0, 0, model.proto_id).setOrigin(0.5, 0.75).setScale(2);
    if (model.proto_id.includes('wall')){
      sprite.setScale(4);
      sprite.setAlpha(0.5);
      sprite.setOrigin(0.4, 0.9);
    } else if (model.proto_id.includes('bed')){
      sprite.setScale(1.5);
    }
    container.add(sprite);
  } else {
    const size = Math.max(20, GLOBAL.tileW * 0.6);
    const g = scene.add.graphics();
    g.fillStyle(model.color || 0x78c, 1);
    g.fillRoundedRect(-size / 2, -size / 2, size, size * 0.6, 6);
    container.add(g);
  }
  
  return container;
}

window.addEventListener('load', () => {
  const editRoomBtn = document.getElementById('btn-edit-room');
  const editRoomWindow = document.getElementById('edit-room-window');
  const closeEditorBtn = document.getElementById('btn-close-editor');

  if (editRoomBtn && editRoomWindow) {
    editRoomBtn.addEventListener('click', () => {
      editRoomWindow.classList.remove('hidden');
      editRoomWindow.classList.add('visible');
      populateFurnitureList();
    });
  }

  if (closeEditorBtn && editRoomWindow) {
    closeEditorBtn.addEventListener('click', () => {
      editRoomWindow.classList.remove('visible');
      editRoomWindow.classList.add('hidden');
    });
  }
});

function generateUID() { return `f${Date.now().toString(36)}_${_uidCounter++}`; }

function log(msg) {
  const l = document.getElementById('log');
  const line = document.createElement('div');
  line.textContent = `[${new Date().toLocaleTimeString()}] ${msg}`;
  l.prepend(line);
}

function initBottomUI() {
  const btnChat = document.getElementById('btn-chat');
  const btnFriends = document.getElementById('btn-friends');
  const btnPM = document.getElementById('btn-pm');
  const btnRoom = document.getElementById('btn-room');

  if (btnChat) btnChat.onclick = () => toggleSection('chat-window');
  if (btnFriends) btnFriends.onclick = () => toggleSection('friends-window');
  if (btnPM) btnPM.onclick = () => toggleSection('pm-window');
  if (btnRoom) btnRoom.onclick = () => toggleSection('room-window');
}

function toggleSection(id) {
  const el = document.getElementById(id);
  if (!el) return;

  const isVisible = el.classList.contains('visible');
  document.querySelectorAll('.window').forEach(w => {
    w.classList.remove('visible');
    w.classList.add('hidden');
  });

  if (!isVisible) {
    el.classList.remove('hidden');
    el.classList.add('visible');
  }
}

function initChatSystem() {
  const input = document.getElementById('chat-input');
  const sendBtn = document.getElementById('btn-send-chat');

  if (!input || !sendBtn) return;

  sendBtn.onclick = sendChatMessage();
  input.addEventListener('keydown', e => {
    if (e.key === 'Enter') sendChatMessage();
  });
  console.log("Chat system initialized.");
}

function sendChatMessage() {
  const input = document.getElementById('chat-input');
  const msg = input.value.trim();
  if (!msg) return;

  input.value = '';
  
  console.log('Sending message:', msg);
  showChatBubble(msg, getPlayerScreenPos()  /*{ x: window.innerWidth / 2, y: window.innerHeight / 2 }*/);
  if (ws && ws.readyState === WebSocket.OPEN) {
    ws.send(msg);
  } else {
    log('Not connected to server');
  }
  }

function showChatBubble(text, pos) {
  const sprite = players["You"] || currentPlayer;
  if (!sprite || !sceneRef) return;

  const cam = sceneRef.cameras.main;
  
  // Get the actual sprite position in world space
  const worldX = sprite.x;
  const worldY = sprite.y;
  
  // Convert to screen space
  const screenX = (worldX - cam.scrollX) * cam.zoom + cam.x;
  const screenY = (worldY - cam.scrollY) * cam.zoom + cam.y;

  if (!GLOBAL.activeBubbles["You"]) {
    GLOBAL.activeBubbles["You"] = [];
  }
  
  const existingBubbles = GLOBAL.activeBubbles["You"];
  let yOffset = 90;

  existingBubbles.forEach(bubbleData => {
    if (bubbleData.element && document.body.contains(bubbleData.element)){
      yOffset += bubbleData.element.offsetHeight + 5;
    }
  });

  const bubble = document.createElement('div');
  bubble.className = 'chat-bubble';
  bubble.textContent = text;
  document.body.appendChild(bubble);
  
  const gameContainer = document.getElementById('game-container');
  const containerRect = gameContainer.getBoundingClientRect();
  
  bubble.style.left = (containerRect.left + screenX) + 'px';
  bubble.style.top = (containerRect.top + screenY - 90) + 'px';
  
  const bubbleData = {
    element: bubble,
    username: "You",
    createdAt: Date.now(),
  };
  existingBubbles.unshift(bubbleData);

  // console.log(`Chat bubble for ${username} at screen (${screenX}, ${screenY})`);
  
  setTimeout(() => {
    bubble.remove();

    const index = existingBubbles.indexOf(bubbleData);
    if (index > -1){
      existingBubbles.splice(index, 1);
    }

    // repositionBubbles("You");
  }, 4000);
  
}

function displayPlainChatMessage(raw) {
  // expected format: "username: message"
  const splitIdx = raw.indexOf(':');
  let username = 'Someone';
  let message = raw;

  if (splitIdx !== -1) {
    username = raw.substring(0, splitIdx).trim();
    message = raw.substring(splitIdx + 1).trim();
  }

  const pos = getPlayerScreenPos(username);
  showChatBubble(`${username}: ${message}`, pos);
}

function repositionBubbles(username) {
  if (!GLOBAL.activeBubbles[username]) return;
  
  const sprite = players[username] || currentPlayer;
  if (!sprite) return;
  
  const cam = sceneRef.cameras.main;
  const worldX = sprite.x;
  const worldY = sprite.y;
  const screenX = (worldX - cam.scrollX) * cam.zoom + cam.x;
  const screenY = (worldY - cam.scrollY) * cam.zoom + cam.y;
  
  const gameContainer = document.getElementById('game-container');
  const containerRect = gameContainer.getBoundingClientRect();
  
  let yOffset = 90;
  
  GLOBAL.activeBubbles[username].forEach((bubbleData, index) => {
    if (bubbleData.element && document.body.contains(bubbleData.element)) {
      // Smoothly transition to new position
      bubbleData.element.style.transition = 'top 0.3s ease';
      bubbleData.element.style.left = (containerRect.left + screenX) + 'px';
      bubbleData.element.style.top = (containerRect.top + screenY - yOffset) + 'px';
      
      yOffset += bubbleData.element.offsetHeight + 5;
    }
  });
}

function updateBubblePositions() {
  if (!sceneRef || !sceneRef.cameras || !sceneRef.cameras.main) return;
  
  const cam = sceneRef.cameras.main;
  const gameContainer = document.getElementById('game-container');
  if (!gameContainer) return;
  
  const containerRect = gameContainer.getBoundingClientRect();
  
  Object.keys(GLOBAL.activeBubbles).forEach(username => {
    const sprite = players[username] || (username === "You" ? currentPlayer : null);
    if (!sprite) return;
    
    const worldX = sprite.x;
    const worldY = sprite.y;
    const screenX = (worldX - cam.scrollX) * cam.zoom + cam.x;
    const screenY = (worldY - cam.scrollY) * cam.zoom + cam.y;
    
    let yOffset = 90;
    
    GLOBAL.activeBubbles[username].forEach(bubbleData => {
      if (bubbleData.element && document.body.contains(bubbleData.element)) {
        bubbleData.element.style.left = (containerRect.left + screenX) + 'px';
        bubbleData.element.style.top = (containerRect.top + screenY - yOffset) + 'px';
        yOffset += bubbleData.element.offsetHeight + 5;
      }
    });
  });
  repositionBubbles("You");
}

function getPlayerScreenPos(username) {
  const sprite = players[username];
  if (sprite) {
    return { x: sprite.x, y: sprite.y };
  }
  if (currentPlayer) {
    return { x: currentPlayer.x, y: currentPlayer.y };
  }
  return { x: window.innerWidth / 2, y: window.innerHeight / 2 };
}

function spawnPlayer(username, tx, ty) {
  if (!sceneRef) return;
  if (players[username]) return;

  const s = tileToScreen(tx, ty);

  // CHANGED: Use physics.add.sprite instead of add.sprite
  const sprite = sceneRef.physics.add.sprite(s.x, s.y, 'avatar_walk_right', 0);
  sprite.setOrigin(0.5, 0.75);
  sprite.setScale(2);
  sprite.setDepth(s.y);
  sprite.tx = tx;
  sprite.ty = ty;
  
  // ADD: Set up physics body (only for "You", the main player)
  // if (username === "You") {
  //   sprite.body.setSize(20, 20); // Collision box size
  //   sprite.body.setOffset(22, 40); // Offset to match sprite feet
    
  //   // Add collision with walls
  //   if (sceneRef.wallGroup) {
  //     sceneRef.physics.add.collider(sprite, sceneRef.wallGroup);
  //   }
    
    currentPlayer = sprite;
  // }
  
  players[username] = sprite;
}

function movePlayer(username, tx, ty) {
  const sprite = players[username];
  if (!sprite) return;

  if (!insideRoom(tx, ty)) {
    log(`Cannot move ${username} to (${tx}, ${ty}) - outside room bounds.`);
    return;
  }

  const targetPos = tileToScreen(tx, ty);
  const isWallBlocking = GLOBAL.wallColliders.some(wall =>{
    return wall.tx === tx && wall.ty === ty;
  });

  if (isWallBlocking) {
    log(`wall blocking.`);
    return;
  }

// if (username === "You" && currentPlayer) {
//     // Use physics body movement for smooth collision
//     sceneRef.physics.moveTo(
//       currentPlayer, 
//       targetPos.x, 
//       targetPos.y - 16, 
//       200 // speed
//     );
    
//     // Stop after reaching destination
//     sceneRef.time.delayedCall(1200, () => {
//       if (currentPlayer.body) {
//         currentPlayer.body.setVelocity(0, 0);
//         currentPlayer.stop().setFrame(0);
//       }
//     });
    
//     currentPlayer.play('walk', true);
  // } else {  
    sceneRef.tweens.add({
    targets: sprite,
    x: targetPos.x,
    y: targetPos.y - 16,
    duration: 400,
    onStart: () => sprite.play('walk', true),
    onComplete: () => sprite.stop().setFrame(0)
  });

  sprite.tx = tx;
  sprite.ty = ty;

   if (username === "You")
    currentPlayerPOS = { x: tx, y: ty };
  // }
}

function removePlayer(username) {
  if (players[username]) {
    players[username].destroy();
    delete players[username];
  }
}

// -------------- ISOMETRIC HELPERS (FIXED) --------------
function tileToScreen(tx, ty) {
  const w = GLOBAL.tileW;
  const h = GLOBAL.tileH;
  const sx = (tx - ty) * (w / 2);
  const sy = (tx + ty) * (h / 2);
  return { x: sx + GLOBAL.originX, y: sy + GLOBAL.originY };
}

function screenToTile(screenX, screenY) {
  const w = GLOBAL.tileW;
  const h = GLOBAL.tileH;
  
  const x = screenX - GLOBAL.originX;
  const y = screenY - GLOBAL.originY;
  
  const tx = (x / (w / 2) + y / (h / 2)) / 2;
  const ty = (y / (h / 2) - x / (w / 2)) / 2;
  
  return { tx: Math.round(tx), ty: Math.round(ty) };
}

// -------------- PHASER SCENE --------------
function preload() {
  this.load.spritesheet('avatar_walk_right', 'assets/avatar/avatar_walk_right.png', {
    frameWidth: 64,
    frameHeight: 64
  });
  const categories = ['furniture', 'objects', 'walls', 'avatar'];
  categories.forEach(category => {
    this.load.json(category, `metadata/${category}.json`);
  });
}

async function create() {

  initBottomUI();
  initChatSystem();

  sceneRef = this;
  window.gameScene = this;
  currentScene = this;
  const categories = ['furniture', 'objects', 'walls', 'avatar'];

  categories.forEach(category => {
    const data = this.cache.json.get(category);
    if (!data) return;
    
    for (const [key, info] of Object.entries(data)) {
      this.load.image(key, `assets/${info.sprite}`);
    }
  });

  this.wallGroup = this.physics.add.staticGroup();
  
  this.load.once('complete', () => {
    console.log("✅ All furniture and assets loaded!");
  });

  this.load.start();

  await connectWebSocket();

  try {
    const templates = await requestWS({ type: 'GET_ROOM_TEMPLATES' });
    populateRoomButtons(templates || []);
    if (templates && templates.length > 0) {
      loadRoomTemplate(templates[2].id);
    }
  } catch (e) {
    log('Failed to load room templates: ' + e);
  }

  this.cameras.main.setBackgroundColor('#222222');
  this.cameras.main.setZoom(1);
  this.cameras.main.centerToBounds();

  sceneRef.anims.create({
    key: 'walk',
    frames: sceneRef.anims.generateFrameNumbers('avatar_walk_right', { start: 0, end: 4 }),
    frameRate: 16,
    repeat: -1
  });

  // FIXED: Pointer handling with proper tile detection
  this.input.on('pointerdown', pointer => {
    // Ignore if dragging furniture
    if (isDraggingFurniture) return;
    
    const worldPoint = this.cameras.main.getWorldPoint(pointer.x, pointer.y);
    const { tx, ty } = screenToTile(worldPoint.x, worldPoint.y);
    
    if (currentRoom && insideRoom(tx, ty)) {
      log(`Clicked tile (${tx}, ${ty})`);
      movePlayer("You", tx, ty);
      // currentPlayerPOS = { x: tx, y: ty };
      sendWS({ type: 'TILE_CLICK', room: currentRoom.name, tx, ty });
    }
  });

  window.addEventListener('resize', () => {
    this.scale.resize(window.innerWidth - 260, window.innerHeight);
  });
  
  populateFurnitureList();
}

async function joinRoom(roomName) {
  if (!ws || ws.readyState !== WebSocket.OPEN) {
    log('WebSocket not connected yet');
    return;
  }

  ws.send(`/join ${roomName}`);

  currentRoom = {
    name: roomName,
    furniture: [],
    rows: 10,
    cols: 10
  };

  drawRoom();
}

function update(time, dt) {
  // Update logic if needed
  updateBubblePositions();
}

// -------------- WEBSOCKET --------------
function connectWebSocket() {
  if (ws && ws.readyState === WebSocket.OPEN) {
    return Promise.resolve();
  }

  return new Promise((resolve, reject) => {
    try {
      ws = new WebSocket(WS_URL);
    } catch (e) {
      log('WS ctor failed: ' + e.message);
      return reject(e);
    }

    ws.onopen = () => {
      log('WebSocket connected');
      if(sceneRef && sceneRef.textures.exists('avatar_walk_right')){
        spawnPlayer("You", 3, 7);
        log('Spawned player avatar.');
      } else {
        log('Avatar texture not loaded yet.');
      }

      joinRoom("Lobby");

      if (currentRoom && currentRoom.name) {
        sendWS({ type: 'SUBSCRIBE_ROOM', room: currentRoom.name });
      }
      resolve();
    };

    ws.onmessage = ev => {
      try {
        const msg = JSON.parse(ev.data);
        handleWSMessage(msg);
      } catch (e) {
    // Non-JSON = simple room chat message
        const raw = ev.data.trim();
        if (!raw) return;
        if (raw.startsWith('❌') || raw.startsWith('✅') || raw.startsWith('⚠️')) {
          log(raw);
          return;
        }
    if (raw.startsWith('')) {
        log(raw);
      return;
    }
    log(raw)
        displayPlainChatMessage(raw);
      }
    };


    ws.onclose = () => log('WebSocket closed');
    ws.onerror = e => log('WebSocket error');
  });
}

function sendWS(obj) {
  if (!ws || ws.readyState !== WebSocket.OPEN) {
    log('WS not connected. (would send) ' + JSON.stringify(obj));
    return;
  }
  ws.send(JSON.stringify(obj));
}

async function requestWS(obj, timeoutMs = 5000) {
  await connectWebSocket();

  return new Promise((resolve, reject) => {
    const reqId = generateUID();
    obj.reqId = reqId;

    function listener(ev) {
      let payload;
      try { payload = JSON.parse(ev.data); } catch (e) { return; }
      if (payload && payload.reqId === reqId) {
        ws.removeEventListener('message', listener);
        resolve(payload.data);
      }
    }

    ws.addEventListener('message', listener);
    ws.send(JSON.stringify(obj));

    setTimeout(() => {
      try { ws.removeEventListener('message', listener); } catch (e) {}
      reject(new Error('request timed out'));
    }, timeoutMs);
  });
}

// -------------- ROOM UI --------------
function populateRoomButtons(roomTemplates) {
  roomButtonsDiv.innerHTML = '';
  roomTemplates.forEach(tpl => {
    const btn = document.createElement('div');
    btn.className = 'btn secondary';
    btn.textContent = tpl.name;
    btn.onclick = () => loadRoomTemplate(tpl.id);
    roomButtonsDiv.appendChild(btn);
  });
}

async function loadRoomTemplate(roomId) {
  try {
    const tpl = await requestWS({ type: 'GET_ROOM_TEMPLATE', templateId: roomId });
    if (!tpl) {
      log('Template not returned');
      return;
    }

    let layout = null;
    if (tpl.default_layout_json) {
      try {
        layout = JSON.parse(tpl.default_layout_json);
      } catch (err) {
        console.warn("Invalid layout JSON for room:", tpl.name, err);
      }
    }

    currentRoom = {
      id: tpl.id,
      name: tpl.name,
      cols: tpl.width || 10,
      rows: tpl.height || 10,
      skew_angle: tpl.skew_angle || 30,
      furniture: [],
      layout: layout,
    };

    GLOBAL.tileW = 64;
    GLOBAL.tileH = 32;
    GLOBAL.originX = (game.scale.width / 2) - (GLOBAL.tileW / 2);
    GLOBAL.originY = 50;

    const furniture = await requestWS({ type: 'GET_ROOM_FURNITURE', roomId: currentRoom.id });
    currentRoom.furniture = furniture || [];

    drawRoom();
    log(`Loaded room template "${currentRoom.name}" (${currentRoom.cols}x${currentRoom.rows})`);

  } catch (e) {
    log('Failed to load room template: ' + e);
  }

  if (!ws || ws.readyState !== WebSocket.OPEN) return;
}

function insideRoom(tx, ty) {
  if (!currentRoom) return false;

  if (currentRoom.layout && currentRoom.layout.tiles) {
    const layout = currentRoom.layout.tiles;
    if (ty < 0 || ty >= layout.length) return false;
    if (tx < 0 || tx >= layout[ty].length) return false;
    return layout[ty][tx] === 1;
  }

  return tx >= 0 && ty >= 0 && tx < currentRoom.cols && ty < currentRoom.rows;
}

// -------------- DRAW ROOM --------------
function drawRoom() {
  if (!sceneRef || !currentRoom) return;

  if (sceneRef.roomLayer) sceneRef.roomLayer.destroy(true);
  if (sceneRef.furnitureLayer) sceneRef.furnitureLayer.destroy(true);

  if (sceneRef.wallGroup) {
    sceneRef.wallGroup.clear(true, true);
  }
  GLOBAL.wallColliders = [];

  sceneRef.roomLayer = sceneRef.add.layer();
  sceneRef.furnitureLayer = sceneRef.add.layer();

  for (let ty = 0; ty < currentRoom.rows; ty++) {
    for (let tx = 0; tx < currentRoom.cols; tx++) {
      if (!insideRoom(tx, ty)) continue;
      const s = tileToScreen(tx, ty);
      drawIsoTile(sceneRef, s.x, s.y, GLOBAL.tileW, GLOBAL.tileH, (tx+ty)%2 ? 0x8B5A2B : 0x8B4513);
    }
  }

  drawRoomWalls(sceneRef);
  currentRoom.furniture.forEach(f => createFurnitureGO(sceneRef, f));
  centerCameraOnRoom(sceneRef);
}

function drawIsoTile(scene, cx, cy, w, h, color=0x8B4513) {
  const g = scene.add.graphics({ x:0, y:0 });
  g.fillStyle(color,1);
  g.beginPath();
  g.moveTo(cx, cy - h/2);
  g.lineTo(cx + w/2, cy);
  g.lineTo(cx, cy + h/2);
  g.lineTo(cx - w/2, cy);
  g.closePath();
  g.fillPath();
  g.lineStyle(1,0x000000,0.3);
  g.strokePath();
  g.setDepth(0);
  scene.roomLayer.add(g);
}

function drawRoomWalls(scene) {
  const w = GLOBAL.tileW, h = GLOBAL.tileH;

  if (scene.wallGroup) {
    scene.wallGroup.clear(true, true);
  }
  GLOBAL.wallColliders = [];

  for (let tx=0; tx<currentRoom.cols; tx++) {
    for (let ty=0; ty<currentRoom.rows; ty++) {
      if (!insideRoom(tx, ty)) continue;
      const isEdge = tx===0||ty===0||tx===currentRoom.cols-1||ty===currentRoom.rows-1;
      if (!isEdge) continue;
      const s = tileToScreen(tx, ty);
      const g = scene.add.graphics();
      g.fillStyle(0xffffff,1);
      g.fillRect(s.x-w/4, s.y-h-(h/4), w/2, h/2+6);
      g.lineStyle(1,0x000000,0.15);
      g.strokeRect(s.x-w/4, s.y-h-(h/4), w/2, h/2+6);      
      scene.roomLayer.add(g);

      const wallCollider = scene.add.rectangle(
        s.x,
        s.y - h/2,
        w/2,
        h/2,
        0xff0000,
        0.3
      );

      scene.physics.add.existing(wallCollider, true);
      scene.wallGroup.add(wallCollider);
      GLOBAL.wallColliders.push({
        tx, ty,
        body: wallCollider,
        bounds: wallCollider.getBounds()
      });
    }
  }

  if (currentPlayer) {
    scene.physics.add.collider(currentPlayer, scene.wallGroup);
  }

}

// -------------- FURNITURE --------------
function createFurnitureGO(scene, f) {
  const s = tileToScreen(f.tx, f.ty);
  const y = s.y - (GLOBAL.tileH / 2) * 0.2;

  const container = scene.add.container(s.x, y);
  container.setSize(GLOBAL.tileW * 0.6, GLOBAL.tileH * 0.6);
  container.setInteractive(
    new Phaser.Geom.Rectangle(-container.width/2, -container.height/2, container.width, container.height),
    Phaser.Geom.Rectangle.Contains
  );

  container.uid = f.uid || `dbid_${f.id}`;
  container.protoId = f.proto_id || f.name;

  let sprite = null;
  let collisionBody = null;

  if (scene.textures.exists(f.proto_id || f.name)) {
    sprite = scene.add.image(0, 0, f.proto_id || f.name).setOrigin(0.5, 0.75).setScale(2);
    if ((f.proto_id || f.name).includes('wall')){
      sprite.setScale(4);
      sprite.setOrigin(0.4, 0.9);

      collisionBody = scene.add.rectangle(s.x, s.y, GLOBAL.tileW * 0.8, GLOBAL.tileH * 0.8, 0xff0000, 0.3);
      scene.physics.add.existing(collisionBody, true);
      scene.wallGroup.add(collisionBody);

      container.collisionBody = collisionBody;

    } else if (f.proto_id.includes('bed')){
      sprite.setScale(1.5);
    }
    container.add(sprite);
  } else {
    const size = Math.max(20, GLOBAL.tileW * 0.6);
    const g = scene.add.graphics();
    g.fillStyle(f.color || 0x78c, 1);
    g.fillRoundedRect(-size / 2, -size / 2, size, size * 0.6, 6);
    container.add(g);
  }

  container.on('pointerdown', pointer => {
    if (sprite) sprite.setTint(0xffff00);
  });

  scene.input.setDraggable(container);

  let lastTile = { tx: f.tx, ty: f.ty };

  container.on('drag', (pointer, dragX, dragY) => {
    const { tx, ty } = screenToTile(dragX, dragY + GLOBAL.tileH / 4);
    if (insideRoom(tx, ty)) {
      const s2 = tileToScreen(tx, ty);
      container.x = s2.x;
      container.y = s2.y - (GLOBAL.tileH / 2) * 0.2;
      if (collisionBody) {
        collisionBody.x = s2.x;
        collisionBody.y = s2.y;
      }

      lastTile = { tx, ty };
    }
  });

  container.on('dragend', () => {
    const { tx, ty } = lastTile;
    const s2 = tileToScreen(tx, ty);
    container.x = s2.x;
    container.y = s2.y - (GLOBAL.tileH / 2) * 0.2;

    if (collisionBody) {
      collisionBody.x = s2.x;
      collisionBody.y = s2.y;
      collisionBody.body.updateFromGameObject();
    }

    const ff = currentRoom.furniture.find(x => x.uid === container.uid);
    if (ff) { ff.tx = tx; ff.ty = ty; }

    sendWS({ type:'UPDATE_FURNITURE', room: currentRoom.name, uid: container.uid, tx, ty });

    if (sprite) sprite.clearTint();
  });

  scene.furnitureLayer.add(container);
  furnitureGameObjects[container.uid] = container;

  return container;
}

function centerCameraOnRoom(scene) {
  if (!currentRoom) return;
  const centerTile={ tx:Math.floor(currentRoom.cols/2), ty:Math.floor(currentRoom.rows/2) };
  const s = tileToScreen(centerTile.tx, centerTile.ty);
  scene.cameras.main.centerOn(s.x, s.y-20);
}

// -------------- WS MESSAGE HANDLER --------------
function handleWSMessage(msg) {
  if (!msg || !msg.type) return;
  switch(msg.type) {
    case 'ROOM_TEMPLATES':
      if (Array.isArray(msg.data)) populateRoomButtons(msg.data);
      break;
    case 'ROOM_TEMPLATE':
      break;
    case 'ROOM_FURNITURE':
      if (Array.isArray(msg.data)) {
        currentRoom.furniture = msg.data.map(f => {
          return {
            id: f.id,
            name: f.name,
            sprite_path: f.sprite_path,
            tx: f.tx,
            ty: f.ty,
            rotation: f.rotation,
            scale: f.scale,
            interactable: f.interactable
          };
        });
        if (sceneRef) {
          Object.values(furnitureGameObjects).forEach(go => go.destroy());
          Object.keys(furnitureGameObjects).forEach(k => delete furnitureGameObjects[k]);
          currentRoom.furniture.forEach(f => createFurnitureGO(sceneRef, f));
        }
      }
      break;
    case 'ROOM_STATE':
      if (msg.room === currentRoom.name) {
        currentRoom.furniture = msg.furniture || [];
        Object.values(furnitureGameObjects).forEach(go => go.destroy());
        Object.keys(furnitureGameObjects).forEach(k => delete furnitureGameObjects[k]);
        currentRoom.furniture.forEach(f => createFurnitureGO(sceneRef, f));
        log('Room state synced from server.');
      }
      break;
    case 'FURNITURE_UPDATED':
      if (msg.room !== currentRoom.name) return;
      const f = msg.furniture;
      const existing = currentRoom.furniture.find(x => x.uid === f.uid || x.id === f.id);
      if (existing) {
        existing.tx = f.tx;
        existing.ty = f.ty;
        const go = furnitureGameObjects[f.uid || `dbid_${f.id}`];
        if (go) {
          const s = tileToScreen(f.tx, f.ty);
          go.x = s.x; go.y = s.y - (GLOBAL.tileH/2) * 0.2;
        }
      } else {
        const model = {
          uid: f.uid || (`dbid_${f.id}`),
          id: f.id,
          proto_id: f.proto_id || f.name,
          tx: f.tx,
          ty: f.ty,
          color: f.color || undefined
        };
        currentRoom.furniture.push(model);
        createFurnitureGO(sceneRef, model);
      }
      break;
    default:
      // other messages (create/update responses) are ignored here (requestWS resolves them)
      // But we log for debugging:
      log(typeof raw === 'string' ? raw : JSON.stringify(raw));
      break;
  }
}