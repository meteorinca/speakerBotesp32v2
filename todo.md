
# 1. What you are building

## On the ESP32-S3

One firmware that does all of this:

* connects to Wi-Fi as a station
* exposes a tiny HTTP API for:

  * connect/status
  * servo angle
  * OLED text/status
  * start/stop stream
* receives **Opus audio packets over UDP**
* decodes Opus to PCM
* plays PCM to the speaker through I2S
* reads mic PCM from I2S
* optionally encodes mic audio to Opus and sends it back to the PC
* drives a servo with PWM
* updates the OLED over I2C

## On the PC

A Python app with Flask that:

* serves a local webpage
* lets you enter the ESP IP
* lets you upload / select local MP3s
* uses `ffmpeg` to decode MP3 to PCM
* encodes PCM to Opus frames
* streams Opus packets to the ESP over UDP
* can also receive mic audio back from the ESP if you want two-way audio later

---

# 2. The right architecture

## Control plane

Use **HTTP** on the ESP for:

* `/status`
* `/servo`
* `/oled`
* `/stream/start`
* `/stream/stop`

ESP-IDF’s HTTP server supports normal handlers, persistent sockets, and WebSocket support, but the WebSocket support is limited enough that I would not use it as the main audio transport. The docs explicitly note WebSocket support exists, but the example README says it is “very limited” and needs special care. ([Espressif Systems][1])

## Audio data plane

Use **UDP** for Opus packets.

Why UDP:

* lower overhead
* no head-of-line blocking
* easy one-packet-per-frame design
* good fit for live audio

Why not HTTP multipart:

* too heavy
* too much parsing
* bad for real-time audio

Why not raw flash chunk files:

* file I/O and erase/write latency will cause dropouts
* NVS is meant for key-value storage and works best for many small values, not large blobs; Espressif explicitly recommends a filesystem for large blobs/strings instead. ([Espressif Systems][2])

---

# 3. Why Opus is the right codec here

Opus is specifically meant for low-delay interactive audio and streaming. The Opus project describes it as a royalty-free codec intended for interactive speech and music transmission. ([opus-codec.org][3])

Opus frame sizes are standardized at **2.5, 5, 10, 20, 40, or 60 ms**, and packets can combine frames up to 120 ms. For real-time audio, **20 ms** is the usual sweet spot. The Opus RFC and FFmpeg codec docs both list those frame sizes, and FFmpeg notes that 20 ms is the default. ([RFC Editor][4])

So for your first good version, use:

* **mono**
* **48 kHz Opus encoder input**
* **20 ms frames**
* **24 to 32 kbps** for voice
* **40 to 64 kbps** for better music-ish playback

---

# 4. ESP-IDF pieces you need

## Wi-Fi

ESP-IDF Wi-Fi supports STA, AP, and STA/AP. The Wi-Fi driver docs and the station example are the right starting points for joining your local network as a client. ([Espressif Systems][5])

## I2S

ESP32-S3 has **two I2S peripherals**, with separate RX and TX channels and DMA support. That is exactly what you want for mic-in and speaker-out running at the same time. ([Espressif Systems][6])

## Ring buffers

ESP-IDF provides ring buffers that are FIFO, accept arbitrary-sized items, and are more suitable than vanilla queues for variable-sized audio packets. ([Espressif Systems][7])

## HTTP server

Use `esp_http_server` for the control API. It already supports persistent connections and has working examples for standard handlers and WebSockets. ([Espressif Systems][1])

## Servo

The Espressif servo component in ESP-IoT-Solution uses **LEDC** and is meant for standard RC-style servo control. The servo docs call out the common 50 Hz, 20 ms period, and roughly 0.5 to 2.5 ms pulse range for a 180° servo. ([Espressif Systems][8])

## OLED

For the SSD1306:

* you can use ESP-IDF’s I2C master driver
* for the panel side, ESP-IDF includes SSD1306 panel support in `esp_lcd`, and the registry also points developers toward the updated IDF driver rather than the older deprecated component. ([Espressif Systems][9])

## Opus on ESP-IDF

You have two practical choices:

### Choice A, best for you

Use **`espressif/esp_audio_codec`** through the component registry. Its docs say it supports **Opus encode and decode**, and that the decoder expects **frame-boundary input**. ([components.espressif.com][10])

### Choice B

Use **ESP-ADF** helpers if you want a more audio-pipeline-heavy framework. ADF has an Opus decoder element, but for your project I would not pull in all of ADF unless you really want its pipeline model. ([Espressif Systems][11])

