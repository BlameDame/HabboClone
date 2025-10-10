// Email regex for basic validation
const emailRegex = /^[^\s@]+@[^\s@]+\.[^\s@]+$/;

// Password strength regex: min 8 chars, at least one uppercase, one lowercase, one number
const passwordRegex = /^(?=.*[a-z])(?=.*[A-Z])(?=.*\d).{8,}$/;

const emailInput = document.getElementById('reg-email');
const usernameInput = document.getElementById('reg-username');
const registerMsg = document.getElementById('register-msg');

// Connect to WebSocket server
const ws = new WebSocket('ws://localhost:9001');

ws.onopen = () => console.log('✅ Connected to server');
ws.onclose = () => console.log('Connection closed');
ws.onerror = (err) => console.error('WebSocket error:', err);
ws.onmessage = (msg) => {
    console.log('Message from server:', msg.data);
};

// ----------------- Login -----------------
document.getElementById('login-btn').addEventListener('click', () => {
    const username = document.getElementById('login-username').value.trim();
    const password = document.getElementById('login-password').value;

    if (!username || !password) {
        document.getElementById('login-msg').textContent = 'Please fill all fields';
        return;
    }

    // Optional: basic password validation on login
    // if (!passwordRegex.test(password)) {
    //     document.getElementById('login-msg').textContent = 'Invalid password format';
    //     return;
    // }

    ws.send(`/login ${username} ${password}`);
});

// ----------------- Register -----------------
document.getElementById('register-btn').addEventListener('click', () => {
    const email = document.getElementById('reg-email').value.trim();
    const username = document.getElementById('reg-username').value.trim();
    const password = document.getElementById('reg-password').value;
    const password2 = document.getElementById('reg-password2').value;
    const registerMsg = document.getElementById('register-msg');

    if (!email || !username || !password || !password2) {
        document.getElementById('register-msg').textContent = 'Please fill all fields';
        return;
    }

    if (!emailRegex.test(email)) {
        document.getElementById('register-msg').textContent = 'Invalid email format';
        return;
    }

    if (!passwordRegex.test(password)) {
        document.getElementById('register-msg').textContent =
            'Password must be at least 8 characters, include upper and lower case letters, and a number';
        return;
    }

    if (password !== password2) {
        document.getElementById('register-msg').textContent = 'Passwords do not match';
        return;
    }

    ws.send(`/register ${username} ${email} ${password}`);
});


// Check email availability when user leaves the email field
emailInput.addEventListener('blur', () => {
    const email = emailInput.value.trim();
    if (!email) return;

    ws.send(`/check_email ${email}`);
});
usernameInput.addEventListener('blur', () => {
    const username = usernameInput.value.trim();
    if (!username) return;

    ws.send(`/check_username ${username}`);
});

ws.onmessage = (msg) => {
    const data = msg.data;

    // Show email availability feedback
    if (data.includes('already') || data.includes('available')) {
        registerMsg.textContent = data;
        return;
    }

    if (data.includes('Logged in as') || data.startsWith('REGISTER_SUCCESS')) {
        const parts = data.split(' ');
        const userId = parts[1];
        const username = parts[2];
        // store in localStorage/sessionStorage if needed
        window.location.href = '../game/index.html';
        return;
    }

    // Handle failure messages
    if (data.includes('Invalid') || data.startsWith('REGISTER_FAIL')) {
        const parts = data.split(' ');
        const msgText = parts.slice(1).join(' ');
        alert(msgText); // or show in your UI
        return;
    }

    // Handle other server messages
    console.log('Message from server:', data);
};

