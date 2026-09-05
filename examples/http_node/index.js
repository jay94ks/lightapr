const http = require('http');
const { AprClient } = require('../../sdk/nodejs');

const PORT = process.env.PORT ? parseInt(process.env.PORT, 10) : 3000;

// 1. Start the application's own HTTP server (SDK does not manage endpoint server)
const server = http.createServer((req, res) => {
  if (req.url === '/api/data') {
    res.writeHead(200, { 'Content-Type': 'application/json' });
    res.end(JSON.stringify({ status: 'OK', message: 'Hello from Node.js HTTP Node Endpoint!' }));
  } else {
    res.writeHead(404);
    res.end();
  }
});

server.listen(PORT, '127.0.0.1', async () => {
  console.log(`[HTTP Node] Application HTTP server running on port ${PORT}`);

  // 2. Initialize LightAPR SDK Client with endpoint info
  const client = new AprClient({
    mqttUrl: process.env.APR_MQTT_URL || 'mqtt://127.0.0.1:1883',
    httpUrl: process.env.APR_HTTP_URL || 'http://127.0.0.1:8080',
    role: 'api-service',
    workers: ['auth', 'payment'],
    endpoint: { addr: '127.0.0.1', port: PORT, scheme: 'http' },
    accessKey: process.env.APR_ACCESS_KEY || 'lightapr_secret_key'
  });

  client.on('nodeStatusChanged', (node) => {
    console.log(`[HTTP Node] Topology event received: Node ${node.id} (${node.role}) status is ${node.status}`);
  });

  try {
    await client.start();
    console.log(`[HTTP Node] Registered to LightAPR as role 'api-service' with endpoint port ${PORT}`);

    // Subscribe to application events
    client.subscribeAppEvent('api-service', 'auth', (payload, topic) => {
      console.log(`[HTTP Node] Received app event on ${topic}:`, payload);
    });
  } catch (err) {
    console.error(`[HTTP Node] Error starting LightAPR SDK client:`, err);
  }

  process.on('SIGINT', async () => {
    console.log(`[HTTP Node] Shutting down...`);
    await client.stop();
    server.close();
    process.exit(0);
  });
});