For your case, I would use **plain ESP-IDF + `esp_audio_codec`**.

---

# 5. Hardware layout

Using your pin set:

## Mic I2S RX

* `MIC_SCK_PIN = 5`
* `MIC_WS_PIN = 6`
* `MIC_SD_PIN = 7`

## Speaker I2S TX to MAX98357A

* `SPK_BCLK_PIN = 15`
* `SPK_LRC_PIN = 16`
* `SPK_DIN_PIN = 17`

## OLED I2C

* `OLED_SCL_PIN = 42`
* `OLED_SDA_PIN = 41`

## Servo

* `SERVO_PIN = 18`

## LED

* `LED_PIN = 48`

Notes:

* MAX98357A usually does not need MCLK, just BCLK, LRCLK, and DIN.
* SSD1306 I2C lines need pull-ups.
* Servo power should not come straight from a weak board rail if the servo is anything beyond tiny. Use a proper 5 V rail and common ground.

---

# 6. Recommended runtime design on the ESP

Create these tasks:

## Task 1: control task

* starts Wi-Fi
* starts HTTP server
* handles `/status`, `/servo`, `/oled`, `/stream/start`, `/stream/stop`

## Task 2: UDP receive task

* listens on `UDP_AUDIO_PORT`
* each UDP packet contains one Opus frame plus a tiny custom header
* pushes packet payload into an **Opus RX ring buffer**

## Task 3: Opus decode task

* pops one Opus frame at a time from the RX ring buffer
* decodes to PCM using `esp_audio_codec`
* pushes PCM blocks into a **PCM playback ring buffer**

## Task 4: I2S TX task

* pulls PCM from playback ring buffer
* writes it to speaker I2S DMA

## Task 5: I2S RX / mic task

* reads PCM from mic I2S
* either:

  * stores for VU meter / local processing, or
  * sends to an encoder task

## Task 6: Opus encode task, optional

* collects 20 ms PCM mic frames
* encodes to Opus
* sends UDP packets back to the PC

## Task 7: OLED UI task

* updates status once every 200 to 500 ms
* keep it low priority

Servo control does not need its own task. It can be set from the HTTP handler or a command queue.

---

# 7. The packet format

Keep it simple.

Each UDP packet should contain:

```text
[magic 2 bytes]
[version 1 byte]
[type 1 byte]
[seq 4 bytes]
[timestamp_ms 4 bytes]
[payload_len 2 bytes]
[payload ...]
```

Where:

* `type = 1` for Opus audio
* payload is exactly one Opus packet

Why this is good:

* easy loss detection via `seq`
* easy stats on OLED
* easy debugging in Wireshark if needed

Do not send Ogg pages to the ESP.
Send **raw Opus frames**, one frame per UDP packet.

That matters because `esp_audio_codec` says the decoder operates on **audio frame data**, meaning frame-boundary input. ([components.espressif.com][10])

---

# 8. Recommended audio settings

## For first stable playback

Use:

* Opus input/output sample rate basis: **48 kHz**
* channels: **mono**
* frame duration: **20 ms**
* bitrate: **32000 bps**
* complexity: medium
* packetization: one Opus frame per UDP packet

Why 48 kHz:
Opus is naturally built around 48 kHz internally, and 20 ms is the usual default frame duration. The RFC and FFmpeg docs support the frame-duration choice. ([RFC Editor][4])

If CPU gets tight:

* try 16 or 24 kbps
* keep 20 ms
* keep mono

If latency matters more:

* try 10 ms frames
* expect more packets/sec and more overhead

---

# 9. ESP-IDF project structure

I would organize it like this:

```text
esp32s3_audio_bot/
  CMakeLists.txt
  sdkconfig.defaults
  main/
    CMakeLists.txt
    app_main.c
    wifi_sta.c
    http_control.c
    audio_i2s.c
    audio_udp.c
    audio_opus.c
    audio_pipeline.c
    oled_ui.c
    servo_ctrl.c
    common.h
```

And add dependencies with Component Manager:

```bash
idf.py add-dependency "espressif/esp_audio_codec^2.0.3"
```

For the OLED, either use the built-in `esp_lcd` SSD1306 path or pull a small driver component. The component registry has SSD1306 drivers, and Espressif’s older separate SSD1306 component is marked deprecated in favor of the updated driver in ESP-IDF. ([components.espressif.com][12])

---

# 10. ESP setup steps

