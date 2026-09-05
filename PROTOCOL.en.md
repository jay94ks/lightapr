🌐 **Language**: [English Version](https://github.com/jay94ks/lightapr/blob/main/PROTOCOL.en.md) | [Korean Version](https://github.com/jay94ks/lightapr/blob/main/PROTOCOL.md)

---

## MQTT Topic Specifications

** When each node connects to the MQTT server, APR assigns an ID to that node. **

> **Note (WebSocket Connections)**:
> - WebSocket-based MQTT connections (port 8083) support node registration (`apr/node/meta`) and endpoint registration identically to Native TCP.
> - However, web browser clients (such as web monitoring dashboards or web apps) cannot bind listening HTTP server ports due to browser limitations, so browser clients typically register with `"endpoint": null` or operate as event listeners/publishers.

```
TOPIC apr/node/meta
Description:

Topic for each node to register its identity with APR.
All nodes publish only to this topic, and APR receives only.
Both Native TCP and WebSocket MQTT connections support node registration.

{
    "role": <node_role>,
    "workers": [
        "worker_1",
        "worker_2",
        ...
    ],
    
    "endpoint": {
        "addr": "Node IP address", (If omitted, automatically substituted with MQTT peer IP)
        "port": "Node port number", (Default: forced to `80`)
        "scheme": "Endpoint protocol scheme" (Default: forced to `http`)
    } | null
}

(Specify endpoint as null for worker nodes that do not expose an HTTP endpoint)
```

```
TOPIC apr/{role}
Description:

When a node publishes its metadata, APR publishes a message to the {role} topic.
Each node subscribes to this topic, and APR publishes only.

{
    "id": "Node ID",
    "role": <node_role>,
    "workers": [
        "worker_1",
        "worker_2",
        ...
    ],
    
    "endpoint": {
        "addr": "Node IP address",
        "port": "Node port number",
        "scheme": "Endpoint protocol scheme"
    } | null,

    "status": "OK" | "GRACE" | "ERASED"
}
```

```
TOPIC app/{role}
TOPIC app/{role}/{worker}

These two topics are not monitored by APR at all.
They serve as auxiliary channels for asynchronous event delivery between nodes.
```

## HTTP Endpoints
### Health Check & Observability

```
GET /healthz

{
    "status": "UP",
    "cell_id": "<cell_id value configured via ENV or CLI>",
    "uptime": <Uptime in seconds>
}
```

HTTP API for orchestrators (Kubernetes, Docker Swarm, etc.) and monitoring systems to verify APR daemon health and integrate with management mechanisms.

```
GET /status

{
  "nodes": {
    "total": <Total node count (alive + grace)>,
    "alive": <Online node count>,
    "grace": <Grace period node count>
  },
  
  "memory": {
    "total": <Total memory footprint in KB>,
    "meta": <Metadata storage memory in KB>,
    "mqtt": <MQTT protocol memory in KB>,
    "etc": <Other memory in KB>
  },
  
  "roles": {
    <role_key>: <Count of registered nodes for this role>
  },

  "workers": {
    <worker_key>: <Count of registered nodes for this worker>
  }
}
```

### Service Discovery & Routing
```
GET /registry
Query Parameters:

1. `page` and `count`: Page number (1-based) and node count per page.
2. `role`: Specific role filter.
3. `worker`: Specific worker filter.

{
    "total": <Total matched node count>,
    "nodes": [
        {
            "id": "Node ID",
            "role": "Node role",
            
            "endpoint": {
                "addr": "Node IP address",
                "port": "Node port number",
                "scheme": "Endpoint protocol scheme"
            } | null,

            "added_at": <Unix timestamp when node registered>,
            "active_at": <Unix timestamp of node's last activity>
        }
    ]
}
```

Queries all servers registered in the internal registry.

```
GET /registry/{id}

{
    "id": "Node ID",
    "role": "Node role",
    "workers": [
        "Worker 1 registered by node",
        "Worker 2 registered by node",
        ...
    ],

    "endpoint": {
        "addr": "Node IP address",
        "port": "Node port number",
        "scheme": "Endpoint protocol scheme"
    } | null,

    "added_at": <Unix timestamp when node registered>,
    "active_at": <Unix timestamp of node's last activity>,
    "expires_in": null (connection alive) or <Remaining grace time in seconds>
}
```

### HTTP Response-based Load Balancing
```
GET /resolve
Query Parameters:
1. role: Required role (Required)
2. worker: Required worker (Optional)

{
    "id": "Selected node ID",
    "endpoint": {
        "addr": "Node IP address",
        "port": "Node port number",
        "scheme": "Endpoint protocol scheme"
    } | null
}
```

## Bootstrapping Sequence for New Nodes

1. **[MQTT Connect & Subscribe]**
   Connect to the MQTT broker and immediately subscribe to `apr/+`.
   * All MQTT messages received from this point onward are temporarily stored in a Sync Queue (Buffer) without immediate processing.

2. **[Metadata Publish]**
   Publish role, worker list, and endpoint metadata to `apr/node/meta`.

3. **[Snapshot Fetch]**
   Call HTTP `GET /registry` sequentially to fetch the current cell's full node snapshot and build the initial local in-memory routing database.

4. **[Queue Reconciliation]**
   Reconcile and apply events accumulated in the Sync Queue during step 3 sequentially:
   - `status == "OK"`: Add to database or update to latest info.
   - `status == "GRACE"`: Change node status to `GRACE` (reduce routing weight if necessary).
   - `status == "ERASED"`: Permanently remove node from local database.
   * Once the queue is completely drained, transition to real-time mode to immediately apply incoming `apr/+` messages to the local database.
