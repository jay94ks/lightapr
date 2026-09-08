import { AprClient, NodeInfo } from '../../../sdk/ts/src';

async function main() {
  const client = new AprClient({
    mqttUrl: process.env.APR_MQTT_URL || 'mqtt://127.0.0.1:1883',
    httpUrl: process.env.APR_HTTP_URL || 'http://127.0.0.1:8080',
    role: 'ts-worker',
    workers: ['queue-consumer'],
    endpoint: null,
    accessKey: process.env.APR_ACCESS_KEY || 'lightapr_secret_key'
  });

  client.on('nodeStatusChanged', (node: NodeInfo) => {
    console.log(`[TS Worker Node] Topology event: ${node.id} (${node.role}) -> ${node.status}`);
  });

  try {
    await client.start();
    console.log(`[TS Worker Node] Registered to LightAPR as role 'ts-worker' (endpoint: null)`);

    // Announce initial extra data for the "queue-consumer" worker. Unlike
    // role/workers/endpoint, this can be republished at any point during the
    // session - see the periodic update in the interval below.
    client.publishExtra('queue-consumer', { status: 'idle', queueDepth: 0 });

    setInterval(async () => {
      const resolved = client.resolveNode('ts-api-service', 'auth');
      if (resolved) {
        console.log(`[TS Worker Node] Resolved 'ts-api-service':`, resolved.endpoint);
        await client.publishAppEvent('ts-api-service', 'auth', { event: 'ping', time: Date.now() });
      }

      client.publishExtra('queue-consumer', { status: 'busy', queueDepth: Math.floor(Math.random() * 10) });
    }, 5000);
  } catch (err) {
    console.error(`[TS Worker Node] Error:`, err);
  }

  process.on('SIGINT', async () => {
    await client.stop();
    process.exit(0);
  });
}

main();