## A. Wi-Fi station mode

Start from the station example structure:

* init NVS
* init netif
* create default event loop
* init Wi-Fi
* register event handlers
* set STA config
* connect
* wait for `IP_EVENT_STA_GOT_IP` ([Espressif Systems][5])

## B. I2S setup

Use one I2S controller for RX and another for TX, or separate RX/TX channels cleanly.

Because ESP32-S3 has two I2S peripherals and separate TX/RX channels with DMA, you are in good shape for full duplex audio. ([Espressif Systems][6])

Initial settings:

* mic RX: 16 kHz or 48 kHz mono 16-bit
* speaker TX: 48 kHz mono 16-bit

If your mic is fixed at 16 kHz, that is fine. You can:

* keep mic path 16 kHz for voice uplink
* keep playback 48 kHz for incoming Opus decode if needed
* resample only if your chosen decoder/output path requires it

## C. Servo

Use LEDC-based servo control.

* 50 Hz
* min pulse and max pulse mapped to your actual servo
* start at 90° ([Espressif Systems][8])

## D. OLED

Use I2C master driver and SSD1306 panel init.
Display:

* IP address
* Wi-Fi RSSI
* stream state
* packet loss count
* underruns
* servo angle

## E. Ring buffers

Create:

* `opus_rx_rb`
* `pcm_play_rb`
* `mic_pcm_rb`, optional

ESP-IDF ring buffers are made for arbitrary-sized FIFO items and are a better fit than plain queues here. ([Espressif Systems][7])

---

# 11. The Flask webapp design

Your Python app should have two parts:

## Frontend, simple local webpage

Fields:

* ESP IP
* connect button
* MP3 upload
* bitrate dropdown
* frame size dropdown, default 20 ms
* play
* stop
* servo slider
* OLED text box
* stats panel

## Backend, Flask

Routes:

* `GET /` serve page
* `POST /connect`
* `POST /play`
* `POST /stop`
* `POST /servo`
* `POST /oled`

The Flask backend does the real work:

* receives uploaded MP3
* spawns ffmpeg to decode to PCM
* packetizes PCM into 20 ms frames
* encodes to Opus
* sends UDP packets to ESP

---

# 12. Python audio path

There are two sensible ways.

## Path A, easier to reason about

`ffmpeg` decodes MP3 to raw PCM, and Python encodes PCM to Opus with a library such as `opuslib` or another libopus binding.

Flow:

```text
MP3 file
  -> ffmpeg decode to raw s16le mono PCM at 48k
  -> Python reads 20 ms PCM chunks
  -> Python Opus encoder
  -> UDP packets to ESP
```

This gives you exact frame control.

## Path B, less Python work but clunkier

Use ffmpeg to output an Opus stream and then try to parse/frame it before sending.

I would not do that first.

For your build, use **Path A**.

---

# 13. Python packetizer numbers

At **48 kHz**, **20 ms** equals:

* 0.02 × 48000 = **960 samples**

At **16-bit mono**, that is:

* 960 × 2 = **1920 bytes PCM per frame**

So your Python sender loop should:

* read **1920 bytes**
* encode one Opus frame
* send one UDP packet

That one design choice removes a lot of timing weirdness.

---

# 14. ESP Opus decode strategy

Use the Opus decoder from `esp_audio_codec`.

Because that decoder expects frame-boundary input, your UDP packet already being one frame is perfect. ([components.espressif.com][10])

Pseudo-flow:

```c
udp_rx_task:
  recvfrom(...)
  verify header
  push payload into opus_rx_rb

opus_decode_task:
  pop opus packet from opus_rx_rb
  decode to pcm buffer
  push pcm buffer to pcm_play_rb

i2s_tx_task:
  pop pcm from pcm_play_rb
  i2s_channel_write(...)
```

If `pcm_play_rb` gets too empty:

* count underrun
* output short silence frame
* do not block forever

That is how you avoid speaker pops and random cutoffs.

---

# 15. Uplink mic audio, optional but nice

Once playback works, add mic uplink.

Mic path:

```text
I2S RX -> mic frame buffer -> Opus encoder -> UDP send to PC
```

For voice uplink, 16 kHz mono Opus is fine.
For speaker playback from MP3/music, 48 kHz decode/playback is nicer.

So it is okay if:

* **mic uplink = 16 kHz Opus**
* **downlink playback = 48 kHz Opus**

They do not have to be identical.

---

# 16. Why this is better than your MicroPython version

Because it avoids all the bad parts:

