from fastapi import FastAPI
from app.api.websocket import router

app = FastAPI()

app.include_router(router)

@app.get("/")
def root():
    return {"status": "Server running"}