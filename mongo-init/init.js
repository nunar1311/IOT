// MongoDB Initialization Script
// Creates collections with proper indexes for IoT data

db = db.getSiblingDB('iot_smart_room');

// Time-series collection for sensor data
db.createCollection('sensor_data', {
  timeseries: {
    timeField: 'timestamp',
    metaField: 'sensor_id',
    granularity: 'seconds'
  },
  expireAfterSeconds: 2592000 // 30 days retention
});

// Regular collections
db.createCollection('alerts');
db.createCollection('access_log');
db.createCollection('faces');
db.createCollection('anomalies');
db.createCollection('system_status');

// Indexes
db.alerts.createIndex({ timestamp: -1 });
db.alerts.createIndex({ type: 1, resolved: 1 });
db.access_log.createIndex({ timestamp: -1 });
db.access_log.createIndex({ person_name: 1 });
db.anomalies.createIndex({ timestamp: -1 });
db.anomalies.createIndex({ severity: 1 });
db.system_status.createIndex({ timestamp: -1 });

print('✅ IoT Smart Room database initialized successfully');
