
from flask import Flask, request, Response
import edge_tts #microsoft TTS Engine 
import asyncio  #untuk jalankan fungsi async
import io   #buffer di memori
import subprocess   #jalankan ffmpeg
import numpy as np  #validasi audio data 

app = Flask(__name__)

VOICE = "id-ID-ArdiNeural"



# ================= TTS =================
#mengubah text json menjadi mp3
async def generate_tts_mp3(text, rate="-10%", pitch="+5Hz"):
    communicate = edge_tts.Communicate(
        text,
         VOICE,
         rate=rate, #kecepatan bicara
         pitch=pitch,   #nada suara 
         volume="+20%"  #keras suara
)
    audio_buffer = io.BytesIO()

    #text - kirim ke microsoft tts server - terima audio dalam bentuk chunk - kumpulkan chunk dalam memori - ubah ke mp3
    async for chunk in communicate.stream():
        if chunk["type"] == "audio":
            audio_buffer.write(chunk["data"])

    data = audio_buffer.getvalue()

    if len(data) == 0:
        print("❌ EDGE-TTS RETURN EMPTY AUDIO")
    return data


# ================= MP3 -> PCM =================
# mengubah mp3 jadi pcm fungsi untuk membersihkan noise/suara tunning
def mp3_to_pcm(mp3_data):

    if mp3_data is None or len(mp3_data) < 10:
        print("❌ MP3 kosong sebelum ffmpeg")
        return b""

    process = subprocess.Popen(
        [
            "ffmpeg",
            "-hide_banner",
            "-loglevel", "error",
            "-i", "pipe:0",
            "-af",
            # Urutan: denoise dulu → cut frekuensi buruk → boost vokal → normalize
            "afftdn=nf=-25,"                          # 1. Noise reduction PERTAMA
            "highpass=f=180,"                         # 2. Cut bass rumble (<180Hz)
            "lowpass=f=10000,"                         # 3. Cut frekuensi tinggi berlebih
            "equalizer=f=300:t=q:w=1:g=-4,"          # 4. Cut mud/boxy (300Hz)
            "equalizer=f=1000:t=q:w=1:g=-2,"         # 5. Cut nasal (1kHz)
            "equalizer=f=3000:t=q:w=1:g=+4,"         # 6. Boost presence/clarity vokal
            "equalizer=f=5000:t=q:w=1:g=+2,"         # 7. Boost air/ketajaman vokal
            "equalizer=f=6500:t=q:w=1:g=-5,"   # ← potong sibilance utama
            "equalizer=f=8000:t=q:w=1:g=-4,"
            "volume=1.5",                             # 8. Gain terakhir
            "-f", "s16le",
            "-acodec", "pcm_s16le",
            "-ar", "48000",
            "-ac", "1",
            "pipe:1"
        ],
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE
    )

    pcm_data, ffmpeg_err = process.communicate(input=mp3_data)

    # ================= VALIDASI =================
    if pcm_data is None or len(pcm_data) == 0:
        print("❌ FFmpeg gagal convert!")
        print(ffmpeg_err.decode("utf-8", errors="ignore"))
        return b""

    arr = np.frombuffer(pcm_data, dtype=np.int16)

    if len(arr) == 0 or np.count_nonzero(arr) == 0:
        print("❌ PCM kosong setelah convert!")
        return b""

    print(f"✅ PCM OK: {len(pcm_data)} bytes | {len(arr)} samples")

    return pcm_data


# ================= ROUTE =================
# untuk menunggu ping sinyal dari script tts client  : 
@app.route("/tts", methods=["POST"])
def tts():

    data = request.get_json()
    if not data or "text" not in data:
        return {"error": "no text"}, 400

    text = data["text"]
    print(f"\n🗣 TTS: {text}")

    try:
        # ================= GENERATE MP3 =================
        mp3_data = asyncio.run(generate_tts_mp3(text))

        if len(mp3_data) == 0:
            return {"error": "mp3 empty"}, 500

        print(f"🎧 MP3: {len(mp3_data)} bytes")

        # ================= CONVERT =================
        pcm_data = mp3_to_pcm(mp3_data)

        if len(pcm_data) == 0:
            return {"error": "pcm empty"}, 500

        return Response(
            pcm_data,
            mimetype="application/octet-stream",
            headers={"Content-Length": str(len(pcm_data))}
        )

    except Exception as e: 
        import traceback
        print(traceback.format_exc())
        return {"error": str(e)}, 500


# ================= PING =================
@app.route("/ping")
def ping():
    return {"status": "ok"}


if __name__ == "__main__":
    print("=== TTS SERVER STARTED ===")
    print(f"Voice: {VOICE}")
    app.run(host="0.0.0.0", port=5050, debug=False)
