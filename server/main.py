"""
============================================
IoT Smart Room - Main Server Entry Point
============================================
"""

import os
import sys
import asyncio
import logging
from datetime import datetime

import uvicorn

# Setup logging
logging.basicConfig(
    level=logging.INFO,
    format="%(asctime)s [%(levelname)s] %(name)s: %(message)s",
    datefmt="%Y-%m-%d %H:%M:%S"
)
logger = logging.getLogger(__name__)

# Add server directory to path
sys.path.insert(0, os.path.dirname(__file__))

from config import Config
from database import db
from mqtt_client import mqtt_client
from models.face_recognizer import face_recognizer
from models.anomaly_detector import anomaly_detector
from services.alert_service import alert_service
from services.telegram_service import telegram_service
from services.data_processor import data_processor
from services.tts_service import tts_service
from api.app import app, ws_manager, mqtt_to_ws


# Event loop chính – được gán trong startup() sau khi asyncio loop sẵn sàng
_main_loop: asyncio.AbstractEventLoop = None


def setup_mqtt_handlers():
    """Register MQTT topic handlers."""
    # Sensor data handlers
    mqtt_client.subscribe(Config.TOPICS["env"], data_processor.process_environment)
    mqtt_client.subscribe(Config.TOPICS["air"], data_processor.process_air_quality)
    mqtt_client.subscribe(Config.TOPICS["motion"], data_processor.process_motion)
    
    # Alert handler
    mqtt_client.subscribe(Config.TOPICS["alert"], data_processor.process_alert)
    
    # Door events
    mqtt_client.subscribe(Config.TOPICS["door_status"], data_processor.process_door_event)
    
    # TTS audio playback handler
    mqtt_client.subscribe(Config.TOPICS["tts_play"], tts_service.process_tts_message)
    
    # WebSocket bridge (forward all IoT messages to dashboard)
    mqtt_client.set_ws_broadcast(lambda topic, data: mqtt_to_ws(topic, data))
    
    logger.info("📌 MQTT handlers registered")


def setup_face_recognition():
    """Initialize face recognition system."""
    # Load known faces from database
    face_recognizer.load_faces_from_db(db)

    # Define callbacks – chạy trong thread riêng của face_recognizer
    def on_face_recognized(name, confidence, frame):
        logger.info(f"✅ Face recognized: {name} ({confidence:.1%})")

        # Mở cửa qua MQTT
        mqtt_client.publish(Config.TOPICS["door_command"], {
            "command": "UNLOCK",
            "name": name
        })

        # Lưu log truy cập
        db.save_access_log(name, "granted", confidence)

        # Broadcast lên dashboard
        ws_manager.broadcast_sync("face_recognized", {"name": name, "confidence": confidence})

        # Gửi thông báo Telegram (schedule sang main event loop)
        if _main_loop and _main_loop.is_running():
            asyncio.run_coroutine_threadsafe(
                alert_service.send_face_alert(name, confidence),
                _main_loop
            )

    def on_unknown_face(frame, confidence):
        logger.warning("⚠ Unknown face detected!")

        # Lưu ảnh người lạ
        timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
        unknown_dir = os.path.join(Config.FACES_DIR, "unknown")
        os.makedirs(unknown_dir, exist_ok=True)

        import cv2
        image_path = os.path.join(unknown_dir, f"unknown_{timestamp}.jpg")
        cv2.imwrite(image_path, frame)

        # Đọc bytes ảnh để gửi Telegram
        try:
            with open(image_path, "rb") as f:
                image_data = f.read()
        except Exception as e:
            logger.error(f"Cannot read saved frame: {e}")
            image_data = None

        # Lưu log
        db.save_access_log("Unknown", "denied", confidence, image_path)

        # Kích hoạt alarm MQTT (LED + buzzer ESP32)
        mqtt_client.publish(Config.TOPICS["alert_trigger"], {
            "type": "emergency",
            "message": "Người lạ tại cửa!"
        })

        # TTS cảnh báo
        mqtt_client.publish(Config.TOPICS["tts_play"], {
            "text": "Cảnh báo! Có người lạ xâm nhập."
        })

        # Broadcast lên dashboard
        ws_manager.broadcast_sync("face_unknown", {"confidence": confidence})

        # ── Gửi ảnh + text cảnh báo lên Telegram ──
        if _main_loop and _main_loop.is_running():
            asyncio.run_coroutine_threadsafe(
                alert_service.send_face_alert("Unknown", confidence, image_data),
                _main_loop
            )

    def on_no_face():
        pass  # Trạng thái bình thường

    # Khởi động monitoring
    face_recognizer.start_monitoring(
        on_recognized=on_face_recognized,
        on_unknown=on_unknown_face,
        on_no_face=on_no_face
    )

    logger.info("🎥 Face recognition system started")


def setup_anomaly_detection():
    """Initialize anomaly detection model."""
    # Try to load pre-trained model
    if not anomaly_detector.load_model():
        logger.info("🧠 No pre-trained model. Will use statistical detection until enough data is collected.")
        anomaly_detector.build_model()
    
    # Register anomaly callback
    def on_anomaly(error, features, raw_data):
        feature_names = ", ".join([f["feature"] for f in features])
        logger.warning(f"🔍 Anomaly detected! Features: {feature_names}, Error: {error:.6f}")
    
    anomaly_detector.on_anomaly(on_anomaly)
    logger.info("🧠 Anomaly detection initialized")


async def startup():
    """Application startup tasks."""
    logger.info("=" * 50)
    logger.info("  IoT Smart Room Server Starting...")
    logger.info("=" * 50)

    # 1. Kết nối MongoDB
    db.connect()

    # 2. Kết nối MQTT
    mqtt_client.connect()
    setup_mqtt_handlers()

    # 3. Khởi tạo kênh cảnh báo
    alert_service.set_mqtt_publish(mqtt_client.publish)
    alert_service.set_ws_broadcast(ws_manager.broadcast_sync)
    await alert_service.init_telegram()   # ← Telegram Bot

    # 4. Lấy event loop chính và truyền cho các service cần schedule
    global _main_loop
    loop = asyncio.get_running_loop()
    _main_loop = loop  # Cho phép callbacks trong thread gọi Telegram
    data_processor.set_event_loop(loop)
    ws_manager.set_loop(loop)

    # 5. Anomaly detection
    setup_anomaly_detection()

    # 6. Face recognition (callbacks chạy trong thread riêng)
    setup_face_recognition()

    logger.info("=" * 50)
    logger.info(f"  Server ready at http://{Config.SERVER_HOST}:{Config.SERVER_PORT}")
    logger.info(f"  Dashboard: http://localhost:{Config.SERVER_PORT}")
    logger.info(f"  API Docs: http://localhost:{Config.SERVER_PORT}/docs")
    logger.info("=" * 50)


async def shutdown():
    """Application shutdown tasks."""
    logger.info("Shutting down...")
    face_recognizer.stop_monitoring()
    mqtt_client.disconnect()
    db.close()
    logger.info("Server stopped")


from contextlib import asynccontextmanager

@asynccontextmanager
async def lifespan(app):
    await startup()
    yield
    await shutdown()

app.router.lifespan_context = lifespan


if __name__ == "__main__":
    uvicorn.run(
        "main:app",
        host=Config.SERVER_HOST,
        port=Config.SERVER_PORT,
        reload=False,
        log_level="info",
        ws_ping_interval=30,
        ws_ping_timeout=30
    )
