"""
============================================
IoT Smart Room - Data Processor Service
Processes incoming sensor data, triggers analytics
============================================
"""

import logging
import asyncio
from datetime import datetime
from typing import Dict

from config import Config
from database import db
from models.anomaly_detector import anomaly_detector
from services.alert_service import alert_service

logger = logging.getLogger(__name__)


class DataProcessor:
    """Processes incoming sensor data from MQTT and runs analytics."""
    
    def __init__(self):
        self.latest_env = {}
        self.latest_air = {}
        self.latest_motion = {}
        self.combined_data = {}
        self._loop = None
    
    def set_event_loop(self, loop):
        """Set asyncio event loop for alert dispatching."""
        self._loop = loop
    
    def process_environment(self, topic: str, data: Dict):
        """Process environment sensor data (DHT11, MQ-2, MQ-7)."""
        self.latest_env = data
        self._update_combined()
        
        # Save to MongoDB
        db.save_sensor_data("environment", data)
        
        # Check thresholds and generate alerts
        self._check_env_alerts(data)
        
        # Run anomaly detection
        self._run_anomaly_check()
    
    def process_air_quality(self, topic: str, data: Dict):
        """Process air quality data (APM10)."""
        self.latest_air = data
        self._update_combined()
        
        # Save to MongoDB
        db.save_sensor_data("air_quality", data)
        
        # Check PM thresholds
        self._check_air_alerts(data)
    
    def process_motion(self, topic: str, data: Dict):
        """Process motion sensor data (PIR, Piezo)."""
        self.latest_motion = data
        
        # Save to MongoDB
        db.save_sensor_data("motion", data)
        
        # Check alerts
        if data.get("vibration_alert"):
            self._dispatch_alert(
                "vibration",
                f"Rung động bất thường! Mức: {data.get('vibration', 'N/A')}",
                "warning",
                data
            )
    
    def process_alert(self, topic: str, data: Dict):
        """Process alert events from ESP32."""
        alert_type = data.get("type", "unknown")
        message = data.get("message", "Alert received from device")
        severity = data.get("severity", "warning")
        device = data.get("device", "esp32")
        
        # Save to MongoDB
        db.save_alert(alert_type, message, severity, device, data)
        
        # Forward to notification channels
        self._dispatch_alert(alert_type, message, severity, data)
    
    def process_door_event(self, topic: str, data: Dict):
        """Process door access events."""
        event = data.get("event", "")
        name = data.get("name", "")
        
        if event == "access_granted":
            db.save_access_log(name, "granted")
            self._dispatch_alert("access", f"{name} đã vào phòng", "info")
        
        elif event == "unknown_face":
            db.save_access_log("Unknown", "denied")
            self._dispatch_alert(
                "security",
                "Người lạ được phát hiện tại cửa!",
                "danger"
            )
        
        elif event == "auto_locked":
            db.save_access_log("system", "auto_locked")
    
    def _update_combined(self):
        """Update combined sensor data for anomaly detection."""
        self.combined_data = {
            **self.latest_env,
            **self.latest_air,
            "vibration": self.latest_motion.get("vibration", 0)
        }
    
    def _check_env_alerts(self, data: Dict):
        """Check environment data against thresholds."""
        temp = data.get("temperature", 0)
        humidity = data.get("humidity", 0)
        gas_raw = data.get("gas_raw", 0)
        co_raw = data.get("co_raw", 0)
        
        # Temperature
        if temp >= Config.TEMP_HIGH:
            self._dispatch_alert(
                "temperature",
                f"Nhiệt độ nguy hiểm: {temp}°C!",
                "critical",
                {"temperature": temp}
            )
        elif temp >= Config.TEMP_WARNING:
            self._dispatch_alert(
                "temperature",
                f"Nhiệt độ cao: {temp}°C",
                "warning",
                {"temperature": temp}
            )
        
        # Gas
        if gas_raw >= Config.GAS_THRESHOLD:
            self._dispatch_alert(
                "gas",
                f"Phát hiện khí gas nguy hiểm! Mức: {gas_raw}",
                "critical",
                {"gas_raw": gas_raw, "gas_ppm": data.get("gas_ppm", 0)}
            )
        
        # CO
        if co_raw >= Config.CO_THRESHOLD:
            self._dispatch_alert(
                "co",
                f"Phát hiện khí CO nguy hiểm! Mức: {co_raw}",
                "critical",
                {"co_raw": co_raw, "co_ppm": data.get("co_ppm", 0)}
            )
    
    def _check_air_alerts(self, data: Dict):
        """Check air quality against thresholds."""
        pm25 = data.get("pm2_5", data.get("pm25", 0))
        pm10 = data.get("pm10", 0)
        
        if pm25 >= Config.PM25_THRESHOLD:
            self._dispatch_alert(
                "air_quality",
                f"Bụi mịn PM2.5 cao: {pm25} µg/m³",
                "warning",
                {"pm2_5": pm25, "pm10": pm10}
            )
        
        if pm10 >= Config.PM10_THRESHOLD:
            self._dispatch_alert(
                "air_quality",
                f"Bụi PM10 cao: {pm10} µg/m³",
                "warning",
                {"pm10": pm10}
            )
    
    def _run_anomaly_check(self):
        """Run anomaly detection on combined data."""
        if not self.combined_data:
            return
        
        result = anomaly_detector.predict(self.combined_data)
        
        if result.get("is_anomaly"):
            severity = result.get("severity", "warning")
            features = result.get("abnormal_features", [])
            
            feature_text = ", ".join([f["feature"] for f in features]) if features else "multiple"
            
            # Save anomaly
            db.save_anomaly(
                "sensor_anomaly",
                f"Bất thường phát hiện ở: {feature_text}",
                severity,
                result.get("reconstruction_error", 0),
                self.combined_data
            )
            
            self._dispatch_alert(
                "anomaly",
                f"AI phát hiện bất thường: {feature_text}",
                severity,
                result
            )
    
    def _dispatch_alert(self, alert_type: str, message: str, severity: str, 
                        data: Dict = None):
        """Dispatch alert through the alert service."""
        if self._loop:
            asyncio.run_coroutine_threadsafe(
                alert_service.send_alert(alert_type, message, severity, data=data),
                self._loop
            )
        else:
            logger.warning(f"Alert not dispatched (no event loop): {message}")
    
    def get_latest_data(self) -> Dict:
        """Get all latest sensor data."""
        return {
            "environment": self.latest_env,
            "air_quality": self.latest_air,
            "motion": self.latest_motion,
            "combined": self.combined_data,
            "timestamp": datetime.utcnow().isoformat()
        }


# Singleton
data_processor = DataProcessor()
