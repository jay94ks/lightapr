import * as http from 'http';
import { AprClient, NodeInfo } from '../../../sdk/ts/src';

const PORT = process.env.PORT ? parseInt(process.env.PORT, 10) : 3001;

const server = http.createServer((req, res) => {
  if (req.url === '/api/info') {
    res.writeHead(200, { 'Content-Type': 'application/json' });
    res.end(JSON.stringify({ status: 'OK', message: 'Hello from TypeScript HTTP Node!' }));
  } else {
    res.writeHead(404);
    res.end();
  }
});

server.listen(PORT, '127.0.0.1', async () => {
  console.log(`[TS HTTP Node] Server listening on port ${PORT}`);

  const client = new AprClient({
    mqttUrl: process.env.APR_MQTT_URL || 'mqtt://127.0.0.1:1883',
    httpUrl: process.env.APR_HTTP_URL || 'http://127.0.0.1:8080',
    role: 'ts-api-service',
    workers: ['auth', 'billing'],
    endpoint: { addr: '127.0.0.1', port: PORT, scheme: 'http' },
    accessKey: process.env.APR_ACCESS_KEY || 'lightapr_secret_key'
  });

  client.on('nodeStatusChanged', (node: NodeInfo) => {
    console.log(`[TS HTTP Node] Topology event: ${node.id} (${node.role}) -> ${node.status}`);
  });

  try {
    await client.start();
    console.log(`[TS HTTP Node] Registered to LightAPR as role 'ts-api-service'`);

    client.subscribeAppEvent('ts-api-service', 'auth', (payload: any, topic: string) => {
      console.log(`[TS HTTP Node] App event on ${topic}:`, payload);
    });
  } catch (err) {
    console.error(`[TS HTTP Node] Failed to start APR client:`, err);
  }

  process.on('SIGINT', async () => {
    await client.stop();
    server.close();
    process.exit(0);
  });
});
