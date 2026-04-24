"""
============================================
IoT Smart Room - Face Recognition Module
Using OpenCV DNN (YuNet + SFace)
============================================
"""

import os
import cv2
import json
import time
import base64
import logging
import threading
import numpy as np
from datetime import datetime
from typing import List, Dict, Optional, Tuple

logger = logging.getLogger(__name__)

import requests

from config import Config

# Model files paths
CURRENT_DIR = os.path.dirname(os.path.abspath(__file__))
YUNET_MODEL = os.path.join(CURRENT_DIR, "face_detection_yunet_2023mar.onnx")
SFACE_MODEL = os.path.join(CURRENT_DIR, "face_recognition_sface_2021dec.onnx")

class FaceRecognizer:
    """AI-powered face recognition for door access control using OpenCV DNN."""
    
    def __init__(self):
        self.known_faces: List[Dict] = []  # [{name, encoding}]
        self.running = False
        self._thread: Optional[threading.Thread] = None
        self._lock = threading.Lock()
        
        # Callbacks
        self._on_recognized = None
        self._on_unknown = None
        self._on_no_face = None
        
        # Stats
        self.total_checks = 0
        self.total_recognized = 0
        self.total_unknown = 0
        
        # Camera connection state
        self._cam_connected = False
        self._cam_error_logged = False
        self._cam_retry_delay = 2
        self._cam_max_retry_delay = 30
        
        # Ensure faces directory exists
        os.makedirs(Config.FACES_DIR, exist_ok=True)
        
        # Load OpenCV DNN Models
        try:
            self.detector = cv2.FaceDetectorYN.create(
                model=YUNET_MODEL,
                config="",
                input_size=(320, 320),
                score_threshold=0.8,
                nms_threshold=0.3,
                top_k=5000
            )
            self.recognizer = cv2.FaceRecognizerSF.create(
                model=SFACE_MODEL,
                config=""
            )
            self.models_loaded = True
            logger.info("✅ OpenCV YuNet/SFace models loaded successfully")
        except Exception as e:
            self.models_loaded = False
            logger.error(f"❌ Failed to load OpenCV Face models: {e}")
    
    def load_faces_from_db(self, db):
        """Load known face encodings from MongoDB."""
        faces = db.get_all_faces()
        self.known_faces = []
        
        for face in faces:
            if "encoding" in face and face["encoding"]:
                self.known_faces.append({
                    "name": face["name"],
                    "encoding": np.array(face["encoding"], dtype=np.float32)
                })
        
        logger.info(f"📸 Loaded {len(self.known_faces)} known faces from database")
    
    def register_face(self, name: str, image_data: bytes, db=None) -> Dict:
        """Register a new face from image data."""
        try:
            if not self.models_loaded:
                return {"success": False, "error": "OpenCV Face models not loaded (missing .onnx files)"}

            # Decode image
            nparr = np.frombuffer(image_data, np.uint8)
            img = cv2.imdecode(nparr, cv2.IMREAD_COLOR)
            
            if img is None:
                return {"success": False, "error": "Could not decode image"}
                
            # Detect face
            h, w = img.shape[:2]
            self.detector.setInputSize((w, h))
            _, faces = self.detector.detect(img)
            
            if faces is None or len(faces) == 0:
                return {"success": False, "error": "No face found in image"}
            
            if len(faces) > 1:
                return {"success": False, "error": "Multiple faces found. Please upload image with single face."}
            
            # Save image to disk (fallback/cache)
            image_path = os.path.join(Config.FACES_DIR, f"{name}.jpg")
            cv2.imwrite(image_path, img)

            # Encode image to Base64 for MongoDB storage
            _, jpeg_buf = cv2.imencode(".jpg", img, [cv2.IMWRITE_JPEG_QUALITY, 85])
            image_b64 = base64.b64encode(jpeg_buf.tobytes()).decode("utf-8")

            face = faces[0]
            
            # Align and extract feature
            aligned_face = self.recognizer.alignCrop(img, face)
            encoding = self.recognizer.feature(aligned_face) # 1x128 array
            encoding_1d = encoding.flatten()
            
            # Save to database (encoding + image Base64)
            if db:
                db.save_face(name, encoding_1d.tolist(), image_path, image_b64)
            
            # Add to runtime
            with self._lock:
                self.known_faces = [f for f in self.known_faces if f["name"] != name]
                self.known_faces.append({
                    "name": name,
                    "encoding": encoding_1d
                })
            
            logger.info(f"✅ Registered face: {name}")
            return {
                "success": True, 
                "name": name, 
                "image_path": image_path,
                "total_faces": len(self.known_faces)
            }
            
        except Exception as e:
            logger.error(f"❌ Face registration failed: {e}")
            return {"success": False, "error": str(e)}
    
    def recognize_from_frame(self, frame: np.ndarray) -> List[Dict]:
        """Recognize faces in a video frame."""
        if not self.models_loaded:
            return []

        h, w = frame.shape[:2]
        self.detector.setInputSize((w, h))
        
        _, faces = self.detector.detect(frame)
        if faces is None:
            return []
            
        results = []
        
        with self._lock:
            # Create a shallow copy for thread safety while iterating
            known_faces_copy = list(self.known_faces)
            
        for face in faces:
            # bbox is x, y, width, height
            box = face[0:4].astype(np.int32)
            left, top, width, height = box
            right = left + width
            bottom = top + height
            
            aligned_face = self.recognizer.alignCrop(frame, face)
            encoding = self.recognizer.feature(aligned_face)
            
            name = "Unknown"
            confidence = 0.0
            
            for known in known_faces_copy:
                known_enc = known["encoding"].reshape(1, -1)
                # Cosine similarity threshold: >= 0.363 means same person
                score = self.recognizer.match(encoding, known_enc, cv2.FaceRecognizerSF_FR_COSINE)
                if score >= 0.363:
                    if score > confidence:
                        name = known["name"]
                        confidence = float(score)
            
            results.append({
                "name": name,
                "confidence": round(confidence, 3) if name != "Unknown" else 0.0,
                "location": {"top": int(top), "right": int(right), "bottom": int(bottom), "left": int(left)}
            })
            
        return results
    
    def capture_and_recognize(self) -> Tuple[Optional[np.ndarray], List[Dict]]:
        """Capture a frame from ESP32-CAM and recognize faces."""
        try:
            response = requests.get(Config.CAM_CAPTURE_URL, timeout=5)
            if response.status_code != 200:
                return None, []
            
            if not self._cam_connected:
                logger.info(f"📷 ESP32-CAM connected at {Config.CAM_CAPTURE_URL}")
                self._cam_connected = True
                self._cam_error_logged = False
                self._cam_retry_delay = 2
            
            img_array = np.frombuffer(response.content, dtype=np.uint8)
            frame = cv2.imdecode(img_array, cv2.IMREAD_COLOR)
            
            if frame is None:
                return None, []
            
            results = self.recognize_from_frame(frame)
            self.total_checks += 1
            
            return frame, results
            
        except requests.exceptions.RequestException as e:
            if not self._cam_error_logged:
                logger.warning(
                    f"📷 ESP32-CAM unreachable at {Config.CAM_CAPTURE_URL} — "
                    f"retrying every {self._cam_retry_delay}s (error: {type(e).__name__})"
                )
                self._cam_error_logged = True
            self._cam_connected = False
            return None, []
    
    def start_monitoring(self, on_recognized=None, on_unknown=None, on_no_face=None):
        """Start continuous face monitoring in background."""
        self._on_recognized = on_recognized
        self._on_unknown = on_unknown
        self._on_no_face = on_no_face
        self.running = True
        
        self._thread = threading.Thread(target=self._monitor_loop, daemon=True)
        self._thread.start()
        logger.info("🎥 Face recognition monitoring started")
    
    def stop_monitoring(self):
        self.running = False
        if self._thread:
            self._thread.join(timeout=5)
        logger.info("Face recognition monitoring stopped")
    
    def _monitor_loop(self):
        while self.running:
            try:
                frame, results = self.capture_and_recognize()
                
                if frame is not None:
                    if len(results) == 0:
                        if self._on_no_face:
                            self._on_no_face()
                    else:
                        for result in results:
                            if result["name"] != "Unknown":
                                self.total_recognized += 1
                                if self._on_recognized:
                                    self._on_recognized(
                                        result["name"], 
                                        result["confidence"],
                                        frame
                                    )
                            else:
                                self.total_unknown += 1
                                if self._on_unknown:
                                    self._on_unknown(frame, result["confidence"])
                
                if self._cam_connected:
                    time.sleep(Config.FACE_CHECK_INTERVAL)
                else:
                    time.sleep(self._cam_retry_delay)
                    self._cam_retry_delay = min(
                        self._cam_retry_delay * 2,
                        self._cam_max_retry_delay
                    )
                
            except Exception as e:
                logger.error(f"Monitor loop error: {e}")
                time.sleep(5)
    
    def get_stats(self) -> Dict:
        return {
            "known_faces": len(self.known_faces),
            "total_checks": self.total_checks,
            "total_recognized": self.total_recognized,
            "total_unknown": self.total_unknown,
            "monitoring_active": self.running
        }

# Singleton
face_recognizer = FaceRecognizer()
