const DATABASE_URL =
  "https://smart-irrigation-esp32-af35c-default-rtdb.asia-southeast1.firebasedatabase.app";
const DEVICE_ID = "esp32-irrigation-01";
const DEVICE_PATH = `/smart_irrigation/devices/${DEVICE_ID}`;
const POLL_INTERVAL_MS = 700;
const OFFLINE_AFTER_MS = 12000;
const MAX_POINTS = 30;
const REQUEST_TIMEOUT_MS = 6500;
const COMMAND_MAX_RETRIES = 3;
const COMMAND_RETRY_DELAY_MS = 400;
const ERROR_TOAST_COOLDOWN_MS = 8000;

const $ = (id) => document.getElementById(id);
const elements = {
  connectionPill: $("connection-pill"),
  connectionText: $("connection-text"),
  lastUpdate: $("last-update"),
  gardenTitle: $("garden-status-title"),
  gardenCopy: $("garden-status-copy"),
  heroSoil: $("hero-soil"),
  heroWater: $("hero-water"),
  heroTemp: $("hero-temp"),
  pumpState: $("pump-state"),
  pumpReason: $("pump-reason"),
  pumpOrb: $("pump-orb"),
  controlModeLabel: $("control-mode-label"),
  runtimeLimitLabel: $("runtime-limit-label"),
  soilValue: $("soil-value"),
  soilRaw: $("soil-raw"),
  soilMeter: $("soil-meter"),
  soilLabel: $("soil-label"),
  waterValue: $("water-value"),
  waterRaw: $("water-raw"),
  waterMeter: $("water-meter"),
  waterLabel: $("water-label"),
  rainValue: $("rain-value"),
  rainRaw: $("rain-raw"),
  rainMeter: $("rain-meter"),
  rainLabel: $("rain-label"),
  airTemp: $("air-temp"),
  airHumidity: $("air-humidity"),
  soilTemp: $("soil-temp"),
  climateLabel: $("climate-label"),
  autoButton: $("auto-button"),
  manualButton: $("manual-button"),
  manualControl: $("manual-control"),
  pumpButton: $("pump-button"),
  pumpButtonLabel: $("pump-button-label"),
  requestState: $("request-state"),
  controlMessage: $("control-message"),
  safetySummary: $("safety-summary"),
  toast: $("toast"),
  chart: $("trend-chart"),
};

const trend = { soil: [], water: [], rain: [] };
let latestDevice = null;
let requestPending = false;
let toastTimer;
let pollTimer;
let pollInFlight = false;
let pollSequence = 0;
let lastErrorToastAt = 0;

function number(value, fallback = 0) {
  return Number.isFinite(Number(value)) ? Number(value) : fallback;
}

function clamp(value) {
  return Math.min(100, Math.max(0, number(value)));
}

function text(id, value) {
  elements[id].textContent = value;
}

function setMeter(id, value) {
  elements[id].style.width = `${clamp(value)}%`;
}

function setConnection(status, label) {
  elements.connectionPill.className = `connection-pill ${status}`;
  elements.connectionText.textContent = label;
}

function formatTime(timestamp) {
  if (!timestamp) return "--:--:--";
  return new Intl.DateTimeFormat("vi-VN", {
    hour: "2-digit",
    minute: "2-digit",
    second: "2-digit",
  }).format(new Date(timestamp));
}

function translatedReason(reason = "") {
  const labels = {
    "SOIL DRY": "Đất khô, đang bắt đầu tưới",
    PUMPING: "Đang tưới tự động",
    "SOIL OK": "Độ ẩm đất đang phù hợp",
    "SOIL WET": "Đất đã đủ ẩm",
    "SOIL ZERO": "Moisture bằng 0%, bơm bị khóa an toàn",
    "LOW WATER": "Mực nước quá thấp",
    RAINING: "Đang phát hiện mưa",
    "DS18B20 ERROR": "Cảm biến DS18B20 lỗi",
    "MAX RUNTIME": "Đã đạt giới hạn thời gian chạy",
    COOLDOWN: "Bơm đang trong thời gian nghỉ",
    "MANUAL OFF": "Bơm thủ công đang tắt",
    "MANUAL ON": "Bật bơm từ dashboard",
    "MANUAL PUMPING": "Đang bơm theo lệnh dashboard",
    "MANUAL TIMEOUT": "Lệnh thủ công đã đạt giới hạn thời gian",
    "NO SENSOR DATA": "Chưa có dữ liệu cảm biến",
    "STARTUP OFF": "Hệ thống vừa khởi động",
  };
  return labels[reason] || reason || "Chưa có dữ liệu";
}

