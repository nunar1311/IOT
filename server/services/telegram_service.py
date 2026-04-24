"""
============================================
IoT Smart Room - Telegram Notification Service
Kênh thông báo: Telegram Bot
- Gửi text + ảnh khi phát hiện người lạ
- Cooldown chống spam
- Tin nhắn định dạng HTML đẹp
============================================
"""

import logging
import asyncio
from datetime import datetime, timedelta
from io import BytesIO
from typing import Optional

logger = logging.getLogger(__name__)


class TelegramService:
    """
    Dịch vụ gửi thông báo qua Telegram Bot.
    Hỗ trợ:
      - Gửi text (HTML parse mode)
      - Gửi ảnh kèm caption khi phát hiện người lạ
      - Cooldown để tránh spam
    """

    # Thời gian chờ tối thiểu giữa 2 lần cảnh báo UNKNOWN (giây)
    UNKNOWN_COOLDOWN_SECONDS = 30
    # Thời gian chờ tối thiểu giữa 2 thông báo access bình thường (giây)
    ACCESS_COOLDOWN_SECONDS = 10

    def __init__(self):
        self._bot = None
        self._chat_id: Optional[str] = None
        self._last_unknown_alert: Optional[datetime] = None
        self._last_access_alert: dict = {}   # name -> datetime
        self._initialized = False

    async def init(self, bot_token: str, chat_id: str) -> bool:
        """
        Khởi tạo Telegram bot.
        Trả về True nếu thành công.
        """
        if not bot_token or bot_token == "YOUR_TELEGRAM_BOT_TOKEN_HERE":
            logger.warning("⚠ TELEGRAM_BOT_TOKEN chưa được cấu hình")
            return False

        if not chat_id:
            logger.warning("⚠ TELEGRAM_CHAT_ID chưa được cấu hình. "
                           "Gửi /start cho bot rồi chạy lệnh get_chat_id để lấy ID.")
            return False

        try:
            from telegram import Bot
            self._bot = Bot(token=bot_token)
            me = await self._bot.get_me()
            self._chat_id = chat_id
            self._initialized = True
            logger.info(f"🤖 Telegram bot sẵn sàng: @{me.username} → chat {chat_id}")
            return True
        except Exception as e:
            logger.error(f"❌ Không thể khởi tạo Telegram bot: {e}")
            return False

    # ------------------------------------------------------------------
    # Public API
    # ------------------------------------------------------------------

    async def notify_unknown_face(self, confidence: float, image_bytes: bytes) -> bool:
        """
        Gửi cảnh báo người lạ kèm ảnh lên Telegram.
        Áp dụng cooldown để tránh spam.

        Args:
            confidence: Độ tin cậy nhận diện (0–1)
            image_bytes: Dữ liệu ảnh JPEG

        Returns:
            True nếu tin nhắn được gửi thành công, False nếu bị throttle / lỗi.
        """
        if not self._initialized:
            return False

        # Kiểm tra cooldown
        now = datetime.now()
        if self._last_unknown_alert:
            elapsed = (now - self._last_unknown_alert).total_seconds()
            if elapsed < self.UNKNOWN_COOLDOWN_SECONDS:
                remaining = int(self.UNKNOWN_COOLDOWN_SECONDS - elapsed)
                logger.debug(f"🔇 Telegram cooldown: còn {remaining}s")
                return False

        self._last_unknown_alert = now

        caption = self._build_unknown_caption(confidence, now)

        try:
            if image_bytes:
                await self._bot.send_photo(
                    chat_id=self._chat_id,
                    photo=BytesIO(image_bytes),
                    caption=caption,
                    parse_mode="HTML"
                )
            else:
                await self._bot.send_message(
                    chat_id=self._chat_id,
                    text=caption,
                    parse_mode="HTML"
                )
            logger.info("📤 Đã gửi cảnh báo người lạ lên Telegram")
            return True
        except Exception as e:
            logger.error(f"❌ Gửi Telegram thất bại: {e}")
            return False

    async def notify_face_recognized(self, name: str, confidence: float) -> bool:
        """
        Thông báo khi nhận diện được khuôn mặt đã biết.
        Áp dụng cooldown per-name.

        Args:
            name: Tên người được nhận diện
            confidence: Độ tin cậy

        Returns:
            True nếu gửi thành công.
        """
        if not self._initialized:
            return False

        now = datetime.now()
        last = self._last_access_alert.get(name)
        if last:
            elapsed = (now - last).total_seconds()
            if elapsed < self.ACCESS_COOLDOWN_SECONDS:
                return False

        self._last_access_alert[name] = now

        text = self._build_access_caption(name, confidence, now)

        try:
            await self._bot.send_message(
                chat_id=self._chat_id,
                text=text,
                parse_mode="HTML"
            )
            logger.info(f"📤 Đã gửi thông báo vào phòng: {name}")
            return True
        except Exception as e:
            logger.error(f"❌ Gửi Telegram thất bại: {e}")
            return False

    async def send_text(self, text: str, parse_mode: str = "HTML") -> bool:
        """
        Gửi tin nhắn văn bản tùy ý.
        """
        if not self._initialized:
            return False
        try:
            await self._bot.send_message(
                chat_id=self._chat_id,
                text=text,
                parse_mode=parse_mode
            )
            return True
        except Exception as e:
            logger.error(f"❌ Telegram send_text error: {e}")
            return False

    async def send_photo(self, image_bytes: bytes, caption: str = "") -> bool:
        """
        Gửi ảnh kèm caption tùy ý.
        """
        if not self._initialized:
            return False
        try:
            await self._bot.send_photo(
                chat_id=self._chat_id,
                photo=BytesIO(image_bytes),
                caption=caption,
                parse_mode="HTML"
            )
            return True
        except Exception as e:
            logger.error(f"❌ Telegram send_photo error: {e}")
            return False

    async def send_sensor_alert(self, alert_type: str, message: str,
                                severity: str, data: dict = None) -> bool:
        """
        Gửi cảnh báo cảm biến (nhiệt độ, khí gas, CO, ...) lên Telegram.
        """
        if not self._initialized:
            return False

        severity_emoji = {
            "info": "ℹ️",
            "warning": "⚠️",
            "danger": "🔴",
            "critical": "🚨"
        }
        emoji = severity_emoji.get(severity, "📢")
        now = datetime.now()

        lines = [
            f"{emoji} <b>IoT Smart Room – Cảnh báo</b>",
            "",
            f"📋 <b>Loại:</b> {alert_type}",
            f"⚡ <b>Mức độ:</b> {severity.upper()}",
            f"💬 <b>Nội dung:</b> {message}",
            f"🕐 <b>Thời gian:</b> {now.strftime('%H:%M:%S  %d/%m/%Y')}",
        ]

        if data:
            lines.append("")
            lines.append("📊 <b>Chi tiết:</b>")
            for k, v in data.items():
                lines.append(f"  • {k}: {v}")

        text = "\n".join(lines)

        try:
            await self._bot.send_message(
                chat_id=self._chat_id,
                text=text,
                parse_mode="HTML"
            )
            return True
        except Exception as e:
            logger.error(f"❌ Telegram sensor alert error: {e}")
            return False

    @property
    def is_ready(self) -> bool:
        """Kiểm tra bot đã được khởi tạo."""
        return self._initialized

    # ------------------------------------------------------------------
    # Helpers
    # ------------------------------------------------------------------

    @staticmethod
    def _build_unknown_caption(confidence: float, ts: datetime) -> str:
        conf_pct = f"{confidence:.1%}"
        time_str = ts.strftime("%H:%M:%S  %d/%m/%Y")
        return (
            "🚨 <b>CẢNH BÁO – Phát hiện người lạ!</b>\n"
            "\n"
            "👤 <b>Danh tính:</b> Không xác định\n"
            f"🎯 <b>Độ tin cậy:</b> {conf_pct}\n"
            f"🕐 <b>Thời gian:</b> {time_str}\n"
            "📍 <b>Vị trí:</b> Cửa ra vào\n"
            "\n"
            "⚠️ Vui lòng kiểm tra camera ngay!"
        )

    @staticmethod
    def _build_access_caption(name: str, confidence: float, ts: datetime) -> str:
        conf_pct = f"{confidence:.1%}"
        time_str = ts.strftime("%H:%M:%S  %d/%m/%Y")
        return (
            "✅ <b>Truy cập được cấp phép</b>\n"
            "\n"
            f"👤 <b>Tên:</b> {name}\n"
            f"🎯 <b>Độ tin cậy:</b> {conf_pct}\n"
            f"🕐 <b>Thời gian:</b> {time_str}\n"
            "🔓 <b>Cửa đã mở tự động</b>"
        )


# Singleton
telegram_service = TelegramService()
