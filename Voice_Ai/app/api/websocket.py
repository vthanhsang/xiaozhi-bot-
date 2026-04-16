from fastapi import APIRouter, WebSocket
import os
import asyncio

from app.services.whisper_service import transcribe
from app.services.ollama_service import ask_llm
from app.services.tts_service import text_to_speech

router = APIRouter()

@router.websocket("/ws/audio")
async def websocket_endpoint(ws: WebSocket):
    await ws.accept()
    print("Client connected")

    audio_path = "audio/input.wav"

    while True:
        try:
            # 📥 nhận audio từ ESP32
            data = await ws.receive_bytes()

            # lưu file
            with open(audio_path, "wb") as f:
                f.write(data)

            print("Audio received")

            # 🧠 Whisper
            text = transcribe(audio_path)
            print("TEXT:", text)

            # 🤖 Ollama
            reply = ask_llm(text)
            print("AI:", reply)

            # 🔊 TTS
            output_path = "audio/output.mp3"
            await text_to_speech(reply, output_path)

            # 📤 gửi lại audio
            with open(output_path, "rb") as f:
                await ws.send_bytes(f.read())

        except Exception as e:
            print("Error:", e)
            break