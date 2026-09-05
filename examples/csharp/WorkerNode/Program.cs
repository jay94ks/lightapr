using System;
using System.Threading.Tasks;
using Apr.Sdk;

class Program
{
    static async Task Main(string[] args)
    {
        var options = new AprClientOptions
        {
            MqttUrl = Environment.GetEnvironmentVariable("APR_MQTT_URL") ?? "mqtt://127.0.0.1:1883",
            HttpUrl = Environment.GetEnvironmentVariable("APR_HTTP_URL") ?? "http://127.0.0.1:8080",
            Role = "cs-worker",
            Workers = new() { "batch-processor" },
            Endpoint = null, // No HTTP endpoint
            AccessKey = Environment.GetEnvironmentVariable("APR_ACCESS_KEY") ?? "lightapr_secret_key"
        };

        var client = new AprClient(options);
        client.NodeStatusChanged += (node) =>
        {
            Console.WriteLine($"[C# WorkerNode] Topology event: {node.Id} ({node.Role}) -> {node.Status}");
        };

        await client.StartAsync();
        Console.WriteLine($"[C# WorkerNode] Registered to LightAPR as role 'cs-worker' (endpoint: null, MQTT: {options.MqttUrl})");

        for (int i = 0; i < 3; i++)
        {
            await Task.Delay(2000);
            var resolved = client.ResolveNode("cs-api-service", "auth");
            if (resolved != null)
            {
                Console.WriteLine($"[C# WorkerNode] Resolved 'cs-api-service' -> Endpoint Port: {resolved.Endpoint?.Port}");
                await client.PublishAppEventAsync("cs-api-service", "auth", new { job = "sync_user", time = DateTime.UtcNow });
            }
            else
            {
                Console.WriteLine("[C# WorkerNode] 'cs-api-service' not found in local registry");
            }
        }

        await client.StopAsync();
    }
}
