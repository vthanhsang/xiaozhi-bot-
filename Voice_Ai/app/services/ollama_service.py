import os

def ask_llm(text):
    cmd = f'ollama run mistral "{text}"'
    return os.popen(cmd).read()