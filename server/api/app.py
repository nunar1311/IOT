"""
============================================
IoT Smart Room - FastAPI Web Server
REST API + WebSocket for Dashboard
============================================
"""

import os
import json
import asyncio
import logging
from datetime import datetime
from typing import Dict, List, Set

from fastapi import FastAPI, WebSocket, WebSocketDisconnect, UploadFile, File, HTTPException
from fastapi.staticfiles import StaticFiles
from fastapi.middleware.cors import CORSMiddleware
from fastapi.responses import HTMLResponse, JSONResponse, Response

from config import Config
from database import db
from mqtt_client import mqtt_client
from models.face_recognizer import face_recognizer
from models.anomaly_detector import anomaly_detector
from services.alert_service import alert_service

logger = logging.getLogger(__name__)

# ============================================
# FastAPI App
# ============================================
app = FastAPI(
    title="IoT Smart Room API",
    description="API for IoT Smart Room monitoring and control",
    version="1.0.0"
)

# CORS
app.add_middleware(
    CORSMiddleware,
    allow_origins=["*"],
    allow_credentials=True,
    allow_methods=["*"],
    allow_headers=["*"],
)

# Static files (Dashboard)
dashboard_path = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", "dashboard"))
if os.path.exists(dashboard_path):
    app.mount("/static", StaticFiles(directory=dashboard_path), name="static")
    
    css_path = os.path.join(dashboard_path, "css")
    js_path = os.path.join(dashboard_path, "js")
    assets_path = os.path.join(dashboard_path, "assets")
    
    if os.path.exists(css_path):
        app.mount("/css", StaticFiles(directory=css_path), name="css")
    if os.path.exists(js_path):
        app.mount("/js", StaticFiles(directory=js_path), name="js")
    if os.path.exists(assets_path):
        app.mount("/assets", StaticFiles(directory=assets_path), name="assets")

# ============================================
# WebSocket Manager
# ============================================
class WSManager:
    """Manages WebSocket connections for real-time updates."""
    
    def __init__(self):
        self.active_connections: Set[WebSocket] = set()
        self._loop = None
        
    def set_loop(self, loop):
        self._loop = loop
    
    async def connect(self, websocket: WebSocket):
        await websocket.accept()
        self.active_connections.add(websocket)
        logger.info(f"WebSocket client connected ({len(self.active_connections)} total)")
    
    def disconnect(self, websocket: WebSocket):
        self.active_connections.discard(websocket)
        logger.info(f"WebSocket client disconnected ({len(self.active_connections)} total)")
    
    async def broadcast(self, event_type: str, data: Dict):
        """Broadcast data to all connected WebSocket clients."""
        if not self.active_connections:
            return
        
        message = json.dumps({
            "event": event_type,
            "data": data,
            "timestamp": datetime.utcnow().isoformat()
        })
        
        disconnected = set()
        for ws in self.active_connections:
            try:
                await ws.send_text(message)
            except Exception:
                disconnected.add(ws)
        
        self.active_connections -= disconnected
    
    def broadcast_sync(self, event_type: str, data: Dict):
        """Synchronous broadcast wrapper for MQTT callbacks."""
        loop_to_use = self._loop
        if not loop_to_use:
            try:
                loop_to_use = asyncio.get_event_loop()
            except RuntimeError:
                pass
                
        if loop_to_use and loop_to_use.is_running():
            asyncio.run_coroutine_threadsafe(
                self.broadcast(event_type, data), loop_to_use
            )

ws_manager = WSManager()

# ============================================
# MQTT → WebSocket Bridge
# ============================================
def mqtt_to_ws(topic: str, data: Dict):
    """Forward MQTT messages to WebSocket clients."""
    event_map = {
        Config.TOPICS["env"]: "sensor_env",
        Config.TOPICS["air"]: "sensor_air",
        Config.TOPICS["motion"]: "sensor_motion",
        Config.TOPICS["status"]: "device_status",
        Config.TOPICS["alert"]: "alert",
        Config.TOPICS["door_status"]: "door_event",
        Config.TOPICS["cam_status"]: "cam_status",
    }
    
    event_type = event_map.get(topic, "unknown")
    ws_manager.broadcast_sync(event_type, data)

# ============================================
# Dashboard Route
# ============================================
@app.get("/", response_class=HTMLResponse)
async def serve_dashboard():
    """Serve the main dashboard page."""
    index_path = os.path.join(dashboard_path, "index.html")
    if os.path.exists(index_path):
        with open(index_path, "r", encoding="utf-8") as f:
            return HTMLResponse(content=f.read())
    return HTMLResponse(content="<h1>IoT Smart Room</h1><p>Dashboard not found. Place files in /dashboard/</p>")