function statusBadge(value, okLabel, alertLabel, isOk) {
  return isOk ? okLabel : alertLabel;
}

function updateSafety(itemId, ok, value) {
  const item = $(itemId);
  item.classList.toggle("alert", !ok);
  item.querySelector(".check-icon").textContent = ok ? "✓" : "!";
  item.querySelector("b").textContent = value;
}

function updateHero(telemetry, config) {
  const soil = clamp(telemetry?.soil?.percent);
  const water = clamp(telemetry?.water?.percent);
  const rain = clamp(telemetry?.rain?.percent);
  const onThreshold = number(config?.thresholds?.soilPumpOnPercent, 30);
  const minimumWater = number(config?.thresholds?.minimumWaterPercent, 20);
  const rainThreshold = number(config?.thresholds?.rainDetectedPercent, 30);

  if (water <= minimumWater) {
    elements.gardenTitle.textContent = "Cần bổ sung nước cho hệ thống";
    elements.gardenCopy.textContent =
      "Bơm đang được khóa an toàn vì mực nước không đủ để tưới.";
  } else if (rain >= rainThreshold) {
    elements.gardenTitle.textContent = "Khu vườn đang nhận nước mưa";
    elements.gardenCopy.textContent =
      "Hệ thống tạm ngừng tưới để tránh cấp nước dư thừa.";
  } else if (soil > 0 && soil < onThreshold) {
    elements.gardenTitle.textContent = "Đất đang khô, cần tưới nước";
    elements.gardenCopy.textContent =
      "Hệ thống sẽ kích hoạt bơm khi tất cả điều kiện an toàn được đáp ứng.";
  } else {
    elements.gardenTitle.textContent = "Khu vườn đang ở trạng thái tốt";
    elements.gardenCopy.textContent =
      "Độ ẩm đất, nguồn nước và thời tiết đang trong giới hạn vận hành.";
  }
}

