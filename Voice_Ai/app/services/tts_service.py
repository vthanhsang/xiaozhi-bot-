import edge_tts

async def text_to_speech(text, output_file):
    communicate = edge_tts.Communicate(text, "vi-VN-HoaiMyNeural")
    await communicate.save(output_file)