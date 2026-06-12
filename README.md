# Smart Irrigation ESP32 + Firebase RTDB

Firmware doc cam bien dat, muc nuoc, mua, DS18B20 va DHT11; tu dong dieu
khien bom; hien thi OLED; dong bo du lieu len Firebase Realtime Database.

## Cau truc du lieu

- `smart_irrigation/devices/esp32-irrigation-01/config`: calibration va nguong.
- `smart_irrigation/devices/esp32-irrigation-01/state`: trang thai thiet bi/bom.
- `smart_irrigation/devices/esp32-irrigation-01/telemetry/latest`: mau moi nhat.
- `smart_irrigation/devices/esp32-irrigation-01/telemetry/history`: lich su theo
  Firebase push ID.

Firmware ghi `latest` va `state` moi 2 giay, `history` moi 60 giay. Dieu khien bom van
hoat dong cuc bo khi mat Wi-Fi hoac Firebase.

Bom bat khi do am dat duoi 30%, co du nuoc, khong mua va cam bien DS18B20 hop
le. Bom tat khi dat tren 45%, thieu nuoc, co mua, loi cam bien hoac da chay du
30 giay. Sau khi tat, bom nghi it nhat 5 giay truoc khi co the bat lai.

## Cau hinh Firebase

1. Tao Firebase Realtime Database.
2. Dat rules `.read` va `.write` thanh `true` de cho phep ket noi public.
3. Dien `WIFI_SSID` va `WIFI_PASSWORD` trong `include/secrets.h`.
4. Firmware da duoc cau hinh voi database URL:
   `https://smart-irrigation-esp32-af35c-default-rtdb.asia-southeast1.firebasedatabase.app`.
5. Trong Realtime Database, chon `Import JSON` va upload `data.json`.
6. Xoa node mau
   `telemetry/history/sample_remove_after_import` sau khi import.
7. Publish noi dung `database.rules.json` trong tab `Rules`.

Rules public cho phep bat ky ai biet URL doc, sua hoac xoa du lieu. Chi nen dung
de test; he thong production nen bat Firebase Authentication va rules rieng cho
tung thiet bi.

## Build va upload

Du an dung PlatformIO:

```powershell
pio run
pio run --target upload
pio device monitor
```

Neu relay cua ban la active LOW, doi
`AppConfig::RELAY_ACTIVE_LOW` thanh `true` trong `include/app_config.h`.

Can hieu chinh cac gia tri raw trong `include/app_config.h` theo cam bien thuc
te truoc khi cho bom chay.

## Dashboard web

Dashboard tinh nam trong thu muc `web/`. Co the chay truc tiep bang mot static
server hoac deploy len Firebase Hosting:

```powershell
npx firebase-tools deploy --only hosting
```

Dashboard doc du lieu moi 2 giay va ghi len node `control`:

- `mode: "auto"`: ESP32 dieu khien bom theo cam bien.
- `mode: "manual"` va `manualPumpOn: true`: yeu cau bat bom thu cong.
- Lenh manual van bi chan neu thieu nuoc, dang mua hoac DS18B20 loi.
- Sau khi manual chay du 30 giay, can tat yeu cau roi bat lai.

## Che do test day noi pump tam thoi

Trang thai che do test trong `include/app_config.h`:

- `FORCE_SOIL_PERCENT_FOR_TEST = false`: dung soil percent thuc te.
- `ENABLE_PUMP_RUNTIME_LIMIT = true`: pump tu tat sau 30 giay.
- Pump luon tat khi soil percent bang 0%.
- Bao ve thieu nuoc, mua va loi DS18B20 van hoat dong.

Sau khi test xong, dat `FORCE_SOIL_PERCENT_FOR_TEST = false` va
`ENABLE_PUMP_RUNTIME_LIMIT = true` de khoi phuc van hanh an toan.
