from flask import Flask, jsonify, redirect, render_template, request, send_from_directory, url_for
from pathlib import Path
from streamer import SpeakerBotStreamer

BASE_DIR = Path(__file__).resolve().parent
UPLOAD_DIR = BASE_DIR / "uploads"
UPLOAD_DIR.mkdir(exist_ok=True)

app = Flask(__name__)
app.config["UPLOAD_FOLDER"] = str(UPLOAD_DIR)
streamer = SpeakerBotStreamer()


@app.route("/")
def index():
    return render_template("index.html")


@app.route("/health")
def health():
    return jsonify({"ok": True})


@app.route("/api/status")
def api_status():
    return jsonify(streamer.status())


@app.route("/api/connect", methods=["POST"])
def api_connect():
    payload = request.get_json(force=True)
    esp_ip = payload.get("esp_ip", "").strip()
    if not esp_ip:
        return jsonify({"ok": False, "error": "esp_ip is required"}), 400
    streamer.set_esp_ip(esp_ip)
    return jsonify({"ok": True, "esp_status": streamer.fetch_esp_status()})


@app.route("/api/upload", methods=["POST"])
def api_upload():
    if "audio" not in request.files:
        return jsonify({"ok": False, "error": "missing file field: audio"}), 400
    file = request.files["audio"]
    if not file.filename:
        return jsonify({"ok": False, "error": "empty filename"}), 400
    destination = UPLOAD_DIR / file.filename
    file.save(destination)
    return jsonify({"ok": True, "filename": file.filename, "path": str(destination)})


@app.route("/api/play", methods=["POST"])
def api_play():
    payload = request.get_json(force=True)
    filename = payload.get("filename", "").strip()
    if not filename:
        return jsonify({"ok": False, "error": "filename is required"}), 400
    audio_path = UPLOAD_DIR / filename
    if not audio_path.exists():
        return jsonify({"ok": False, "error": f"file not found: {filename}"}), 404
    frame_ms = int(payload.get("frame_ms", 20))
    sample_rate = int(payload.get("sample_rate", 48000))
    codec = payload.get("codec", "pcm")
    bitrate = int(payload.get("bitrate", 32000))
    result = streamer.play(audio_path=audio_path, frame_ms=frame_ms, sample_rate=sample_rate, codec=codec, bitrate=bitrate)
    status = 200 if result.get("ok") else 400
    return jsonify(result), status


@app.route("/api/stop", methods=["POST"])
def api_stop():
    return jsonify(streamer.stop())


@app.route("/api/servo", methods=["POST"])
def api_servo():
    payload = request.get_json(force=True)
    angle = int(payload.get("angle", 90))
    return jsonify(streamer.set_servo(angle))


@app.route("/api/oled", methods=["POST"])
def api_oled():
    payload = request.get_json(force=True)
    return jsonify(streamer.set_oled(payload.get("line1", ""), payload.get("line2", "")))


@app.route("/api/mic", methods=["GET"])
def api_mic():
    return jsonify(streamer.get_mic())



@app.route("/uploads/<path:name>")
def uploads(name: str):
    return send_from_directory(app.config["UPLOAD_FOLDER"], name)


if __name__ == "__main__":
    app.run(host="127.0.0.1", port=5000, debug=True)
