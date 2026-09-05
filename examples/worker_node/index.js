const { AprClient } = require('../../sdk/nodejs');

async function main() {
  // Worker node without HTTP endpoint (endpoint: null)
  const client = new AprClient({
    mqttUrl: process.env.APR_MQTT_URL || 'mqtt://127.0.0.1:1883',
    httpUrl: process.env.APR_HTTP_URL || 'http://127.0.0.1:8080',
    role: 'background-worker',
    workers: ['image-processor', 'email-sender'],
    endpoint: null,
    accessKey: process.env.APR_ACCESS_KEY || 'lightapr_secret_key'
  });

  client.on('nodeStatusChanged', (node) => {
    console.log(`[Worker Node] Topology event: Node ${node.id} (${node.role}) -> ${node.status}`);
  });

  try {
    await client.start();
    console.log(`[Worker Node] Registered to LightAPR as role 'background-worker' (endpoint: null)`);

    // Periodically resolve HTTP nodes and trigger app events
    setInterval(async () => {
      const resolved = client.resolveNode('api-service', 'auth');
      if (resolved) {
        console.log(`[Worker Node] Resolved 'api-service' endpoint via local DB:`, resolved.endpoint);

        // Send app event to api-service
        await client.publishAppEvent('api-service', 'auth', {
          jobId: Math.floor(Math.random() * 1000),
          action: 'validate_token',
          timestamp: Date.now()
        });
      } else {
        console.log(`[Worker Node] No active 'api-service' node found in local registry.`);
      }
    }, 5000);
  } catch (err) {
    console.error(`[Worker Node] Error starting LightAPR client:`, err);
  }

  process.on('SIGINT', async () => {
    console.log(`[Worker Node] Shutting down...`);
    await client.stop();
    process.exit(0);
  });
}

main();
