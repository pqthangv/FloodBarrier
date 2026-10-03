const char webpage[] PROGMEM = R"=====(
<!DOCTYPE html>
<html lang="vi">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>Điều Khiển Máy Bơm</title>
  <style>
    body {
      margin: 0; padding: 1rem;
      box-sizing: border-box;
      font-family: 'Segoe UI', Arial, sans-serif;
      background: #e2e8f0;
      display: flex; align-items: center; justify-content: center;
      min-height: 100vh;
    }
    .panel {
      background: #ffffff;
      padding: 2.5rem;
      border-radius: 12px;
      box-shadow: 0 4px 20px rgba(0,0,0,0.12);
      text-align: center;
      width: 100%;
      max-width: 360px;
      box-sizing: border-box;
    }
    .panel h1 {
      margin-bottom: 1.5rem;
      font-size: 2rem;
      color: #1a202c;
      letter-spacing: 0.5px;
    }
    #time-display {
      font-size: 1.5rem;
      margin-bottom: 1.5rem;
      color: #2d3748;
    }
    #limit-msg {
      color: #c53030;
      margin: 0.5rem 0 1rem;
      min-height: 1.2em;
    }
    .input-group {
      margin-bottom: 2rem;
      text-align: left;
    }
    .input-group label {
      font-size: 1rem;
      color: #1a202c;
      margin-bottom: 0.5rem;
      display: block;
    }
    .input-group input {
      width: 100%;
      padding: 0.75rem;
      font-size: 1rem;
      border: 1px solid #cbd5e0;
      border-radius: 6px;
      box-sizing: border-box;
    }
    .btn-group {
      display: flex;
      gap: 1rem;
      justify-content: center;
    }
    .btn {
      padding: 0.85rem 1.5rem;
      font-size: 1rem;
      font-weight: bold;
      border: none;
      border-radius: 8px;
      text-decoration: none;
      color: #ffffff;
      cursor: pointer;
      transition: background 0.2s, transform 0.1s;
      user-select: none;
    }
    .btn:active {
      transform: scale(0.97);
    }
    .btn:disabled {
      opacity: 0.5;
      cursor: not-allowed;
    }
    .btn-toggle {
      background: #3182ce;
    }
    .btn-toggle:hover {
      background: #2b6cb0;
    }
    .btn-reset {
      background: #e53e3e;
    }
    .btn-reset:hover {
      background: #c53030;
    }
    .btn-set {
      background: #38a169;
      flex: 1;
    }
    .btn-set:hover {
      background: #2f855a;
    }
  </style>
</head>

<body>
  <div class="panel">
    <h1>Điều Khiển Máy Bơm</h1>

    <div id="time-display">
      Đã bơm: <strong id="pumped">0</strong> giây
    </div>

    <div id="status-display">
      Trạng thái: <strong id="pump-status">OFF</strong>
    </div>
    <div id="limit-msg"></div>

    <div class="input-group">
      <label for="max">Thời gian tối đa (giây):</label>
      <input type="number" id="max" min="1" max="86400" required oninput="checkMaxInput()">
    </div>

    <div class="btn-group">
      <button type="button" class="btn btn-toggle" onclick="togglePump()">BẬT / TẮT</button>
      <button type="button" class="btn btn-reset" onclick="sendCommand('/reset')">RESET</button>
      <button type="button" class="btn btn-set" id="btn-set" onclick="setMaxTime()">ĐẶT</button>
    </div>
  </div>

  <script>
    let pumpOn = false;
    let maxLoaded = false;

    // s = {"pumpOn":true,"pumpedSeconds":100,"maxRunSeconds":600}
    function render(s) {
      pumpOn = s.pumpOn;
      document.getElementById("pumped").textContent = s.pumpedSeconds;
      document.getElementById("pump-status").textContent = s.pumpOn ? "ON" : "OFF";
      document.getElementById("limit-msg").textContent =
        (!s.pumpOn && s.pumpedSeconds >= s.maxRunSeconds) ? "Đã đạt thời gian tối đa – nhấn RESET để bơm tiếp" : "";

      // Fill the max time box once, so it doesn't overwrite what the user is typing
      if (!maxLoaded) {
        document.getElementById("max").value = s.maxRunSeconds;
        maxLoaded = true;
        checkMaxInput();
      }
    }

    // Every endpoint replies with the current status, so the page updates right away
    function sendCommand(url) {
      return fetch(url)
        .then(r => r.json())
        .then(render)
        .catch(() => {});
    }

    function poll() {
      sendCommand("/status").then(() => setTimeout(poll, 1000));
    }

    function togglePump() {
      sendCommand("/pump?state=" + (pumpOn ? "off" : "on"));
    }

    function setMaxTime() {
      const seconds = document.getElementById("max").value;
      sendCommand("/maxtime?seconds=" + encodeURIComponent(seconds));
    }

    function checkMaxInput() {
      document.getElementById("btn-set").disabled = !document.getElementById("max").checkValidity();
    }

    checkMaxInput();
    poll();
  </script>
</body>
</html>
)=====";
