// main.js - Phaser isometric-ish prototype with WS-based DB calls (JSON "type" messages)

// -------------- GLOBALS --------------
const GLOBAL = {
  tileW: 64,
  tileH: 32,
  originX: 0,
  originY: 0
};

const WS_URL = "ws://localhost:9001"; // your server
let sceneRef = null;
let ws = null;
let currentRoom = null; // { id, name, rows, cols, shape, furniture: [] }
const furnitureGameObjects = {};
let _uidCounter = 1;
function generateUID() { return `f${Date.now().toString(36)}_${_uidCounter++}`; }

// -------------- DOM UI --------------
const roomButtonsDiv = document.getElementById('room-buttons');
const furnDiv = document.getElementById('furniture-list');

function log(msg) {
  const l = document.getElementById('log');
  const line = document.createElement('div');
  line.textContent = `[${new Date().toLocaleTimeString()}] ${msg}`;
  l.prepend(line);
}

// bottom UI stub
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
  el.classList.toggle('hidden');
}

// -------------- PHASER CONFIG --------------
const phaserConfig = {
  type: Phaser.AUTO,
  parent: 'game-container',
  width: window.innerWidth - 260,
  height: window.innerHeight,
  backgroundColor: '#1e1e1e',
  physics: { default: 'arcade', arcade: { debug: false } },
  scene: { preload, create, update }
};

const game = new Phaser.Game(phaserConfig);

// -------------- ISOMETRIC HELPERS --------------
function tileToScreen(tx, ty) {
  const w = GLOBAL.tileW, h = GLOBAL.tileH;
  const sx = (tx - ty) * (w / 2);
  const sy = (tx + ty) * (h / 2);
  return { x: sx + GLOBAL.originX, y: sy + GLOBAL.originY };
}

function screenToTile(screenX, screenY) {
  const w = GLOBAL.tileW, h = GLOBAL.tileH;
  const x = screenX - GLOBAL.originX;
  const y = screenY - GLOBAL.originY;
  const tx = ((x / (w/2)) + (y / (h/2))) / 2;
  const ty = ((y / (h/2)) - (x / (w/2))) / 2;
  return { tx: Math.round(tx), ty: Math.round(ty) };
}

// -------------- PHASER SCENE --------------
function preload() {}

async function create() {
  sceneRef = this;
  window.gameScene = this;

  initBottomUI();

  // ensure websocket
  connectWebSocket();

  // request room templates over WS
  try {
    const templates = await requestWS({ type: 'GET_ROOM_TEMPLATES' });
    populateRoomButtons(templates || []);
    if (templates && templates.length > 0) {
      // prefer a "Lobby" if available, otherwise first template
      const lobby = templates.find(t => t.name === "Lobby") || templates[0];
      if (lobby) loadRoomTemplate(lobby.id);
    }
  } catch (e) {
    log('Failed to load room templates: ' + e);
  }

  this.cameras.main.setBackgroundColor('#222222');
  this.cameras.main.setZoom(1);
  this.cameras.main.centerToBounds();

  // pointer handling
  this.input.on('pointerdown', pointer => {
    const p = pointer.positionToCamera(this.cameras.main);
    const { tx, ty } = screenToTile(p.x, p.y);
    if (currentRoom && insideRoom(tx, ty)) {
      log(`Clicked tile ${tx},${ty}`);
      sendWS({ type: 'TILE_CLICK', room: currentRoom.name, tx, ty });
    }
  });

  // resize support
  window.addEventListener('resize', () => {
    this.scale.resize(window.innerWidth - 260, window.innerHeight);
  });
}

function update(time, dt) {}

// -------------- WEBSOCKET REQUEST/RESPONSE machinery --------------
function connectWebSocket() {
  // if it's already open, just resolve immediately
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
      if (currentRoom && currentRoom.name) {
        sendWS({ type: 'SUBSCRIBE_ROOM', room: currentRoom.name });
      }
      resolve();
    };

    ws.onmessage = ev => {
      let msg = null;
      try { msg = JSON.parse(ev.data); } catch (e) { log('Non-json WS: ' + ev.data); return; }
      handleWSMessage(msg);
    };

    ws.onclose = () => log('WebSocket closed');
    ws.onerror = e => log('WebSocket error');
  });
}


// simple send (no reqId)
function sendWS(obj) {
  if (!ws || ws.readyState !== WebSocket.OPEN) {
    log('WS not connected. (would send) ' + JSON.stringify(obj));
    return;
  }
  ws.send(JSON.stringify(obj));
}