# ============================================
# WebSocket Endpoint
# ============================================
@app.websocket("/ws/realtime")
async def websocket_endpoint(websocket: WebSocket):
    """WebSocket endpoint for real-time data streaming."""
    await ws_manager.connect(websocket)
    
    # Send current state on connect
    try:
        latest = mqtt_client.get_all_latest()
        
        # Map topic keys to friendly names the dashboard expects
        friendly_data = {}
        topic_key_map = {
            Config.TOPICS["env"]: "environment",
            Config.TOPICS["air"]: "air_quality",
            Config.TOPICS["motion"]: "motion",
            Config.TOPICS["status"]: "device_status",
            Config.TOPICS["cam_status"]: "cam_status",
            Config.TOPICS["door_status"]: "door_event",
        }
        
        for topic_key, friendly_name in topic_key_map.items():
            if topic_key in latest and isinstance(latest[topic_key], dict):
                friendly_data[friendly_name] = latest[topic_key]
        
        # Fallback to database if no MQTT data available
        if "environment" not in friendly_data:
            db_env = db.get_latest_sensor_data("environment")
            if db_env:
                friendly_data["environment"] = db_env
        if "air_quality" not in friendly_data:
            db_air = db.get_latest_sensor_data("air_quality")
            if db_air:
                friendly_data["air_quality"] = db_air
        
        logger.info(f"📡 Sending initial_state with keys: {list(friendly_data.keys())}")
        
        await websocket.send_text(json.dumps({
            "event": "initial_state",
            "data": friendly_data,
            "timestamp": datetime.utcnow().isoformat()
        }, default=str))
    except Exception as e:
        logger.error(f"Error sending initial state: {e}")
    
    try:
        while True:
            # Keep connection alive, handle incoming commands
            data = await websocket.receive_text()
            try:
                cmd = json.loads(data)
                await handle_ws_command(cmd)
            except json.JSONDecodeError:
                pass
    except WebSocketDisconnect:
        ws_manager.disconnect(websocket)

async def handle_ws_command(cmd: Dict):
    """Handle commands received via WebSocket."""
    action = cmd.get("action", "")
    
    if action == "control_relay":
        relay_id = cmd.get("relay_id")
        state = cmd.get("state")
        mqtt_client.publish(Config.TOPICS["control_relay"], {
            "relay_id": relay_id,
            "state": state
        })
        
        # Artificially update memory state
        status_topic = Config.TOPICS["status"]
        current = mqtt_client.get_latest(status_topic) or {}
        current[f"relay{relay_id}"] = state
        mqtt_client._latest_data[status_topic] = current
        ws_manager.broadcast_sync("device_status", current)
    elif action == "control_motor":
        mqtt_client.publish(Config.TOPICS["control_motor"], {
            "action": cmd.get("motor_action", "stop")
        })
    elif action == "control_led":
        mode = cmd.get("mode", "dim")
        mqtt_client.publish(Config.TOPICS["control_led"], {
            "mode": mode
        })
        
        # Artificially update memory state
        status_topic = Config.TOPICS["status"]
        current = mqtt_client.get_latest(status_topic) or {}
        current["ledMode"] = mode
        mqtt_client._latest_data[status_topic] = current
        ws_manager.broadcast_sync("device_status", current)

# ============================================
# REST API: Sensors
# ============================================
@app.get("/api/sensors/latest")
async def get_latest_sensors():
    """Get latest sensor readings. Falls back to database if MQTT cache is empty."""
    env = mqtt_client.get_latest(Config.TOPICS["env"])
    air = mqtt_client.get_latest(Config.TOPICS["air"])
    motion = mqtt_client.get_latest(Config.TOPICS["motion"])
    status = mqtt_client.get_latest(Config.TOPICS["status"])
    
    # Fallback to database if MQTT cache is empty (ESP32 hasn't sent data since server start)
    if not env:
        env = db.get_latest_sensor_data("environment")
    if not air:
        air = db.get_latest_sensor_data("air_quality")
    if not motion:
        motion = db.get_latest_sensor_data("motion")
    
    return {
        "environment": env,
        "air_quality": air,
        "motion": motion,
        "status": status
    }

@app.get("/api/sensors/history")
async def get_sensor_history(sensor_type: str = None, hours: int = 24):
    """Get sensor data history."""
    return db.get_sensor_history(sensor_type, hours)

# ============================================
# REST API: Alerts
# ============================================
@app.get("/api/alerts")
async def get_alerts(limit: int = 50, unresolved: bool = False):
    """Get recent alerts."""
    return db.get_alerts(limit, unresolved)

@app.post("/api/alerts/{alert_id}/resolve")
async def resolve_alert(alert_id: str):
    """Resolve an alert."""
    db.resolve_alert(alert_id)
    return {"success": True}

