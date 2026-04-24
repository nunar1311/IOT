"""
============================================
Tiện ích: Lấy Telegram Chat ID
============================================
Cách dùng:
  1. Mở Telegram, tìm bot của bạn và gửi bất kỳ tin nhắn gì
  2. Chạy script này:
       python get_chat_id.py
  3. Script sẽ in ra Chat ID và tự động cập nhật file .env
============================================
"""

import asyncio
import os
import sys
from pathlib import Path

# Đảm bảo import đúng từ thư mục server
sys.path.insert(0, os.path.dirname(__file__))

from dotenv import load_dotenv, set_key

ENV_FILE = Path(__file__).parent / ".env"
load_dotenv(ENV_FILE)

BOT_TOKEN = os.getenv("TELEGRAM_BOT_TOKEN", "")


async def main():
    if not BOT_TOKEN or "YOUR_TOKEN" in BOT_TOKEN:
        print("❌ TELEGRAM_BOT_TOKEN chưa được cấu hình trong .env")
        return

    try:
        from telegram import Bot
    except ImportError:
        print("❌ python-telegram-bot chưa được cài. Chạy: pip install python-telegram-bot==21.1.1")
        return

    bot = Bot(token=BOT_TOKEN)

    print(f"🤖 Kết nối tới bot...")
    me = await bot.get_me()
    print(f"✅ Bot: @{me.username} ({me.first_name})")
    print()
    print("📋 Đang lấy danh sách updates (tin nhắn gần nhất)...")

    updates = await bot.get_updates(limit=10)

    if not updates:
        print()
        print("⚠️  Không có tin nhắn nào.")
        print("   → Mở Telegram, tìm bot của bạn và gửi /start hoặc bất kỳ tin nhắn gì")
        print("   → Sau đó chạy lại script này")
        return

    print()
    print("=" * 50)
    print("  Các Chat ID tìm thấy:")
    print("=" * 50)

    seen = set()
    chats = []
    for update in updates:
        msg = update.message or update.channel_post
        if msg and msg.chat.id not in seen:
            seen.add(msg.chat.id)
            chats.append({
                "id": msg.chat.id,
                "type": msg.chat.type,
                "name": msg.chat.title or msg.chat.full_name or "N/A",
                "username": f"@{msg.chat.username}" if msg.chat.username else ""
            })
            print(f"  ID: {msg.chat.id:>15}  |  Loại: {msg.chat.type:<10}  |  Tên: {msg.chat.title or msg.chat.full_name}")

    print("=" * 50)

    if len(chats) == 1:
        chosen = chats[0]
    else:
        print()
        idx = input("Nhập số thứ tự chat muốn dùng (0 = đầu tiên): ").strip()
        try:
            chosen = chats[int(idx)]
        except (ValueError, IndexError):
            chosen = chats[0]

    chat_id = str(chosen["id"])
    print()
    print(f"✅ Sẽ dùng Chat ID: {chat_id}  ({chosen['name']})")

    # Cập nhật .env
    set_key(str(ENV_FILE), "TELEGRAM_CHAT_ID", chat_id)
    print(f"💾 Đã ghi TELEGRAM_CHAT_ID={chat_id} vào {ENV_FILE}")

    # Gửi tin nhắn test
    print()
    print("📤 Đang gửi tin nhắn kiểm tra...")
    await bot.send_message(
        chat_id=chat_id,
        text=(
            "✅ <b>IoT Smart Room – Kết nối thành công!</b>\n"
            "\n"
            "🤖 Bot Telegram đã được cấu hình.\n"
            "Bạn sẽ nhận thông báo tại đây khi:\n"
            "  🚨 Phát hiện người lạ tại cửa (kèm ảnh)\n"
            "  ✅ Nhận diện khuôn mặt đã đăng ký\n"
            "  ⚠️ Cảnh báo cảm biến (nhiệt độ, khí gas, ...)\n"
        ),
        parse_mode="HTML"
    )
    print("✅ Tin nhắn test đã được gửi!")
    print()
    print("🎉 Hoàn tất! Khởi động lại server để áp dụng.")


if __name__ == "__main__":
    asyncio.run(main())