function renderDevice(device) {
  latestDevice = device;
  const telemetry = device?.telemetry?.latest || {};
  const state = device?.state || {};
  const control = device?.control || { mode: "auto", manualPumpOn: false };
  const config = device?.config || {};
  const thresholds = config.thresholds || {};
  const testMode = config.testMode || {};
  const soil = telemetry.soil || {};
  const water = telemetry.water || {};
  const rain = telemetry.rain || {};
  const ds18 = telemetry.temperature?.ds18b20 || {};
  const dht = telemetry.temperature?.dht11 || {};
  const humidity = telemetry.humidity?.dht11 || {};
  const pump = telemetry.pump || {};

  const age = Date.now() - number(state.lastSeen);
  const online = state.online === true && age < OFFLINE_AFTER_MS;
  setConnection(online ? "online" : "offline", online ? "ESP32 Online" : "ESP32 Offline");
  elements.lastUpdate.textContent = formatTime(state.lastSeen || telemetry.timestamp);

  text("heroSoil", Math.round(clamp(soil.percent)));
  text("heroWater", Math.round(clamp(water.percent)));
  text("heroTemp", dht.valid ? number(dht.celsius).toFixed(1) : "--");

  text("soilValue", Math.round(clamp(soil.percent)));
  text("soilRaw", soil.raw ?? "--");
  setMeter("soilMeter", soil.percent);
  text(
    "soilLabel",
    statusBadge(
      soil.percent,
      "ĐỦ ẨM",
      "ĐẤT KHÔ",
      number(soil.percent) >= number(thresholds.soilPumpOnPercent, 30),
    ),
  );

  text("waterValue", Math.round(clamp(water.percent)));
  text("waterRaw", water.raw ?? "--");
  setMeter("waterMeter", water.percent);
  text(
    "waterLabel",
    statusBadge(
      water.percent,
      "SẴN SÀNG",
      "MỰC THẤP",
      number(water.percent) > number(thresholds.minimumWaterPercent, 20),
    ),
  );

  text("rainValue", Math.round(clamp(rain.percent)));
  text("rainRaw", rain.raw ?? "--");
  setMeter("rainMeter", rain.percent);
  text(
    "rainLabel",
    statusBadge(
      rain.percent,
      "KHÔ RÁO",
      "ĐANG MƯA",
      number(rain.percent) < number(thresholds.rainDetectedPercent, 30),
    ),
  );

  text("airTemp", dht.valid ? number(dht.celsius).toFixed(1) : "--");
  text("airHumidity", humidity.valid ? `${number(humidity.percent).toFixed(1)}%` : "--");
  text("soilTemp", ds18.valid ? `${number(ds18.celsius).toFixed(1)}°C` : "--");
  text("climateLabel", dht.valid ? "HOẠT ĐỘNG" : "LỖI CẢM BIẾN");

  const pumpOn = state.pumpOn === true || pump.on === true;
  // Prefer control.* (command source of truth) to avoid flicker while state.* lags.
  const activeControlMode = control.mode || state.controlMode || "auto";
  elements.pumpState.textContent = pumpOn ? "Đang hoạt động" : "Đang tắt";
  elements.pumpReason.textContent = translatedReason(state.pumpReason || pump.reason);
  elements.pumpOrb.classList.toggle("active", pumpOn);
  elements.controlModeLabel.textContent = `Chế độ ${activeControlMode.toUpperCase()}`;
  elements.runtimeLimitLabel.textContent =
    testMode.pumpRuntimeLimitEnabled === false
      ? "TEST: không giới hạn thời gian"
      : `Tối đa ${Math.round(number(thresholds.maximumPumpRuntimeMs, 30000) / 1000)} giây/lần`;

  const manual = activeControlMode === "manual";
  const manualRequested =
    control.manualPumpOn === true || state.manualPumpRequested === true;
  const manualOverrideActive = manual && manualRequested;
  elements.autoButton.classList.toggle("active", !manual);
  elements.manualButton.classList.toggle("active", manual);
  elements.manualControl.classList.toggle("disabled", !manual);
  elements.pumpButton.disabled = !manual || requestPending;
  elements.pumpButton.classList.toggle("stop", manualRequested);
  elements.pumpButtonLabel.textContent = manualRequested ? "Tắt yêu cầu" : "Bật bơm";
  elements.requestState.textContent = manualRequested ? "Đang yêu cầu bật bơm" : "Không có lệnh chờ";

  // Show an explicit override badge when manual request is active.
  const overrideLabel = $("override-label");
  if (manualOverrideActive) {
    const forcedSoil = testMode.soilPercent ?? 10;
    overrideLabel.textContent = `FORCE SOIL ${Math.round(number(forcedSoil))}% · RUNTIME OFF`;
    overrideLabel.style.display = "inline-block";
  } else {
    overrideLabel.textContent = "";
    overrideLabel.style.display = "none";
  }

  if (state.pumpReason === "MANUAL TIMEOUT" && manualRequested) {
    elements.controlMessage.textContent =
      "Bơm đã đạt giới hạn thời gian. Hãy tắt yêu cầu trước khi bật lại.";
  } else if (testMode.pumpRuntimeLimitEnabled === false) {
    elements.controlMessage.textContent =
      "CẢNH BÁO TEST: độ ẩm đất cố định 10% và bơm không tự tắt theo thời gian.";
  } else if (!manual) {
    elements.controlMessage.textContent =
      "Hệ thống tự quyết định bật/tắt bơm dựa trên dữ liệu cảm biến.";
  } else {
    elements.controlMessage.textContent =
      "Manual không bỏ qua bảo vệ mực nước, mưa và lỗi cảm biến.";
  }

  const waterSafe = number(water.percent) > number(thresholds.minimumWaterPercent, 20);
  const rainSafe = number(rain.percent) < number(thresholds.rainDetectedPercent, 30);
  const sensorSafe = ds18.valid === true;
  updateSafety("safety-water", waterSafe, `${Math.round(clamp(water.percent))}%`);
  updateSafety("safety-rain", rainSafe, `${Math.round(clamp(rain.percent))}%`);
  updateSafety("safety-sensor", sensorSafe, sensorSafe ? "Hợp lệ" : "Lỗi");
  const safeCount = [waterSafe, rainSafe, sensorSafe].filter(Boolean).length;
  elements.safetySummary.textContent = `${safeCount}/3 an toàn`;

  updateHero(telemetry, config);
  pushTrend(soil.percent, water.percent, rain.percent);
}