# ============================================
# REST API: Face Recognition
# ============================================
@app.post("/api/faces/register")
async def register_face(name: str, file: UploadFile = File(...)):
    """Register a new face."""
    image_data = await file.read()
    result = face_recognizer.register_face(name, image_data, db)
    
    if not result["success"]:
        raise HTTPException(status_code=400, detail=result["error"])
    
    return result

@app.get("/api/faces/list")
async def list_faces():
    """List all registered faces."""
    faces = db.get_all_faces()
    return [
        {
            "name": f["name"],
            "created_at": f.get("created_at"),
            "has_image": f.get("has_image", False)
        }
        for f in faces
    ]

@app.get("/api/faces/{name}/image")
async def get_face_image(name: str):
    """Serve face image stored in MongoDB."""
    image_bytes = db.get_face_image(name)
    if image_bytes is None:
        # Fallback: try reading from disk
        import os
        from config import Config
        disk_path = os.path.join(Config.FACES_DIR, f"{name}.jpg")
        if os.path.exists(disk_path):
            with open(disk_path, "rb") as f:
                image_bytes = f.read()
        else:
            raise HTTPException(status_code=404, detail="Image not found")
    return Response(content=image_bytes, media_type="image/jpeg")

@app.get("/api/faces/stats")
async def face_stats():
    """Get face recognition statistics."""
    return face_recognizer.get_stats()

@app.delete("/api/faces/{name}")
async def delete_face(name: str):
    """Delete a registered face."""
    db.delete_face(name)
    face_recognizer.load_faces_from_db(db)
    return {"success": True}

# ============================================
# REST API: Access Log
# ============================================
@app.get("/api/access-log")
async def get_access_log(limit: int = 50):
    """Get door access log."""
    return db.get_access_log(limit)

# ============================================
# REST API: Anomaly Detection
# ============================================
@app.get("/api/anomalies")
async def get_anomalies(limit: int = 50):
    """Get detected anomalies."""
    return db.get_anomalies(limit)

@app.get("/api/anomalies/stats")
async def anomaly_stats():
    """Get anomaly detection statistics."""
    return anomaly_detector.get_stats()

# ============================================
# REST API: Controls
# ============================================
@app.post("/api/control/relay")
async def control_relay(relay_id: int, state: bool):
    """Control a relay."""
    mqtt_client.publish(Config.TOPICS["control_relay"], {
        "relay_id": relay_id,
        "state": state
    })
    
    # Artificially update memory state
    status_topic = Config.TOPICS["status"]
    current = mqtt_client.get_latest(status_topic) or {}
    current[f"relay{relay_id}"] = state
    mqtt_client._latest_data[status_topic] = current
    ws_manager.broadcast_sync("device_status", current)
    
    return {"success": True, "relay_id": relay_id, "state": state}

@app.post("/api/control/motor")
async def control_motor(action: str):
    """Control stepper motor (open/close door)."""
    if action not in ("open", "close", "stop"):
        raise HTTPException(status_code=400, detail="Action must be open, close, or stop")
    
    mqtt_client.publish(Config.TOPICS["control_motor"], {"action": action})
    return {"success": True, "action": action}

@app.post("/api/control/led")
async def control_led(mode: str):
    """Control LED mode."""
    valid_modes = ("off", "dim", "bright", "blink", "pulse")
    if mode not in valid_modes:
        raise HTTPException(status_code=400, detail=f"Mode must be one of: {valid_modes}")
    
    mqtt_client.publish(Config.TOPICS["control_led"], {"mode": mode})
    
    # Artificially update memory state
    status_topic = Config.TOPICS["status"]
    current = mqtt_client.get_latest(status_topic) or {}
    current["ledMode"] = mode
    mqtt_client._latest_data[status_topic] = current
    ws_manager.broadcast_sync("device_status", current)
    
    return {"success": True, "mode": mode}

@app.post("/api/control/door")
async def control_door(command: str, name: str = "Dashboard"):
    """Manually control door lock."""
    mqtt_client.publish(Config.TOPICS["door_command"], {
        "command": command.upper(),
        "name": name
    })
    return {"success": True, "command": command}

# ============================================
# REST API: System
# ============================================
@app.get("/api/stats")
async def get_system_stats():
    """Get system statistics."""
    return {
        "database": db.get_stats(),
        "anomaly_detector": anomaly_detector.get_stats(),
        "face_recognizer": face_recognizer.get_stats(),
        "mqtt_connected": mqtt_client.is_connected,
        "ws_clients": len(ws_manager.active_connections),
        "server_time": datetime.utcnow().isoformat()
    }

@app.get("/api/health")
async def health_check():
    """Health check endpoint."""
    return {
        "status": "ok",
        "mqtt": mqtt_client.is_connected,
        "timestamp": datetime.utcnow().isoformat()
    }
