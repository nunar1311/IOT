"""
============================================
IoT Smart Room - Anomaly Detection Module
TensorFlow Autoencoder + PyTorch LSTM
============================================
"""

import os
import logging
import numpy as np
from datetime import datetime
from typing import Dict, List, Optional, Tuple
from collections import deque

from config import Config

logger = logging.getLogger(__name__)

# ============================================
# Feature names for the model
# ============================================
FEATURE_NAMES = ["temperature", "humidity", "gas_ppm", "co_ppm", "pm2_5", "pm10", "vibration"]
NUM_FEATURES = len(FEATURE_NAMES)


class AnomalyDetector:
    """AI-based anomaly detection using Autoencoder (TensorFlow)."""
    
    def __init__(self):
        self.model = None
        self.scaler_mean = None
        self.scaler_std = None
        self.threshold = Config.ANOMALY_THRESHOLD
        self.data_buffer = deque(maxlen=10000)  # Store recent normal data
        self.is_trained = False
        self._callbacks = []
        
        # Stats
        self.total_predictions = 0
        self.total_anomalies = 0
        self.recent_errors = deque(maxlen=100)
    
    def build_model(self):
        """Build TensorFlow Autoencoder model."""
        try:
            import tensorflow as tf
            from tensorflow import keras
            from tensorflow.keras import layers
            
            # Autoencoder architecture
            encoder_input = keras.Input(shape=(NUM_FEATURES,), name="encoder_input")
            x = layers.Dense(64, activation="relu", name="enc_dense1")(encoder_input)
            x = layers.BatchNormalization()(x)
            x = layers.Dropout(0.2)(x)
            x = layers.Dense(32, activation="relu", name="enc_dense2")(x)
            x = layers.BatchNormalization()(x)
            x = layers.Dense(16, activation="relu", name="enc_dense3")(x)
            
            # Latent space
            latent = layers.Dense(8, activation="relu", name="latent")(x)
            
            # Decoder
            x = layers.Dense(16, activation="relu", name="dec_dense1")(latent)
            x = layers.BatchNormalization()(x)
            x = layers.Dense(32, activation="relu", name="dec_dense2")(x)
            x = layers.BatchNormalization()(x)
            x = layers.Dropout(0.2)(x)
            x = layers.Dense(64, activation="relu", name="dec_dense3")(x)
            decoder_output = layers.Dense(NUM_FEATURES, activation="sigmoid", name="decoder_output")(x)
            
            self.model = keras.Model(encoder_input, decoder_output, name="anomaly_autoencoder")
            self.model.compile(
                optimizer=keras.optimizers.Adam(learning_rate=0.001),
                loss="mse"
            )
            
            logger.info("🧠 Autoencoder model built successfully")
            logger.info(f"   Parameters: {self.model.count_params():,}")
            return True
            
        except ImportError:
            logger.warning("TensorFlow not available, using statistical fallback")
            return False
    
    def train(self, data: np.ndarray, epochs: int = 100, validation_split: float = 0.1):
        """Train the anomaly detection model on normal data.
        
        Args:
            data: numpy array of shape (n_samples, NUM_FEATURES)
            epochs: number of training epochs
            validation_split: fraction of data for validation
        """
        if data.shape[0] < 100:
            logger.warning("Not enough data to train (need at least 100 samples)")
            return
        
        # Normalize
        self.scaler_mean = data.mean(axis=0)
        self.scaler_std = data.std(axis=0)
        self.scaler_std[self.scaler_std == 0] = 1  # Avoid division by zero
        
        normalized = (data - self.scaler_mean) / self.scaler_std
        
        # Clip to [0, 1] for sigmoid output
        normalized = np.clip(normalized, 0, 1)
        
        if self.model is None:
            if not self.build_model():
                return
        
        import tensorflow as tf
        
        history = self.model.fit(
            normalized, normalized,
            epochs=epochs,
            batch_size=32,
            validation_split=validation_split,
            callbacks=[
                tf.keras.callbacks.EarlyStopping(patience=10, restore_best_weights=True),
                tf.keras.callbacks.ReduceLROnPlateau(factor=0.5, patience=5)
            ],
            verbose=1
        )
        
        # Calculate threshold from training data
        predictions = self.model.predict(normalized, verbose=0)
        errors = np.mean(np.square(normalized - predictions), axis=1)
        self.threshold = np.percentile(errors, 95)  # 95th percentile
        
        logger.info(f"✅ Model trained! Threshold: {self.threshold:.6f}")
        logger.info(f"   Final loss: {history.history['loss'][-1]:.6f}")
        
        # Save model
        self.save_model()
        self.is_trained = True
    
    def predict(self, data: Dict) -> Dict:
        """Predict if a single sensor reading is anomalous.
        
        Args:
            data: dict with sensor readings
            
        Returns:
            dict with is_anomaly, reconstruction_error, details
        """
        self.total_predictions += 1
        
        # Extract features
        features = self._extract_features(data)
        if features is None:
            return {"is_anomaly": False, "error": "insufficient_data"}
        
        # Add to buffer for future training
        self.data_buffer.append(features)
        
        # If model is trained, use it
        if self.is_trained and self.model is not None:
            return self._predict_with_model(features, data)
        
        # Fallback: statistical anomaly detection
        return self._predict_statistical(features, data)
    
    def _predict_with_model(self, features: np.ndarray, raw_data: Dict) -> Dict:
        """Predict using trained autoencoder."""
        # Normalize
        normalized = (features - self.scaler_mean) / self.scaler_std
        normalized = np.clip(normalized, 0, 1).reshape(1, -1)
        
        # Predict
        reconstruction = self.model.predict(normalized, verbose=0)
        error = float(np.mean(np.square(normalized - reconstruction)))
        
        self.recent_errors.append(error)
        
        is_anomaly = error > self.threshold
        
        if is_anomaly:
            self.total_anomalies += 1
            
            # Determine which features are abnormal
            feature_errors = np.square(normalized[0] - reconstruction[0])
            abnormal_features = []
            for i, (name, err) in enumerate(zip(FEATURE_NAMES, feature_errors)):
                if err > self.threshold * 2:
                    abnormal_features.append({
                        "feature": name,
                        "error": float(err),
                        "value": float(features[i])
                    })
            
            # Notify callbacks
            for cb in self._callbacks:
                try:
                    cb(error, abnormal_features, raw_data)
                except Exception as e:
                    logger.error(f"Anomaly callback error: {e}")
        
        return {
            "is_anomaly": is_anomaly,
            "reconstruction_error": error,
            "threshold": self.threshold,
            "severity": self._get_severity(error),
            "abnormal_features": abnormal_features if is_anomaly else [],
            "timestamp": datetime.utcnow().isoformat()
        }
    
    def _predict_statistical(self, features: np.ndarray, raw_data: Dict) -> Dict:
        """Statistical fallback when model is not trained."""
        if len(self.data_buffer) < 50:
            return {
                "is_anomaly": False,
                "method": "statistical",
                "note": "Collecting data... ({}/50)".format(len(self.data_buffer))
            }
        
        # Calculate z-scores
        buffer_array = np.array(list(self.data_buffer))
        mean = buffer_array.mean(axis=0)
        std = buffer_array.std(axis=0)
        std[std == 0] = 1
        
        z_scores = np.abs((features - mean) / std)
        max_z = float(np.max(z_scores))
        
        is_anomaly = max_z > 3.0  # 3-sigma rule
        
        if is_anomaly:
            self.total_anomalies += 1
        
        abnormal_features = []
        for i, (name, z) in enumerate(zip(FEATURE_NAMES, z_scores)):
            if z > 3.0:
                abnormal_features.append({
                    "feature": name,
                    "z_score": float(z),
                    "value": float(features[i])
                })
        
        return {
            "is_anomaly": is_anomaly,
            "method": "statistical",
            "max_z_score": max_z,
            "severity": "danger" if max_z > 5 else "warning" if max_z > 3 else "normal",
            "abnormal_features": abnormal_features,
            "timestamp": datetime.utcnow().isoformat()
        }
    
    def _extract_features(self, data: Dict) -> Optional[np.ndarray]:
        """Extract feature vector from sensor data dict."""
        try:
            features = np.array([
                data.get("temperature", 0),
                data.get("humidity", 0),
                data.get("gas_ppm", data.get("gas_raw", 0)),
                data.get("co_ppm", data.get("co_raw", 0)),
                data.get("pm2_5", data.get("pm25", 0)),
                data.get("pm10", 0),
                data.get("vibration", data.get("vibration_level", 0))
            ], dtype=np.float32)
            return features
        except (ValueError, TypeError) as e:
            logger.error(f"Feature extraction error: {e}")
            return None
    
    def _get_severity(self, error: float) -> str:
        """Get anomaly severity based on reconstruction error."""
        if error > self.threshold * 5:
            return "critical"
        elif error > self.threshold * 3:
            return "danger"
        elif error > self.threshold:
            return "warning"
        return "normal"
    
    def on_anomaly(self, callback):
        """Register callback for anomaly detection."""
        self._callbacks.append(callback)
    
    def save_model(self):
        """Save trained model to disk."""
        if self.model is None:
            return
        
        os.makedirs(os.path.dirname(Config.ANOMALY_MODEL_PATH), exist_ok=True)
        self.model.save(Config.ANOMALY_MODEL_PATH)
        
        # Save scaler params
        scaler_path = Config.ANOMALY_MODEL_PATH.replace(".h5", "_scaler.npz")
        np.savez(scaler_path, mean=self.scaler_mean, std=self.scaler_std, 
                 threshold=np.array([self.threshold]))
        
        logger.info(f"💾 Model saved to {Config.ANOMALY_MODEL_PATH}")
    
    def load_model(self):
        """Load trained model from disk."""
        if not os.path.exists(Config.ANOMALY_MODEL_PATH):
            logger.info("No pre-trained model found, will train when enough data collected")
            return False
        
        try:
            import tensorflow as tf
            self.model = tf.keras.models.load_model(Config.ANOMALY_MODEL_PATH)
            
            scaler_path = Config.ANOMALY_MODEL_PATH.replace(".h5", "_scaler.npz")
            if os.path.exists(scaler_path):
                scaler = np.load(scaler_path)
                self.scaler_mean = scaler["mean"]
                self.scaler_std = scaler["std"]
                self.threshold = float(scaler["threshold"][0])
            
            self.is_trained = True
            logger.info(f"✅ Model loaded from {Config.ANOMALY_MODEL_PATH}")
            return True
            
        except Exception as e:
            logger.error(f"❌ Model load error: {e}")
            return False
    
    def get_stats(self) -> Dict:
        """Get anomaly detection statistics."""
        return {
            "is_trained": self.is_trained,
            "total_predictions": self.total_predictions,
            "total_anomalies": self.total_anomalies,
            "threshold": self.threshold,
            "buffer_size": len(self.data_buffer),
            "recent_avg_error": float(np.mean(list(self.recent_errors))) if self.recent_errors else 0,
            "model_type": "autoencoder" if self.model else "statistical"
        }


# Singleton
anomaly_detector = AnomalyDetector()
