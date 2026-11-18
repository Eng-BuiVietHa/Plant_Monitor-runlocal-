document.addEventListener("DOMContentLoaded", () => {
  // --- Trạng thái hệ thống ---
  const state = {
    mode: "off", // 'auto' | 'cool' | 'off'
    timer: {
      startHour: 6,
      startMin: 30,
      endHour: 7,
      endMin: 0,
      enabled: true
    }
  };

  // --- Phần tử DOM ---
  const dateEl = document.getElementById("current-date");
  const timeEl = document.getElementById("current-time");
  const badgeEl = document.getElementById("connection-badge");
  const connTextEl = document.getElementById("connection-text");
  const tempEl = document.getElementById("temp-value");
  const humiEl = document.getElementById("humi-value");

  const modeButtons = document.querySelectorAll(".ctrl-btn[data-mode]");
  const btnTimerOpen = document.getElementById("btn-timer");
  const timerModal = document.getElementById("timer-modal");
  const btnCloseModal = document.getElementById("btn-close-modal");
  const btnCancelTimer = document.getElementById("btn-cancel-timer");
  const btnSaveTimer = document.getElementById("btn-save-timer");
  const timerEnabledInput = document.getElementById("timer-enabled");

  const pad2 = (num) => String(num).padStart(2, "0");

  // --- 1. Cập nhật Ngày & Giờ trên điện thoại ---
  function updateLocalClock() {
    const now = new Date();
    dateEl.textContent = `${pad2(now.getDate())}/${pad2(now.getMonth() + 1)}/${now.getFullYear()}`;
    timeEl.textContent = `${pad2(now.getHours())} : ${pad2(now.getMinutes())} : ${pad2(now.getSeconds())}`;
  }
  updateLocalClock();
  setInterval(updateLocalClock, 1000);

  // --- 2. Hiển thị giao diện hẹn giờ ---
  function renderTimerInputs() {
    document.getElementById("start-hour").textContent = pad2(state.timer.startHour);
    document.getElementById("start-min").textContent = pad2(state.timer.startMin);
    document.getElementById("end-hour").textContent = pad2(state.timer.endHour);
    document.getElementById("end-min").textContent = pad2(state.timer.endMin);
    timerEnabledInput.checked = state.timer.enabled;
  }

  // --- 3. Xử lý nút tăng/giảm giờ phút ---
  document.querySelectorAll(".spinner-box").forEach((box) => {
    const field = box.dataset.field;
    const isHour = field.includes("Hour");
    const maxVal = isHour ? 23 : 59;

    const setActiveBox = () => {
      document.querySelectorAll(".spinner-box").forEach((b) => b.classList.remove("active"));
      box.classList.add("active");
    };

    box.querySelector(".up").addEventListener("click", () => {
      setActiveBox();
      state.timer[field] = (state.timer[field] + 1) > maxVal ? 0 : state.timer[field] + 1;
      renderTimerInputs();
    });

    box.querySelector(".down").addEventListener("click", () => {
      setActiveBox();
      state.timer[field] = (state.timer[field] - 1) < 0 ? maxVal : state.timer[field] - 1;
      renderTimerInputs();
    });
  });

  // --- 4. Đóng / Mở Modal Hẹn Giờ ---
  btnTimerOpen.addEventListener("click", () => {
    renderTimerInputs();
    timerModal.classList.remove("hidden");
  });

  const closeModal = () => timerModal.classList.add("hidden");
  btnCloseModal.addEventListener("click", closeModal);
  btnCancelTimer.addEventListener("click", closeModal);
  timerModal.addEventListener("click", (e) => {
    if (e.target === timerModal) closeModal();
  });

  // --- 5. Lưu cấu hình Hẹn Giờ gửi xuống ESP32 ---
  btnSaveTimer.addEventListener("click", async () => {
    state.timer.enabled = timerEnabledInput.checked;
    closeModal();

    try {
      await fetch("/api/timer", {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify(state.timer)
      });
    } catch (err) {
      console.warn("Chưa kết nối tới ESP32:", err);
    }
  });

  // --- 6. Chọn chế độ Tự động / Làm mát / Tắt ---
  function updateActiveModeUI(mode) {
    state.mode = mode;
    modeButtons.forEach((btn) => {
      btn.classList.toggle("active", btn.dataset.mode === mode);
    });
  }

  modeButtons.forEach((btn) => {
    btn.addEventListener("click", async () => {
      const selectedMode = btn.dataset.mode;
      updateActiveModeUI(selectedMode);

      try {
        await fetch(`/api/mode?set=${selectedMode}`, { method: "POST" });
      } catch (err) {
        console.warn("Chưa kết nối tới ESP32:", err);
      }
    });
  });

  // --- 7. Đồng bộ 2 chiều với main.cpp mỗi 2 giây ---
  async function fetchSensorStatus() {
    try {
      const now = new Date();
      // Gửi kèm giờ/phút hiện tại của điện thoại để ESP32 cập nhật đồng hồ chạy hẹn giờ
      const response = await fetch(`/api/status?h=${now.getHours()}&m=${now.getMinutes()}&s=${now.getSeconds()}`);
      if (!response.ok) throw new Error("HTTP error");
      const data = await response.json();

      // 1) Cập nhật nhãn kết nối
      badgeEl.classList.remove("disconnected");
      badgeEl.classList.add("connected");
      connTextEl.textContent = "Đã kết nối";

      // 2) Cập nhật nhiệt độ & độ ẩm từ DHT11
      if (data.temp !== undefined && data.temp !== null) {
        tempEl.textContent = Number(data.temp).toFixed(1);
      }
      if (data.humi !== undefined && data.humi !== null) {
        humiEl.textContent = Math.round(Number(data.humi));
      }

      // 3) Cập nhật viền nút chế độ đang chạy trên ESP32
      if (data.mode) {
        updateActiveModeUI(data.mode);
      }

      // 4) Cập nhật cấu hình hẹn giờ đã lưu trên ESP32 (chỉ cập nhật khi người dùng đang đóng popup)
      if (data.timer && timerModal.classList.contains("hidden")) {
        state.timer = { ...state.timer, ...data.timer };
      }
    } catch (err) {
      badgeEl.classList.remove("connected");
      badgeEl.classList.add("disconnected");
      connTextEl.textContent = "Mất kết nối";
    }
  }

  fetchSensorStatus();
  setInterval(fetchSensorStatus, 2000);
});