* no constant flash writes
* no HTTP multipart parsing in the hot path
* no interpreted-language timing jitter in the audio path
* uses DMA-backed I2S
* uses ring buffers between tasks
* uses a codec that was actually made for live audio

Also, NVS is the wrong place for audio. Espressif’s NVS docs explicitly say it is designed for key-value pairs and works best for many small values, not large blobs. ([Espressif Systems][2])

---

# 17. What I would build first

## Phase 1

No Opus yet. Just prove your ESP-IDF I2S + Wi-Fi + ring buffer path.

* HTTP control works
* OLED works
* servo works
* UDP PCM stream works
* speaker playback stable for 10+ minutes

## Phase 2

Add Opus **downlink only**.

* Flask decodes MP3 -> PCM -> Opus
* ESP receives Opus -> decodes -> plays

## Phase 3

Add mic **uplink**.

* ESP mic PCM -> Opus -> PC
* local webpage lets you hear / save it

## Phase 4

Add polish:

* packet loss stats
* jitter buffer
* mute
* volume
* reconnect
* autoplay after reconnect

That sequence will save you a lot of pain.

---

# 18. Jitter buffer rule of thumb

On the ESP side, before starting playback, buffer about:

* **3 to 5 Opus packets** at 20 ms each

That gives you:

* 60 to 100 ms startup buffer

Then hold a target queue depth and monitor:

* late packets
* dropped packets
* underruns

This is simple and works well.

---

# 19. Sample ESP control API

I would make these endpoints:

```text
GET  /status
POST /servo         {"angle":90}
POST /oled          {"line1":"IP 10.0.0.13","line2":"Streaming"}
POST /stream/start  {"udp_port":5006,"codec":"opus","sample_rate":48000,"frame_ms":20}
POST /stream/stop
```

And `/status` returns:

```json
{
  "ip": "10.0.0.13",
  "streaming": true,
  "codec": "opus",
  "sample_rate": 48000,
  "frame_ms": 20,
  "rx_packets": 12345,
  "lost_packets": 8,
  "underruns": 2,
  "servo_angle": 90
}
```

---

# 20. Flask sender design

Backend modules:

```text
webapp/
  app.py
  streamer.py
  opus_sender.py
  ffmpeg_decode.py
  templates/index.html
  static/app.js
```

## `ffmpeg_decode.py`

Runs something like:

```bash
ffmpeg -i input.mp3 -f s16le -ac 1 -ar 48000 pipe:1
```

## `opus_sender.py`

* reads 1920-byte PCM frames
* encodes one frame to Opus
* wraps custom UDP header
* sends to ESP IP and port at exact 20 ms pacing

## `streamer.py`

* manages play/stop state
* sends `/stream/start` and `/stream/stop` to ESP
* keeps stats for UI

---

# 21. What to use for OLED driver

Two decent routes:

## Route 1

Use ESP-IDF’s `esp_lcd` SSD1306 support. ESP-IDF includes SSD1306 panel support under `esp_lcd`, and the registry explicitly points to the IDF driver rather than the older deprecated component. ([GitHub][13])

## Route 2

Use a small registry component such as `k0i05/esp_ssd1306`, which is for generic SSD1306 displays and supports common sizes. ([components.espressif.com][14])

For your use, I would pick the **built-in IDF SSD1306 path** if you want fewer outside dependencies.

---

# 22. What not to do

Do not:

* use NVS as an audio queue
* write incoming audio to SPI flash chunk files in the hot path
* run the audio stream over HTTP multipart POST
* make the OLED update too often
* do servo sweeps in a blocking loop on the same core as audio decode
* start with full duplex + AEC + fancy browser audio all at once

---

# 23. Concrete settings I recommend

## Playback

* codec: Opus
* bitrate: 32000 bps
* channels: mono
* sample rate: 48000
* frame size: 20 ms
* startup buffer: 4 packets

## Mic uplink

* codec: Opus
* bitrate: 16000 to 24000 bps
* channels: mono
* sample rate: 16000
* frame size: 20 ms

## Servo

* PWM: 50 Hz
* pulse min/max tuned to your servo
* non-blocking updates only

## OLED

* refresh every 250 ms
* only redraw changed lines if possible

---

# 24. Build checklist

## ESP-IDF side

