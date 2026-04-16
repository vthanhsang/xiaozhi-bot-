import whisper

model = whisper.load_model("small")  

def transcribe(path):
    result = model.transcribe(path)
    return result["text"]