function pushTrend(soil, water, rain) {
  trend.soil.push(clamp(soil));
  trend.water.push(clamp(water));
  trend.rain.push(clamp(rain));
  for (const series of Object.values(trend)) {
    if (series.length > MAX_POINTS) series.shift();
  }
  drawChart();
}

function drawChart() {
  const canvas = elements.chart;
  const rect = canvas.getBoundingClientRect();
  const dpr = window.devicePixelRatio || 1;
  canvas.width = Math.max(1, rect.width * dpr);
  canvas.height = Math.max(1, rect.height * dpr);
  const ctx = canvas.getContext("2d");
  ctx.scale(dpr, dpr);

  const width = rect.width;
  const height = rect.height;
  const pad = { left: 34, right: 10, top: 10, bottom: 22 };
  const chartWidth = width - pad.left - pad.right;
  const chartHeight = height - pad.top - pad.bottom;

  ctx.clearRect(0, 0, width, height);
  ctx.font = "10px Manrope";
  ctx.fillStyle = "#87958d";
  ctx.strokeStyle = "#e5ece7";
  ctx.lineWidth = 1;

  [0, 25, 50, 75, 100].forEach((value) => {
    const y = pad.top + chartHeight - (value / 100) * chartHeight;
    ctx.beginPath();
    ctx.moveTo(pad.left, y);
    ctx.lineTo(width - pad.right, y);
    ctx.stroke();
    ctx.fillText(`${value}`, 5, y + 3);
  });

  const drawSeries = (points, color) => {
    if (points.length < 2) return;
    ctx.beginPath();
    points.forEach((value, index) => {
      const x = pad.left + (index / (MAX_POINTS - 1)) * chartWidth;
      const y = pad.top + chartHeight - (value / 100) * chartHeight;
      if (index === 0) ctx.moveTo(x, y);
      else ctx.lineTo(x, y);
    });
    ctx.strokeStyle = color;
    ctx.lineWidth = 2.2;
    ctx.lineCap = "round";
    ctx.lineJoin = "round";
    ctx.stroke();
  };

  drawSeries(trend.soil, "#16845b");
  drawSeries(trend.water, "#2687a7");
  drawSeries(trend.rain, "#7b83af");
}

async function firebaseRequest(path, options = {}) {
  const { timeoutMs = REQUEST_TIMEOUT_MS, ...fetchOptions } = options;
  const controller = new AbortController();
  const timeoutId = setTimeout(() => controller.abort(), timeoutMs);
  const isRead = !fetchOptions.method || fetchOptions.method === "GET";
  const cacheBuster = isRead ? `?t=${Date.now()}` : "";

  try {
    const response = await fetch(`${DATABASE_URL}${path}.json${cacheBuster}`, {
      headers: { "Content-Type": "application/json" },
      cache: "no-store",
      signal: controller.signal,
      ...fetchOptions,
    });
    if (!response.ok) {
      throw new Error(`Firebase HTTP ${response.status}`);
    }
    return response.json();
  } catch (error) {
    if (error.name === "AbortError") {
      throw new Error("Request timeout");
    }
    throw error;
  } finally {
    clearTimeout(timeoutId);
  }
}