// requestWS returns Promise that resolves when server replies with same reqId
async function requestWS(obj, timeoutMs = 5000) {
  await connectWebSocket(); // <-- ensure it’s open before continuing

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
    ws.send(JSON.stringify(obj)); // safe now

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

// -------------- ROOM MANAGEMENT --------------
async function loadRoomTemplate(roomId) {
  try {
    const tpl = await requestWS({ type: 'GET_ROOM_TEMPLATE', templateId: roomId });
    if (!tpl) { log('Template not returned'); return; }
    // server returns data object wrapped as payload.data in our requestWS contract; sometimes it's already
    const data = tpl; // the helper expects the server to return { reqId, data: {...} } -> requestWS returns data
    const layoutJson = data.default_layout_json ? JSON.parse(data.default_layout_json || "{}") : {};
    currentRoom = {
      id: data.id,
      name: data.name,
      rows: layoutJson.rows || (data.height || 10),
      cols: layoutJson.cols || (data.width || 10),
      shape: layoutJson.shape || 'rect',
      furniture: []
    };

    GLOBAL.tileW = 64;
    GLOBAL.tileH = 32;
    GLOBAL.originX = (game.scale.width / 2) - (GLOBAL.tileW / 2);
    GLOBAL.originY = 50;

    // fetch furniture via WS
    const furniture = await requestWS({ type: 'GET_ROOM_FURNITURE', roomId: currentRoom.id });
    currentRoom.furniture = furniture || [];

    drawRoom();
    log(`Loaded room template "${currentRoom.name}" (${currentRoom.cols}x${currentRoom.rows})`);

    // subscribe to server-sent updates for this room
    sendWS({ type: 'SUBSCRIBE_ROOM', room: currentRoom.name });

  } catch (e) {
    log('Failed to load room template: ' + e);
  }

  if (!ws || ws.readyState !== WebSocket.OPEN) connectWebSocket();
}

function insideRoom(tx, ty) {
  if (!currentRoom) return false;
  if (currentRoom.shape === 'L') {
    if (tx >= Math.ceil(currentRoom.cols/2) && ty >= Math.ceil(currentRoom.rows/2)) return false;
  }
  return tx >=0 && ty >=0 && tx < currentRoom.cols && ty < currentRoom.rows;
}

// -------------- DRAW ROOM --------------
function drawRoom() {
  if (!sceneRef || !currentRoom) return;
  if (sceneRef.roomLayer) sceneRef.roomLayer.destroy(true);
  if (sceneRef.furnitureLayer) sceneRef.furnitureLayer.destroy(true);

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
  scene.roomLayer.add(g);
}

function drawRoomWalls(scene) {
  const w = GLOBAL.tileW, h = GLOBAL.tileH;
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
    }
  }
}

// -------------- FURNITURE --------------
function createFurnitureGO(scene, f) {
  // f shape expected: { id, name, sprite_path, tx, ty, rotation, scale, interactable }
  const s = tileToScreen(f.tx, f.ty);
  const y = s.y - (GLOBAL.tileH/2)*0.2;
  const size = Math.max(20, GLOBAL.tileW*0.6);
  const g = scene.add.graphics();
  g.fillStyle(f.color || 0x78c,1);
  g.fillRoundedRect(-size/2, -size/2, size, size*0.6,6);
  const container = scene.add.container(s.x, y, [g]);
  container.setSize(size, size*0.6);
  container.setInteractive(new Phaser.Geom.Rectangle(-size/2,-size/2,size,size*0.6), Phaser.Geom.Rectangle.Contains);
  container.uid = f.uid || (`dbid_${f.id}`); // prefer provided uid, otherwise DB generated id mapping
  container.protoId = f.proto_id || f.name;

  container.on('pointerdown', () => {
    container.list[0].clear();
    container.list[0].fillStyle(0xffff00,1);
    container.list[0].fillRoundedRect(-size/2,-size/2,size,size*0.6,6);
  });

  scene.input.setDraggable(container);

  container.on('drag', (pointer, dragX, dragY) => {
    container.x = dragX; container.y = dragY;
  });

  container.on('dragend', () => {
    const { tx, ty } = screenToTile(container.x, container.y + GLOBAL.tileH/4);
    if (insideRoom(tx, ty)) {
      const s2 = tileToScreen(tx, ty);
      container.x = s2.x; container.y = s2.y-(GLOBAL.tileH/2)*0.2;
      const ff = currentRoom.furniture.find(x => x.uid===container.uid);
      if (ff) { ff.tx=tx; ff.ty=ty; }
      // persist move via WS JSON type (server broadcasts it)
      sendWS({ type:'UPDATE_FURNITURE', room: currentRoom.name, uid: container.uid, tx, ty });
    } else {
      const ff = currentRoom.furniture.find(x => x.uid===container.uid);
      if (ff) {
        const s0 = tileToScreen(ff.tx, ff.ty);
        container.x=s0.x; container.y=s0.y-(GLOBAL.tileH/2)*0.2;
      }
    }
  });

  scene.furnitureLayer.add(container);
  furnitureGameObjects[container.uid] = container;
  return container;
}

function spawnFurnitureFromUI(proto) {
  const uid = generateUID();
  const defaultTx=Math.floor(currentRoom.cols/2);
  const defaultTy=Math.floor(currentRoom.rows/2);
  const model={ uid, proto_id: proto.id, tx: defaultTx, ty: defaultTy, color: proto.color };
  currentRoom.furniture.push(model);
  createFurnitureGO(sceneRef, model);
  // persist to DB via CREATE_FURNITURE
  sendWS({ type:'CREATE_FURNITURE', room: currentRoom.name, uid: model.uid, proto_id: model.proto_id, tx: model.tx, ty: model.ty, color: model.color });
  log(`Spawned ${proto.id} as ${uid}`);
}
window.spawnFurnitureFromUI = spawnFurnitureFromUI;

// -------------- CAMERA --------------
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
      // payload expected in msg.data as array
      if (Array.isArray(msg.data)) populateRoomButtons(msg.data);
      break;
    case 'ROOM_TEMPLATE':
      // single template - not used directly here
      break;
    case 'ROOM_FURNITURE':
      // request response for furniture: set currentRoom.furniture
      if (Array.isArray(msg.data)) {
        currentRoom.furniture = msg.data.map(f => {
          // unify field naming
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
        // redraw
        if (sceneRef) {
          Object.values(furnitureGameObjects).forEach(go => go.destroy());
          Object.keys(furnitureGameObjects).forEach(k => delete furnitureGameObjects[k]);
          currentRoom.furniture.forEach(f => createFurnitureGO(sceneRef, f));
        }
      }
      break;
    case 'ROOM_STATE':
      // server broadcasts full state for a room
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
        // create a minimal model
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
      // console.log('WS message unknown:', msg);
      break;
  }
}

// export spawn
window.spawnFurnitureFromUI = spawnFurnitureFromUI;

