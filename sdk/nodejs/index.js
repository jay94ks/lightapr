const mqtt = require('mqtt');
const { EventEmitter } = require('events');

// Canonical APR connection procedure (mirrored - deliberately, not
// accidentally - across sdk/nodejs, sdk/ts, sdk/csharp, and sdk/cpp):
//   1. Open the MQTT connection (username "{role}_{suffix}", password
//      "{accessKey}{username}").
//   2. Subscribe to "apr/+" so topology events start queuing immediately.
//   3. Publish "apr/node/meta" with { role, workers, endpoint }.
//   4. Fetch a full registry snapshot over HTTP (GET /registry) to seed
//      local state without waiting for a slow trickle of individual events.
//   5. Drain the events that queued during steps 2-4 (`syncQueue`) on top of
//      the snapshot, then switch to applying further events live.
// Any SDK client should be recognizable against this sequence; if you're
// implementing a 5th language binding, follow the same order.
class AprClient extends EventEmitter {
  constructor(options = {}) {
    super();
    this.mqttUrl = options.mqttUrl || process.env.APR_MQTT_URL || 'mqtt://127.0.0.1:1883';
    this.httpUrl = options.httpUrl || process.env.APR_HTTP_URL || 'http://127.0.0.1:8080';
    this.role = options.role || process.env.APR_ROLE || 'default-role';
    this.workers = options.workers || [];
    this.endpoint = options.endpoint !== undefined ? options.endpoint : null;
    this.accessKey = options.accessKey || process.env.APR_ACCESS_KEY || 'lightapr_secret_key';

    this.client = null;
    this.localNodes = new Map();
    this.syncQueue = [];
    this.isReconciled = false;
    this.rrIndices = new Map();
  }

  async start() {
    const randomSuffix = Math.floor(Math.random() * 10000);
    const username = `${this.role}_${randomSuffix}`;
    const password = `${this.accessKey}${username}`;
    const clientId = `${this.role}_node_${randomSuffix}`;

    return new Promise((resolve, reject) => {
      this.client = mqtt.connect(this.mqttUrl, {
        clientId,
        username,
        password,
        reconnectPeriod: 2000
      });

      this.client.on('connect', async () => {
        try {
          this.client.subscribe('apr/+');

          // 1. Publish metadata
          this.client.publish('apr/node/meta', JSON.stringify({
            role: this.role,
            workers: this.workers,
            endpoint: this.endpoint
          }));

          // 2. Fetch snapshot
          await this._fetchSnapshot();

          // 3. Reconcile queue
          this._reconcileQueue();

          resolve();
        } catch (err) {
          reject(err);
        }
      });

      this.client.on('message', (topic, payload) => {
        this._handleMessage(topic, payload.toString());
      });

      this.client.on('error', (err) => {
        this.emit('error', err);
      });

      this.client.on('close', () => {
        this.emit('close');
      });
    });
  }

  async stop() {
    if (this.client) {
      this.client.end();
      this.client = null;
    }
  }

  resolveNode(role, worker = null) {
    const candidates = Array.from(this.localNodes.values()).filter(node => {
      if (node.status !== 'OK' || node.role !== role) return false;
      if (worker && (!node.workers || !node.workers.includes(worker))) return false;
      return true;
    });

    if (candidates.length === 0) return null;

    const key = `${role}:${worker || ''}`;
    const idx = (this.rrIndices.get(key) || 0) % candidates.length;
    this.rrIndices.set(key, (idx + 1) % candidates.length);
    return candidates[idx];
  }

  async resolveNodeRemote(role, worker = null) {
    let query = `role=${encodeURIComponent(role)}`;
    if (worker) query += `&worker=${encodeURIComponent(worker)}`;
    try {
      const res = await fetch(`${this.httpUrl}/resolve?${query}`);
      if (res.ok) return await res.json();
    } catch (err) {
      this.emit('error', err);
    }
    return null;
  }

  publishAppEvent(targetRole, worker, payload) {
    const topic = worker ? `app/${targetRole}/${worker}` : `app/${targetRole}`;
    const payloadStr = typeof payload === 'object' ? JSON.stringify(payload) : String(payload);
    if (this.client) {
      this.client.publish(topic, payloadStr);
    }
  }

  subscribeAppEvent(targetRole, worker, callback) {
    const topic = worker ? `app/${targetRole}/${worker}` : `app/${targetRole}`;
    if (this.client) {
      this.client.subscribe(topic);
    }
    this.on(`app_event:${topic}`, callback);
  }

  getLocalRegistry() {
    return Array.from(this.localNodes.values());
  }

  async _fetchSnapshot() {
    try {
      const res = await fetch(`${this.httpUrl}/registry?count=1000`);
      if (res.ok) {
        const data = await res.json();
        if (data.nodes && Array.isArray(data.nodes)) {
          for (const node of data.nodes) {
            this.localNodes.set(node.id, node);
          }
        }
      }
    } catch (err) {
      this.emit('error', err);
    }
  }

  _reconcileQueue() {
    for (const msg of this.syncQueue) {
      this._applyNodeEvent(msg);
    }
    this.syncQueue = [];
    this.isReconciled = true;
  }

  _applyNodeEvent(nodeInfo) {
    if (nodeInfo.status === 'OK') {
      this.localNodes.set(nodeInfo.id, nodeInfo);
    } else if (nodeInfo.status === 'GRACE') {
      const existing = this.localNodes.get(nodeInfo.id);
      if (existing) {
        existing.status = 'GRACE';
        existing.expires_in = nodeInfo.expires_in || 180;
      }
    } else if (nodeInfo.status === 'ERASED') {
      this.localNodes.delete(nodeInfo.id);
    }
    this.emit('nodeStatusChanged', nodeInfo);
  }

  _handleMessage(topic, payloadStr) {
    if (topic.startsWith('apr/')) {
      try {
        const nodeInfo = JSON.parse(payloadStr);
        if (!this.isReconciled) {
          this.syncQueue.push(nodeInfo);
        } else {
          this._applyNodeEvent(nodeInfo);
        }
      } catch (err) {
        this.emit('error', new Error(`Failed to parse ${topic} payload: ${err.message}`));
      }
    } else if (topic.startsWith('app/')) {
      let data = payloadStr;
      try { data = JSON.parse(payloadStr); } catch (e) {}
      this.emit(`app_event:${topic}`, data, topic);
    }
  }
}

module.exports = { AprClient };
