"""
============================================
IoT Smart Room - TTS Audio Service
Plays Vietnamese text-to-speech alerts through connected speaker
============================================
"""

import os
import logging
import threading
import tempfile
import uuid
from typing import Dict

logger = logging.getLogger(__name__)

class TTSService:
    """Service to play Text-to-Speech audio locally using gTTS and playsound."""
    
    def __init__(self):
        self._initialized = False
        self._last_play_time = 0.0
        self._setup_audio()

    def _setup_audio(self):
        try:
            import playsound
            self._initialized = True
            logger.info("🔊 TTS Audio service initialized (playsound)")
        except ImportError:
            logger.error("❌ Need to install 'gTTS' and 'playsound' for TTS functionality")
        except Exception as e:
            logger.error(f"❌ Audio init error: {e}")

    def play_tts(self, text: str):
        if not self._initialized:
            logger.warning("TTS not initialized. Cannot play audio.")
            return
            
        import time
        current_time = time.time()
        if current_time - self._last_play_time < 10.0:
            logger.info("⏳ Skipping TTS: Rate limited (5s cooldown)")
            return
            
        self._last_play_time = current_time
        
        # Run in a separate thread so it doesn't block MQTT or asyncio loop
        def _play():
            try:
                from gtts import gTTS
                from playsound import playsound
                
                logger.info(f"🎤 Generating TTS for: {text}")
                tts = gTTS(text=text, lang='vi')
                
                # Use a temp file with unique name to avoid file lock issues in Windows
                temp_file = os.path.join(tempfile.gettempdir(), f"iot_alert_{uuid.uuid4().hex}.mp3")
                tts.save(temp_file)
                
                # playsound blocks until finished on Windows
                playsound(temp_file)
                
                # Cleanup
                try:
                    os.remove(temp_file)
                except Exception:
                    pass # Windows might keep the file locked if playsound didn't release completely

            except Exception as e:
                logger.error(f"❌ TTS playback error: {e}")

        threading.Thread(target=_play, daemon=True).start()

    def process_tts_message(self, topic: str, data: Dict):
        """Handler for MQTT tts_play topic."""
        text = data.get("text", "")
        if text:
            self.play_tts(text)

# Singleton instance
tts_service = TTSService()