function delay(ms) {
  return new Promise((resolve) => setTimeout(resolve, ms));
}

function cloneDevice(device) {
  return device ? JSON.parse(JSON.stringify(device)) : null;
}

function applyOptimisticControlUpdate(values) {
  if (!latestDevice) {
    return;
  }

  const device = cloneDevice(latestDevice);
  device.control = {
    ...(device.control || {}),
    ...values,
  };
  device.state = {
    ...(device.state || {}),
    controlMode: values.mode ?? device.state?.controlMode,
    manualPumpRequested:
      values.manualPumpOn ?? device.state?.manualPumpRequested,
    updatedAt: Date.now(),
  };

  if (values.mode === "auto") {
    device.state.manualPumpRequested = false;
    device.state.pumpReason = device.state.pumpReason || "AUTO";
  }

  latestDevice = device;
  renderDevice(device);
}

function scheduleNextPoll(delayMs = POLL_INTERVAL_MS) {
  clearTimeout(pollTimer);
  pollTimer = setTimeout(pollDevice, delayMs);
}

async function pollDevice() {
  if (pollInFlight) {
    return;
  }

  pollInFlight = true;
  const requestId = ++pollSequence;

  try {
    const device = await firebaseRequest(DEVICE_PATH);
    if (!device) throw new Error("Không tìm thấy thiết bị trong Firebase");
    if (requestId === pollSequence) {
      renderDevice(device);
    }
    scheduleNextPoll(POLL_INTERVAL_MS);
  } catch (error) {
    setConnection("offline", "Mất kết nối Firebase");
    const now = Date.now();
    if (now - lastErrorToastAt >= ERROR_TOAST_COOLDOWN_MS) {
      lastErrorToastAt = now;
      showToast(error.message);
    }
    scheduleNextPoll(Math.min(POLL_INTERVAL_MS * 3, 5000));
  } finally {
    pollInFlight = false;
  }
}

async function updateControl(values, successMessage) {
  if (requestPending) return;
  requestPending = true;
  elements.pumpButton.disabled = true;

  let lastError = null;

  try {
    for (let attempt = 1; attempt <= COMMAND_MAX_RETRIES; attempt += 1) {
      try {
        await firebaseRequest(`${DEVICE_PATH}/control`, {
          method: "PATCH",
          body: JSON.stringify({
            ...values,
            updatedAt: Date.now(),
            updatedBy: "web-dashboard",
          }),
        });

        applyOptimisticControlUpdate(values);
        showToast(successMessage);
        scheduleNextPoll(0);
        return;
      } catch (error) {
        lastError = error;
      }

      if (attempt < COMMAND_MAX_RETRIES) {
        await delay(COMMAND_RETRY_DELAY_MS * attempt);
      }
    }

    throw lastError || new Error("Không xác định được lỗi gửi lệnh");
  } catch (error) {
    showToast(`Không thể gửi lệnh: ${error.message || error}`);
  } finally {
    requestPending = false;
    if (latestDevice) renderDevice(latestDevice);
  }
}

function showToast(message) {
  clearTimeout(toastTimer);
  elements.toast.textContent = message;
  elements.toast.classList.add("show");
  toastTimer = setTimeout(() => elements.toast.classList.remove("show"), 3200);
}

elements.autoButton.addEventListener("click", () =>
  updateControl(
    { mode: "auto", manualPumpOn: false },
    "Đã chuyển sang chế độ AUTO",
  ),
);

elements.manualButton.addEventListener("click", () =>
  updateControl(
    { mode: "manual", manualPumpOn: false },
    "Đã chuyển sang chế độ MANUAL",
  ),
);

elements.pumpButton.addEventListener("click", () => {
  const requested = latestDevice?.control?.manualPumpOn === true;
  updateControl(
    { mode: "manual", manualPumpOn: !requested },
    requested ? "Đã gửi lệnh tắt bơm" : "Đã gửi lệnh bật bơm",
  );
});

window.addEventListener("resize", drawChart);
scheduleNextPoll(0);