1. Create new IDF project
2. Add Wi-Fi station connect
3. Add HTTP control server
4. Add servo control with LEDC
5. Add OLED init and text draw
6. Add I2S TX test tone
7. Add I2S RX mic capture test
8. Add UDP RX task
9. Add PCM playback ring buffer
10. Prove PCM streaming
11. Add `espressif/esp_audio_codec`
12. Add Opus decode task
13. Switch network audio from PCM to Opus
14. Add jitter buffer logic
15. Add stats endpoint
16. Add mic Opus uplink later

## Python side

1. Flask UI
2. connect/status polling
3. MP3 upload
4. ffmpeg decode to PCM
5. Opus encode per 20 ms frame
6. UDP packet sender
7. play/stop and stats
8. servo slider and OLED controls

---

# 25. My recommendation on libraries

## ESP side

* ESP-IDF stable release
* `esp_http_server`
* `esp_wifi`
* `esp_lcd` or SSD1306 component
* LEDC / servo helper component
* `espressif/esp_audio_codec`

## Python side

* Flask
* requests
* ffmpeg installed on system
* a libopus Python binding, for per-frame encode
* optionally `python-socketio` later for fancier live UI

---

# 26. One important practical note

If your end goal is mostly **voice assistant / robot speech**, you may not even need Opus downlink first. PCM over LAN might be enough if the network is local and stable.

But if you want:

* smoother longer playback
* less bandwidth
* cleaner real-time behavior
* future two-way audio

then Opus is worth it.

---

# 27. Best first milestone

The first milestone I would actually aim for is:

* ESP32-S3 on Wi-Fi
* `/status`, `/servo`, `/oled`
* stable **UDP PCM** playback to MAX98357A for 10 minutes
* then swap the sender to **Opus**
* then add mic return

That reduces the number of unknowns.

---

# 28. Final recommendation

Build it as:

* **ESP-IDF firmware**

  * Wi-Fi STA
  * HTTP control
  * UDP Opus receive
  * Opus decode
  * ring buffer
  * I2S TX speaker
  * I2S RX mic
  * LEDC servo
  * I2C SSD1306

* **Python Flask webapp**

  * local UI
  * ffmpeg decode
  * per-frame Opus encode
  * UDP sender
  * control API client

[1]: https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-reference/protocols/esp_http_server.html "HTTP Server - ESP32 -  — ESP-IDF Programming Guide v6.0 documentation"
[2]: https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-reference/storage/nvs_flash.html "Non-Volatile Storage Library - ESP32 -  — ESP-IDF Programming Guide v6.0 documentation"
[3]: https://opus-codec.org/?utm_source=chatgpt.com "Opus Codec"
[4]: https://www.rfc-editor.org/rfc/rfc6716.html?utm_source=chatgpt.com "RFC 6716: Definition of the Opus Audio Codec"
[5]: https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-reference/network/esp_wifi.html "Wi-Fi - ESP32 -  — ESP-IDF Programming Guide v6.0 documentation"
[6]: https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/api-reference/peripherals/i2s.html "Inter-IC Sound (I2S) - ESP32-S3 -  — ESP-IDF Programming Guide v6.0 documentation"
[7]: https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-reference/system/freertos_additions.html "FreeRTOS (Supplemental Features) - ESP32 -  — ESP-IDF Programming Guide v6.0 documentation"
[8]: https://docs.espressif.com/projects/esp-iot-solution/en/latest/motor/servo.html "Servo -  -  — ESP-IoT-Solution latest documentation"
[9]: https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-reference/peripherals/i2c.html?utm_source=chatgpt.com "Inter-Integrated Circuit (I2C) - ESP32"
[10]: https://components.espressif.com/components/espressif/esp_audio_codec/versions/2.0.3 "
    espressif/esp_audio_codec • v2.0.3
•
    ESP Component Registry"
[11]: https://docs.espressif.com/projects/esp-adf/en/latest/api-reference/codecs/opus_decoder.html "OPUS Decoder -  -  — Espressif Audio Development Framework Guide latest documentation"
[12]: https://components.espressif.com/components/espressif/ssd1306/versions/1.0.5~1/readme?utm_source=chatgpt.com "espressif/ssd1306 • v1.0.5~1 - ESP Component Registry"
[13]: https://github.com/espressif/esp-idf/blob/master/components/esp_lcd/include/esp_lcd_panel_ssd1306.h?utm_source=chatgpt.com "esp_lcd_panel_ssd1306.h"
[14]: https://components.espressif.com/components/k0i05/esp_ssd1306?utm_source=chatgpt.com "k0i05/esp_ssd1306 • v1.2.7 - ESP Component Registry"
