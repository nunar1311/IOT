"""
============================================
IoT Smart Room - MQTT Client Module
Subscribes to all sensor topics and dispatches data
============================================
"""

import json
import logging
import threading
from datetime import datetime
from typing import Callable, Dict, List, Optional

import paho.mqtt.client as mqtt

from config import Config

logger = logging.getLogger(__name__)


class MQTTClient:
    """MQTT client that subscribes to all IoT topics and dispatches data."""
    
    def __init__(self):
        self.client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2, client_id="iot_server")
        self.client.on_connect = self._on_connect
        self.client.on_disconnect = self._on_disconnect
        self.client.on_message = self._on_message
        
        self._callbacks: Dict[str, List[Callable]] = {}
        self._ws_broadcast: Optional[Callable] = None
        self._connected = False
        self._latest_data: Dict[str, Dict] = {}
    
    def connect(self):
        """Connect to MQTT broker."""
        try:
            if Config.MQTT_USERNAME:
                self.client.username_pw_set(Config.MQTT_USERNAME, Config.MQTT_PASSWORD)
            
            self.client.connect(Config.MQTT_BROKER, Config.MQTT_PORT, keepalive=60)
            self.client.loop_start()
            logger.info(f"🔌 Connecting to MQTT broker: {Config.MQTT_BROKER}:{Config.MQTT_PORT}")
        except Exception as e:
            logger.error(f"❌ MQTT connection error: {e}")
    
    def disconnect(self):
        """Disconnect from MQTT broker."""
        self.client.loop_stop()
        self.client.disconnect()
        logger.info("MQTT disconnected")
    
    def _on_connect(self, client, userdata, flags, reason_code, properties=None):
        """Handle MQTT connection."""
        if reason_code == 0:
            self._connected = True
            logger.info("✅ MQTT connected successfully")
            
            # Subscribe to all topics
            client.subscribe("iot/#")
            logger.info("📡 Subscribed to iot/#")
        else:
            logger.error(f"❌ MQTT connection failed: {reason_code}")
    
    def _on_disconnect(self, client, userdata, flags, reason_code, properties=None):
        """Handle MQTT disconnection."""
        self._connected = False
        logger.warning(f"⚠ MQTT disconnected (rc={reason_code})")
    
    def _on_message(self, client, userdata, msg):
        """Handle incoming MQTT message."""
        try:
            topic = msg.topic
            payload = msg.payload.decode("utf-8")
            
            # Try to parse as JSON
            try:
                data = json.loads(payload)
            except json.JSONDecodeError:
                data = {"raw": payload}
            
            # Add server timestamp
            data["_server_timestamp"] = datetime.utcnow().isoformat()
            data["_topic"] = topic
            
            # Store latest data (Merge to preserve artificially set fields like relay states)
            if topic in self._latest_data and isinstance(self._latest_data[topic], dict) and isinstance(data, dict):
                current = self._latest_data[topic].copy()
                current.update(data)
                self._latest_data[topic] = current
                data = current
            else:
                self._latest_data[topic] = data
            
            # Dispatch to registered callbacks
            if topic in self._callbacks:
                for callback in self._callbacks[topic]:
                    try:
                        callback(topic, data)
                    except Exception as e:
                        logger.error(f"Callback error for {topic}: {e}")
            
            # Wildcard callbacks
            for pattern, callbacks in self._callbacks.items():
                if pattern.endswith("#") and topic.startswith(pattern[:-1]):
                    for callback in callbacks:
                        try:
                            callback(topic, data)
                        except Exception as e:
                            logger.error(f"Wildcard callback error: {e}")
            
            # Broadcast to WebSocket clients
            if self._ws_broadcast:
                try:
                    self._ws_broadcast(topic, data)
                except Exception as e:
                    logger.error(f"WebSocket broadcast error: {e}")
                    
        except Exception as e:
            logger.error(f"Message handling error: {e}")
    
    def subscribe(self, topic: str, callback: Callable):
        """Register a callback for a specific topic."""
        if topic not in self._callbacks:
            self._callbacks[topic] = []
        self._callbacks[topic].append(callback)
        logger.info(f"📌 Registered callback for: {topic}")
    
    def set_ws_broadcast(self, broadcast_fn: Callable):
        """Set WebSocket broadcast function."""
        self._ws_broadcast = broadcast_fn
    
    def publish(self, topic: str, data: Dict, retain: bool = False):
        """Publish data to MQTT topic."""
        payload = json.dumps(data)
        result = self.client.publish(topic, payload, retain=retain)
        if result.rc == mqtt.MQTT_ERR_SUCCESS:
            logger.debug(f"📤 Published to {topic}")
        else:
            logger.error(f"❌ Publish failed to {topic}: {result.rc}")
    
    def get_latest(self, topic: str) -> Optional[Dict]:
        """Get the latest data for a topic."""
        return self._latest_data.get(topic)
    
    def get_all_latest(self) -> Dict:
        """Get all latest data."""
        return self._latest_data.copy()
    
    @property
    def is_connected(self) -> bool:
        return self._connected


# Singleton instance
mqtt_client = MQTTClient()
