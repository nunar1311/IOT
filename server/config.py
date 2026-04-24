"""
============================================
IoT Smart Room - Configuration
============================================
"""

import os
from dotenv import load_dotenv

load_dotenv()

class Config:
    # MQTT
    MQTT_BROKER = os.getenv("MQTT_BROKER_HOST", "localhost")
    MQTT_PORT = int(os.getenv("MQTT_BROKER_PORT", 1883))
    MQTT_USERNAME = os.getenv("MQTT_USERNAME", "")
    MQTT_PASSWORD = os.getenv("MQTT_PASSWORD", "")
    
    # MongoDB
    MONGO_HOST = os.getenv("MONGO_HOST", "localhost")
    MONGO_PORT = int(os.getenv("MONGO_PORT", 27017))
    MONGO_DB = os.getenv("MONGO_DB", "iot_smart_room")
    MONGO_USERNAME = os.getenv("MONGO_USERNAME", "iot_user")
    MONGO_PASSWORD = os.getenv("MONGO_PASSWORD", "iot_mongo_2024")
    MONGO_URI = f"mongodb://{MONGO_USERNAME}:{MONGO_PASSWORD}@{MONGO_HOST}:{MONGO_PORT}/{MONGO_DB}?authSource=admin"
    
    # Server
    SERVER_HOST = os.getenv("SERVER_HOST", "0.0.0.0")
    SERVER_PORT = int(os.getenv("SERVER_PORT", 8000))
    SECRET_KEY = os.getenv("SECRET_KEY", "iot-smart-room-secret")
    
    # ESP32-CAM
    CAM_STREAM_URL = os.getenv("ESP32_CAM_STREAM_URL", "http://esp32-cam.local:81/stream")
    CAM_CAPTURE_URL = os.getenv("ESP32_CAM_CAPTURE_URL", "http://esp32-cam.local/capture")
    
    # Telegram
    TELEGRAM_BOT_TOKEN = os.getenv("TELEGRAM_BOT_TOKEN", "")
    TELEGRAM_CHAT_ID = os.getenv("TELEGRAM_CHAT_ID", "")
    
    # Thresholds
    TEMP_HIGH = float(os.getenv("TEMP_HIGH_THRESHOLD", 35.0))
    TEMP_WARNING = float(os.getenv("TEMP_WARNING_THRESHOLD", 30.0))
    HUMIDITY_HIGH = float(os.getenv("HUMIDITY_HIGH_THRESHOLD", 80.0))
    GAS_THRESHOLD = int(os.getenv("GAS_THRESHOLD", 400))
    CO_THRESHOLD = int(os.getenv("CO_THRESHOLD", 300))
    PM25_THRESHOLD = int(os.getenv("PM25_THRESHOLD", 35))
    PM10_THRESHOLD = int(os.getenv("PM10_THRESHOLD", 50))
    
    # Anomaly Detection
    ANOMALY_MODEL_PATH = os.path.join(os.path.dirname(__file__), "models", "anomaly_model.h5")
    ANOMALY_THRESHOLD = 0.05  # Reconstruction error threshold
    ANOMALY_WINDOW_SIZE = 30  # Number of readings for sequence analysis
    
    # Face Recognition
    FACES_DIR = os.path.join(os.path.dirname(__file__), "faces_db")
    FACE_RECOGNITION_TOLERANCE = 0.5  # Lower = more strict
    FACE_CHECK_INTERVAL = 2  # Seconds between face checks
    
    # MQTT Topics
    TOPICS = {
        "env": "iot/sensors/env",
        "air": "iot/sensors/air",
        "motion": "iot/sensors/motion",
        "status": "iot/sensors/status",
        "alert": "iot/alert/event",
        "alert_trigger": "iot/alert/trigger",
        "control_relay": "iot/control/relay",
        "control_motor": "iot/control/motor",
        "control_led": "iot/control/led",
        "door_command": "iot/door/command",
        "door_status": "iot/door/status",
        "cam_status": "iot/cam/status",
        "tts_play": "iot/tts/play",
    }
