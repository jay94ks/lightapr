using System;
using System.Net;
using System.Text;
using System.Threading.Tasks;
using Apr.Sdk;

class Program
{
    static async Task Main(string[] args)
    {
        ushort port = 3003;
        if (args.Length > 0 && ushort.TryParse(args[0], out ushort parsedPort))
        {
            port = parsedPort;
        }

        // 1. Start application's own HTTP endpoint listener
        using var httpListener = new HttpListener();
        httpListener.Prefixes.Add($"http://127.0.0.1:{port}/");
        httpListener.Start();
        Console.WriteLine($"[C# HttpNode] Application HTTP listener running on port {port}");

        _ = Task.Run(async () =>
        {
            while (httpListener.IsListening)
            {
                try
                {
                    var context = await httpListener.GetContextAsync();
                    byte[] response = Encoding.UTF8.GetBytes("{\"status\":\"OK\",\"message\":\"Hello from C# HttpNode!\"}");
                    context.Response.ContentType = "application/json";
                    context.Response.OutputStream.Write(response, 0, response.Length);
                    context.Response.Close();
                }
                catch { break; }
            }
        });

        // 2. Initialize APR SDK client
        var options = new AprClientOptions
        {
            MqttUrl = Environment.GetEnvironmentVariable("APR_MQTT_URL") ?? "mqtt://127.0.0.1:1883",
            HttpUrl = Environment.GetEnvironmentVariable("APR_HTTP_URL") ?? "http://127.0.0.1:8080",
            Role = "cs-api-service",
            Workers = new() { "auth", "checkout" },
            Endpoint = new EndpointInfo { Addr = "127.0.0.1", Port = port, Scheme = "http" },
            AccessKey = Environment.GetEnvironmentVariable("APR_ACCESS_KEY") ?? "lightapr_secret_key"
        };

        var client = new AprClient(options);
        client.NodeStatusChanged += (node) =>
        {
            Console.WriteLine($"[C# HttpNode] Topology event: {node.Id} ({node.Role}) -> {node.Status}");
        };

        await client.StartAsync();
        Console.WriteLine($"[C# HttpNode] Registered to LightAPR as role 'cs-api-service' (MQTT: {options.MqttUrl})");

        client.SubscribeAppEvent("cs-api-service", "auth", (payload, topic) =>
        {
            Console.WriteLine($"[C# HttpNode] App event received on {topic}: {payload}");
        });

        await Task.Delay(10000);
        await client.StopAsync();
    }
}
