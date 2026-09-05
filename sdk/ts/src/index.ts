import * as mqtt from 'mqtt';
import { EventEmitter } from 'events';

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

export interface EndpointInfo {
  addr: string;
  port: number;
  scheme: string;
}

export type NodeStatus = 'OK' | 'GRACE' | 'ERASED';

export interface NodeInfo {
  id: string;
  role: string;
  workers: string[];
  endpoint: EndpointInfo | null;
  status: NodeStatus;
  added_at?: number;
  active_at?: number;
  expires_in?: number | null;
}

export interface AprClientOptions {
  mqttUrl?: string;
  httpUrl?: string;
  role?: string;
  workers?: string[];
  endpoint?: EndpointInfo | null;
  accessKey?: string;
}

export type AppEventCallback = (payload: any, topic: string) => void;

export class AprClient extends EventEmitter {
  private mqttUrl: string;
  private httpUrl: string;
  private role: string;
  private workers: string[];
  private endpoint: EndpointInfo | null;
  private accessKey: string;

  private client: mqtt.MqttClient | null = null;
  private localNodes: Map<string, NodeInfo> = new Map();
  private syncQueue: NodeInfo[] = [];
  private isReconciled = false;
  private rrIndices: Map<string, number> = new Map();

  constructor(options: AprClientOptions = {}) {
    super();
    this.mqttUrl = options.mqttUrl || process.env.APR_MQTT_URL || 'mqtt://127.0.0.1:1883';
    this.httpUrl = options.httpUrl || process.env.APR_HTTP_URL || 'http://127.0.0.1:8080';
    this.role = options.role || process.env.APR_ROLE || 'default-role';
    this.workers = options.workers || [];
    this.endpoint = options.endpoint !== undefined ? options.endpoint : null;
    this.accessKey = options.accessKey || process.env.APR_ACCESS_KEY || 'lightapr_secret_key';
  }

  public async start(): Promise<void> {
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
          this.client!.subscribe('apr/+');

          this.client!.publish('apr/node/meta', JSON.stringify({
            role: this.role,
            workers: this.workers,
            endpoint: this.endpoint
          }));

          await this.fetchSnapshot();
          this.reconcileQueue();

          resolve();
        } catch (err) {
          reject(err);
        }
      });

      this.client.on('message', (topic: string, payload: Buffer) => {
        this.handleMessage(topic, payload.toString());
      });

      this.client.on('error', (err: Error) => {
        this.emit('error', err);
      });

      this.client.on('close', () => {
        this.emit('close');
      });
    });
  }

  public async stop(): Promise<void> {
    if (this.client) {
      this.client.end();
      this.client = null;
    }
  }

  public resolveNode(role: string, worker: string | null = null): NodeInfo | null {
    const candidates: NodeInfo[] = [];
    for (const node of this.localNodes.values()) {
      if (node.status !== 'OK' || node.role !== role) continue;
      if (worker && (!node.workers || !node.workers.includes(worker))) continue;
      candidates.push(node);
    }

    if (candidates.length === 0) return null;

    const key = `${role}:${worker || ''}`;
    const idx = (this.rrIndices.get(key) || 0) % candidates.length;
    this.rrIndices.set(key, (idx + 1) % candidates.length);
    return candidates[idx];
  }

  public async resolveNodeRemote(role: string, worker: string | null = null): Promise<NodeInfo | null> {
    let query = `role=${encodeURIComponent(role)}`;
    if (worker) query += `&worker=${encodeURIComponent(worker)}`;
    try {
      const res = await fetch(`${this.httpUrl}/resolve?${query}`);
      if (res.ok) return (await res.json()) as NodeInfo;
    } catch (err) {
      this.emit('error', err as Error);
    }
    return null;
  }

  public publishAppEvent(targetRole: string, worker: string | null, payload: any): void {
    const topic = worker ? `app/${targetRole}/${worker}` : `app/${targetRole}`;
    const payloadStr = typeof payload === 'object' ? JSON.stringify(payload) : String(payload);
    if (this.client) {
      this.client.publish(topic, payloadStr);
    }
  }

  public subscribeAppEvent(targetRole: string, worker: string | null, callback: AppEventCallback): void {
    const topic = worker ? `app/${targetRole}/${worker}` : `app/${targetRole}`;
    if (this.client) {
      this.client.subscribe(topic);
    }
    this.on(`app_event:${topic}`, callback);
  }

  public getLocalRegistry(): NodeInfo[] {
    return Array.from(this.localNodes.values());
  }

  private async fetchSnapshot(): Promise<void> {
    try {
      const res = await fetch(`${this.httpUrl}/registry?count=1000`);
      if (res.ok) {
        const data = (await res.json()) as { nodes?: NodeInfo[] };
        if (data.nodes && Array.isArray(data.nodes)) {
          for (const node of data.nodes) {
            this.localNodes.set(node.id, node);
          }
        }
      }
    } catch (err) {
      this.emit('error', err as Error);
    }
  }

  private reconcileQueue(): void {
    for (const msg of this.syncQueue) {
      this.applyNodeEvent(msg);
    }
    this.syncQueue = [];
    this.isReconciled = true;
  }

  private applyNodeEvent(nodeInfo: NodeInfo): void {
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

  private handleMessage(topic: string, payloadStr: string): void {
    if (topic.startsWith('apr/')) {
      try {
        const nodeInfo: NodeInfo = JSON.parse(payloadStr);
        if (!this.isReconciled) {
          this.syncQueue.push(nodeInfo);
        } else {
          this.applyNodeEvent(nodeInfo);
        }
      } catch (err) {
        this.emit('error', new Error(`Failed to parse ${topic} payload: ${(err as Error).message}`));
      }
    } else if (topic.startsWith('app/')) {
      let data: any = payloadStr;
      try { data = JSON.parse(payloadStr); } catch (e) {}
      this.emit(`app_event:${topic}`, data, topic);
    }
  }
}
