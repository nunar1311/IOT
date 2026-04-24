"""
============================================
IoT Smart Room - MongoDB Database Module
============================================
"""

import logging
from datetime import datetime, timedelta
from typing import Optional, Dict, List, Any

from pymongo import MongoClient, DESCENDING
from pymongo.errors import ConnectionFailure

from config import Config

logger = logging.getLogger(__name__)


class Database:
    """MongoDB database manager for IoT Smart Room."""
    
    def __init__(self):
        self.client: Optional[MongoClient] = None
        self.db = None
        
    def connect(self):
        """Connect to MongoDB."""
        try:
            self.client = MongoClient(Config.MONGO_URI, serverSelectionTimeoutMS=5000)
            # Test connection
            self.client.admin.command("ping")
            self.db = self.client[Config.MONGO_DB]
            logger.info("✅ Connected to MongoDB successfully")
            self._ensure_collections()
        except ConnectionFailure as e:
            logger.error(f"❌ MongoDB connection failed: {e}")
            # Try without auth for local development
            try:
                self.client = MongoClient(f"mongodb://{Config.MONGO_HOST}:{Config.MONGO_PORT}/")
                self.client.admin.command("ping")
                self.db = self.client[Config.MONGO_DB]
                logger.info("✅ Connected to MongoDB (no auth)")
                self._ensure_collections()
            except Exception as e2:
                logger.error(f"❌ MongoDB connection failed (no auth): {e2}")
                raise
    
    def _ensure_collections(self):
        """Ensure required collections exist with indexes."""
        collections = self.db.list_collection_names()
        
        if "sensor_data" not in collections:
            try:
                self.db.create_collection("sensor_data", timeseries={
                    "timeField": "timestamp",
                    "metaField": "sensor_type",
                    "granularity": "seconds"
                })
                logger.info("Created time-series collection: sensor_data")
            except Exception:
                # Collection might already exist or timeseries not supported
                pass
        
        # Indexes
        self.db.alerts.create_index([("timestamp", DESCENDING)])
        self.db.access_log.create_index([("timestamp", DESCENDING)])
        self.db.anomalies.create_index([("timestamp", DESCENDING)])
        logger.info("Database indexes ensured ✓")
    
    def close(self):
        """Close MongoDB connection."""
        if self.client:
            self.client.close()
            logger.info("MongoDB connection closed")
    
    # ==========================================
    # Sensor Data
    # ==========================================
    def save_sensor_data(self, sensor_type: str, data: Dict[str, Any]):
        """Save sensor reading to time-series collection."""
        # Remove keys that conflict with time-series collection requirements
        clean_data = {k: v for k, v in data.items() 
                      if k not in ("timestamp", "_server_timestamp", "_topic", "sensor_type")}
        doc = {
            "timestamp": datetime.utcnow(),
            "sensor_type": sensor_type,
            "device_millis": data.get("timestamp"),  # preserve ESP32 millis as separate field
            **clean_data
        }
        try:
            self.db.sensor_data.insert_one(doc)
        except Exception as e:
            logger.error(f"Error saving sensor data: {e}")
    
    def get_latest_sensor_data(self, sensor_type: str = None) -> Dict:
        """Get the latest sensor reading."""
        query = {}
        if sensor_type:
            query["sensor_type"] = sensor_type
        
        result = self.db.sensor_data.find_one(
            query, sort=[("timestamp", DESCENDING)]
        )
        if result:
            result["_id"] = str(result["_id"])
        return result
    
    def get_sensor_history(self, sensor_type: str = None, hours: int = 24) -> List[Dict]:
        """Get sensor data history."""
        since = datetime.utcnow() - timedelta(hours=hours)
        query = {"timestamp": {"$gte": since}}
        if sensor_type:
            query["sensor_type"] = sensor_type
        
        results = list(self.db.sensor_data.find(
            query, sort=[("timestamp", DESCENDING)]
        ).limit(5000))
        
        for r in results:
            r["_id"] = str(r["_id"])
            r["timestamp"] = r["timestamp"].isoformat()
        return results
    
    # ==========================================
    # Alerts
    # ==========================================
    def save_alert(self, alert_type: str, message: str, severity: str,
                   device: str = "system", data: Dict = None):
        """Save an alert event."""
        doc = {
            "timestamp": datetime.utcnow(),
            "type": alert_type,
            "message": message,
            "severity": severity,
            "device": device,
            "data": data or {},
            "resolved": False
        }
        result = self.db.alerts.insert_one(doc)
        return str(result.inserted_id)
    
    def get_alerts(self, limit: int = 50, unresolved_only: bool = False) -> List[Dict]:
        """Get recent alerts."""
        query = {}
        if unresolved_only:
            query["resolved"] = False
        
        results = list(self.db.alerts.find(
            query, sort=[("timestamp", DESCENDING)]
        ).limit(limit))
        
        for r in results:
            r["_id"] = str(r["_id"])
            r["timestamp"] = r["timestamp"].isoformat()
        return results
    
    def resolve_alert(self, alert_id: str):
        """Mark an alert as resolved."""
        from bson import ObjectId
        self.db.alerts.update_one(
            {"_id": ObjectId(alert_id)},
            {"$set": {"resolved": True, "resolved_at": datetime.utcnow()}}
        )
    
    # ==========================================
    # Access Log (Face Recognition)
    # ==========================================
    def save_access_log(self, person_name: str, result: str, 
                        confidence: float = 0.0, image_path: str = ""):
        """Save door access log entry."""
        doc = {
            "timestamp": datetime.utcnow(),
            "person_name": person_name,
            "result": result,  # "granted", "denied", "unknown"
            "confidence": confidence,
            "image_path": image_path
        }
        self.db.access_log.insert_one(doc)
    
    def get_access_log(self, limit: int = 50) -> List[Dict]:
        """Get recent access log entries."""
        results = list(self.db.access_log.find(
            sort=[("timestamp", DESCENDING)]
        ).limit(limit))
        
        for r in results:
            r["_id"] = str(r["_id"])
            r["timestamp"] = r["timestamp"].isoformat()
        return results
    
    # ==========================================
    # Anomalies
    # ==========================================
    def save_anomaly(self, anomaly_type: str, description: str,
                     severity: str, reconstruction_error: float,
                     sensor_data: Dict = None):
        """Save detected anomaly."""
        doc = {
            "timestamp": datetime.utcnow(),
            "type": anomaly_type,
            "description": description,
            "severity": severity,
            "reconstruction_error": reconstruction_error,
            "sensor_data": sensor_data or {},
            "acknowledged": False
        }
        result = self.db.anomalies.insert_one(doc)
        return str(result.inserted_id)
    
    def get_anomalies(self, limit: int = 50) -> List[Dict]:
        """Get recent anomalies."""
        results = list(self.db.anomalies.find(
            sort=[("timestamp", DESCENDING)]
        ).limit(limit))
        
        for r in results:
            r["_id"] = str(r["_id"])
            r["timestamp"] = r["timestamp"].isoformat()
        return results
    
    # ==========================================
    # Faces Database
    # ==========================================
    def save_face(self, name: str, encoding: list, image_path: str, image_data: str = None):
        """Save face encoding and image data to database."""
        doc = {
            "name": name,
            "encoding": encoding,
            "image_path": image_path,
            "created_at": datetime.utcnow(),
            "active": True
        }
        if image_data is not None:
            doc["image_data"] = image_data  # Base64 encoded JPEG
        self.db.faces.update_one(
            {"name": name},
            {"$set": doc},
            upsert=True
        )

    def get_face_image(self, name: str) -> Optional[bytes]:
        """Get face image bytes from database by name."""
        import base64
        result = self.db.faces.find_one({"name": name, "active": True}, {"image_data": 1})
        if result and result.get("image_data"):
            try:
                return base64.b64decode(result["image_data"])
            except Exception:
                return None
        return None
    
    def get_all_faces(self) -> List[Dict]:
        """Get all registered face encodings."""
        # Use aggregation to compute has_image without fetching heavy image_data
        results = list(self.db.faces.aggregate([
            {"$match": {"active": True}},
            {"$addFields": {
                # $gt with None returns True if field exists and is not null
                "has_image": {"$gt": ["$image_data", None]}
            }},
            {"$project": {
                "encoding": 1,
                "name": 1,
                "created_at": 1,
                "image_path": 1,
                "has_image": 1
            }}
        ]))
        for r in results:
            r["_id"] = str(r["_id"])
            if "created_at" in r:
                r["created_at"] = r["created_at"].isoformat()
        return results
    
    def delete_face(self, name: str):
        """Soft-delete a face entry."""
        self.db.faces.update_one(
            {"name": name},
            {"$set": {"active": False}}
        )
    
    # ==========================================
    # Statistics
    # ==========================================
    def get_stats(self) -> Dict:
        """Get system statistics."""
        now = datetime.utcnow()
        today = now.replace(hour=0, minute=0, second=0, microsecond=0)
        
        return {
            "total_readings": self.db.sensor_data.count_documents({}),
            "readings_today": self.db.sensor_data.count_documents(
                {"timestamp": {"$gte": today}}
            ),
            "total_alerts": self.db.alerts.count_documents({}),
            "unresolved_alerts": self.db.alerts.count_documents({"resolved": False}),
            "access_attempts_today": self.db.access_log.count_documents(
                {"timestamp": {"$gte": today}}
            ),
            "anomalies_detected": self.db.anomalies.count_documents({}),
            "registered_faces": self.db.faces.count_documents({"active": True})
        }


# Singleton instance
db = Database()
