"""
============================================
IoT Smart Room - Alert Service
Tích hợp các kênh thông báo:
  - Telegram Bot (text + ảnh)
  - WebSocket (dashboard)
  - MQTT (ESP32: LED, buzzer, OLED)
============================================
"""

import logging
import asyncio
from datetime import datetime
from typing import Dict, Optional

from config import Config
from services.telegram_service import telegram_service

logger = logging.getLogger(__name__)


class AlertService:
    """Quản lý cảnh báo và thông báo qua nhiều kênh."""

    def __init__(self):
        self._ws_broadcast = None
        self._mqtt_publish = None
        self.alert_history = []

    # ------------------------------------------------------------------
    # Khởi tạo
    # ------------------------------------------------------------------

    async def init_telegram(self):
        """Khởi tạo Telegram bot."""
        ok = await telegram_service.init(
            bot_token=Config.TELEGRAM_BOT_TOKEN,
            chat_id=Config.TELEGRAM_CHAT_ID
        )
        if ok:
            logger.info("✅ Telegram notification channel ready")
        else:
            logger.warning("⚠ Telegram không khả dụng – kiểm tra .env (TELEGRAM_BOT_TOKEN, TELEGRAM_CHAT_ID)")

    def set_ws_broadcast(self, broadcast_fn):
        """Thiết lập hàm broadcast WebSocket."""
        self._ws_broadcast = broadcast_fn

    def set_mqtt_publish(self, publish_fn):
        """Thiết lập hàm publish MQTT."""
        self._mqtt_publish = publish_fn

    # ------------------------------------------------------------------
    # Gửi cảnh báo chung
    # ------------------------------------------------------------------

    async def send_alert(self, alert_type: str, message: str, severity: str = "warning",
                         device: str = "system", data: Dict = None):
        """
        Gửi cảnh báo qua tất cả các kênh.

        Args:
            alert_type: Loại cảnh báo (temperature, gas, co, security, ...)
            message: Nội dung
            severity: info | warning | danger | critical
            device: Thiết bị nguồn
            data: Dữ liệu bổ sung
        """
        alert = {
            "type": alert_type,
            "message": message,
            "severity": severity,
            "device": device,
            "data": data or {},
            "timestamp": datetime.utcnow().isoformat()
        }

        self.alert_history.append(alert)
        if len(self.alert_history) > 100:
            self.alert_history = self.alert_history[-100:]

        severity_emoji = {
            "info": "ℹ️",
            "warning": "⚠️",
            "danger": "🔴",
            "critical": "🚨"
        }
        emoji = severity_emoji.get(severity, "📢")

        # 1. Telegram
        await telegram_service.send_sensor_alert(alert_type, message, severity, data)

        # 2. WebSocket → Dashboard
        self._send_websocket(alert)

        # 3. MQTT → ESP32
        self._send_mqtt_alert(alert_type, message, severity)

        logger.info(f"{emoji} Alert [{severity}] {alert_type}: {message}")

    # ------------------------------------------------------------------
    # Cảnh báo khuôn mặt (có ảnh)
    # ------------------------------------------------------------------

    async def send_face_alert(self, name: str, confidence: float,
                              image_data: bytes = None):
        """
        Gửi cảnh báo nhận diện khuôn mặt.

        - Người lạ: gửi ảnh + text cảnh báo lên Telegram, kích hoạt MQTT alarm
        - Người quen: gửi text thông báo vào phòng
        """
        if name == "Unknown":
            logger.warning("🚨 Người lạ được phát hiện – gửi Telegram")

            # Telegram: ảnh + caption (cooldown 30s)
            sent = await telegram_service.notify_unknown_face(confidence, image_data or b"")

            if sent:
                logger.info("📤 Đã gửi cảnh báo người lạ lên Telegram")

            # WebSocket → dashboard
            self._send_websocket({
                "type": "security",
                "message": f"Người lạ tại cửa! ({confidence:.1%})",
                "severity": "danger",
                "timestamp": datetime.utcnow().isoformat()
            })

            # MQTT → ESP32 alarm
            self._send_mqtt_alert("security", "Người lạ tại cửa!", "danger")

        else:
            logger.info(f"✅ {name} vào phòng ({confidence:.1%})")

            # Telegram thông báo (cooldown 10s per-name)
            await telegram_service.notify_face_recognized(name, confidence)

            # WebSocket
            self._send_websocket({
                "type": "access",
                "message": f"{name} đã vào phòng ({confidence:.1%})",
                "severity": "info",
                "timestamp": datetime.utcnow().isoformat()
            })

    # ------------------------------------------------------------------
    # Báo cáo hàng ngày
    # ------------------------------------------------------------------

    async def send_daily_report(self, db):
        """Gửi báo cáo tóm tắt hàng ngày lên Telegram."""
        stats = db.get_stats()
        now = datetime.now()

        text = (
            "📊 <b>Báo cáo hàng ngày – IoT Smart Room</b>\n"
            f"📅 <i>{now.strftime('%d/%m/%Y')}</i>\n"
            "\n"
            f"📈 Tổng số lần đo: {stats.get('readings_today', 0)}\n"
            f"⚠️ Cảnh báo hôm nay: {stats.get('total_alerts', 0)}\n"
            f"🔓 Lượt truy cập: {stats.get('access_attempts_today', 0)}\n"
            f"🔍 Dị thường phát hiện: {stats.get('anomalies_detected', 0)}\n"
            f"👤 Khuôn mặt đã đăng ký: {stats.get('registered_faces', 0)}\n"
        )

        await telegram_service.send_text(text)

    # ------------------------------------------------------------------
    # Internal helpers
    # ------------------------------------------------------------------

    def _send_websocket(self, alert: Dict):
        """Broadcast cảnh báo tới các client WebSocket."""
        if self._ws_broadcast:
            try:
                self._ws_broadcast("alert", alert)
            except Exception as e:
                logger.error(f"WebSocket broadcast error: {e}")

    def _send_mqtt_alert(self, alert_type: str, message: str, severity: str):
        """Gửi trigger cảnh báo tới ESP32 qua MQTT."""
        if not self._mqtt_publish:
            return
        try:
            self._mqtt_publish(Config.TOPICS["alert_trigger"], {
                "type": alert_type,
                "message": message,
                "severity": severity
            })

            if severity in ("danger", "critical"):
                self._mqtt_publish(Config.TOPICS["tts_play"], {
                    "text": f"Cảnh báo! {message}"
                })
        except Exception as e:
            logger.error(f"MQTT alert error: {e}")


# Singleton
alert_service = AlertService()
