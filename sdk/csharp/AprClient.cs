using System;
using System.Collections.Concurrent;
using System.Collections.Generic;
using System.Linq;
using System.Net.Http;
using System.Text;
using System.Text.Json;
using System.Text.Json.Serialization;
using System.Threading.Tasks;
using MQTTnet;
using MQTTnet.Client;

namespace Apr.Sdk
{
    public class EndpointInfo
    {
        [JsonPropertyName("addr")]
        public string Addr { get; set; } = "127.0.0.1";

        [JsonPropertyName("port")]
        public ushort Port { get; set; } = 80;

        [JsonPropertyName("scheme")]
        public string Scheme { get; set; } = "http";
    }

    public class NodeInfo
    {
        [JsonPropertyName("id")]
        public string Id { get; set; } = string.Empty;

        [JsonPropertyName("role")]
        public string Role { get; set; } = string.Empty;

        [JsonPropertyName("workers")]
        public List<string> Workers { get; set; } = new();

        [JsonPropertyName("endpoint")]
        public EndpointInfo? Endpoint { get; set; }

        [JsonPropertyName("status")]
        public string Status { get; set; } = "OK";

        [JsonPropertyName("added_at")]
        public long AddedAt { get; set; }

        [JsonPropertyName("active_at")]
        public long ActiveAt { get; set; }

        [JsonPropertyName("expires_in")]
        public long? ExpiresIn { get; set; }
    }

    public class AprClientOptions
    {
        public string MqttUrl { get; set; } = Environment.GetEnvironmentVariable("APR_MQTT_URL") ?? "mqtt://127.0.0.1:1883";
        public string HttpUrl { get; set; } = Environment.GetEnvironmentVariable("APR_HTTP_URL") ?? "http://127.0.0.1:8080";
        public string Role { get; set; } = Environment.GetEnvironmentVariable("APR_ROLE") ?? "default-role";
        public List<string> Workers { get; set; } = new();
        public EndpointInfo? Endpoint { get; set; }
        public string AccessKey { get; set; } = Environment.GetEnvironmentVariable("APR_ACCESS_KEY") ?? "lightapr_secret_key";
    }

    public class RegistryResponse
    {
        [JsonPropertyName("total")]
        public int Total { get; set; }

        [JsonPropertyName("nodes")]
        public List<NodeInfo> Nodes { get; set; } = new();
    }

    public class AprClient
    {
        private readonly AprClientOptions _options;
        private readonly HttpClient _httpClient;
        private IMqttClient? _mqttClient;
        private readonly ConcurrentDictionary<string, NodeInfo> _localNodes = new();
        private readonly ConcurrentBag<NodeInfo> _syncQueue = new();
        private readonly ConcurrentDictionary<string, List<Action<string, string>>> _appCallbacks = new();
        private readonly ConcurrentDictionary<string, int> _rrIndices = new();
        private bool _isReconciled;

        public event Action<NodeInfo>? NodeStatusChanged;

        public AprClient(AprClientOptions options)
        {
            _options = options;
            _httpClient = new HttpClient();
        }

        public async Task StartAsync()
        {
            int randSuffix = Random.Shared.Next(1000, 9999);
            string username = $"{_options.Role}_{randSuffix}";
            string password = $"{_options.AccessKey}{username}";
            string clientId = $"{_options.Role}_cs_{randSuffix}";

            var factory = new MqttFactory();
            _mqttClient = factory.CreateMqttClient();

            var builder = new MqttClientOptionsBuilder()
                .WithClientId(clientId)
                .WithCredentials(username, password)
                .WithCleanSession();

            Uri mqttUri = new Uri(_options.MqttUrl.StartsWith("mqtt://") ? _options.MqttUrl.Replace("mqtt://", "tcp://") : _options.MqttUrl);
            if (mqttUri.Scheme.Equals("ws", StringComparison.OrdinalIgnoreCase) || mqttUri.Scheme.Equals("wss", StringComparison.OrdinalIgnoreCase))
            {
                builder.WithWebSocketServer(o => o.WithUri(mqttUri.ToString()));
            }
            else
            {
                builder.WithTcpServer(mqttUri.Host, mqttUri.Port > 0 ? mqttUri.Port : 1883);
            }

            var options = builder.Build();

            _mqttClient.ApplicationMessageReceivedAsync += e =>
            {
                string topic = e.ApplicationMessage.Topic;
                string payload = Encoding.UTF8.GetString(e.ApplicationMessage.PayloadSegment);
                HandleIncomingPublish(topic, payload);
                return Task.CompletedTask;
            };

            await _mqttClient.ConnectAsync(options);
            await _mqttClient.SubscribeAsync("apr/+");

            await PublishMetadataAsync();
            await FetchSnapshotAsync();
            ReconcileQueue();
        }

        public async Task StopAsync()
        {
            if (_mqttClient != null)
            {
                await _mqttClient.DisconnectAsync();
                _mqttClient.Dispose();
                _mqttClient = null;
            }
        }

        public NodeInfo? ResolveNode(string role, string? worker = null)
        {
            var candidates = _localNodes.Values
                .Where(n => n.Status == "OK" && n.Role == role)
                .Where(n => string.IsNullOrEmpty(worker) || (n.Workers != null && n.Workers.Contains(worker)))
                .ToList();

            if (candidates.Count == 0) return null;

            string key = $"{role}:{worker}";
            int idx = _rrIndices.AddOrUpdate(key, 0, (_, current) => (current + 1) % candidates.Count);
            return candidates[idx % candidates.Count];
        }

        public async Task<NodeInfo?> ResolveNodeRemoteAsync(string role, string? worker = null)
        {
            string query = $"role={Uri.EscapeDataString(role)}";
            if (!string.IsNullOrEmpty(worker)) query += $"&worker={Uri.EscapeDataString(worker)}";

            try
            {
                var response = await _httpClient.GetAsync($"{_options.HttpUrl}/resolve?{query}");
                if (response.IsSuccessStatusCode)
                {
                    string json = await response.Content.ReadAsStringAsync();
                    return JsonSerializer.Deserialize<NodeInfo>(json);
                }
            }
            catch { }
            return null;
        }

        public List<NodeInfo> GetLocalRegistry() => _localNodes.Values.ToList();

        public async Task PublishAppEventAsync(string targetRole, string? worker, object payload)
        {
            string topic = string.IsNullOrEmpty(worker) ? $"app/{targetRole}" : $"app/{targetRole}/{worker}";
            string payloadStr = payload is string s ? s : JsonSerializer.Serialize(payload);
            if (_mqttClient != null && _mqttClient.IsConnected)
            {
                var message = new MqttApplicationMessageBuilder()
                    .WithTopic(topic)
                    .WithPayload(payloadStr)
                    .Build();
                await _mqttClient.PublishAsync(message);
            }
        }

        public void SubscribeAppEvent(string targetRole, string? worker, Action<string, string> callback)
        {
            string topic = string.IsNullOrEmpty(worker) ? $"app/{targetRole}" : $"app/{targetRole}/{worker}";
            _appCallbacks.AddOrUpdate(topic, new List<Action<string, string>> { callback }, (_, list) =>
            {
                list.Add(callback);
                return list;
            });
            if (_mqttClient != null && _mqttClient.IsConnected)
            {
                _ = _mqttClient.SubscribeAsync(topic);
            }
        }

        private async Task PublishMetadataAsync()
        {
            var payload = new
            {
                role = _options.Role,
                workers = _options.Workers,
                endpoint = _options.Endpoint
            };
            string json = JsonSerializer.Serialize(payload);
            if (_mqttClient != null && _mqttClient.IsConnected)
            {
                var message = new MqttApplicationMessageBuilder()
                    .WithTopic("apr/node/meta")
                    .WithPayload(json)
                    .Build();
                await _mqttClient.PublishAsync(message);
            }
        }

        private async Task FetchSnapshotAsync()
        {
            try
            {
                var response = await _httpClient.GetAsync($"{_options.HttpUrl}/registry?count=1000");
                if (response.IsSuccessStatusCode)
                {
                    string json = await response.Content.ReadAsStringAsync();
                    var snapshot = JsonSerializer.Deserialize<RegistryResponse>(json);
                    if (snapshot?.Nodes != null)
                    {
                        foreach (var node in snapshot.Nodes)
                        {
                            _localNodes[node.Id] = node;
                        }
                    }
                }
            }
            catch { }
        }

        private void ReconcileQueue()
        {
            while (_syncQueue.TryTake(out var node))
            {
                ApplyNodeEvent(node);
            }
            _isReconciled = true;
        }

        private void ApplyNodeEvent(NodeInfo node)
        {
            if (node.Status == "OK")
            {
                _localNodes[node.Id] = node;
            }
            else if (node.Status == "GRACE")
            {
                if (_localNodes.TryGetValue(node.Id, out var existing))
                {
                    existing.Status = "GRACE";
                    existing.ExpiresIn = node.ExpiresIn ?? 180;
                }
            }
            else if (node.Status == "ERASED")
            {
                _localNodes.TryRemove(node.Id, out _);
            }
            NodeStatusChanged?.Invoke(node);
        }

        private void HandleIncomingPublish(string topic, string payload)
        {
            if (topic.StartsWith("apr/"))
            {
                try
                {
                    var node = JsonSerializer.Deserialize<NodeInfo>(payload);
                    if (node != null)
                    {
                        if (!_isReconciled)
                        {
                            _syncQueue.Add(node);
                        }
                        else
                        {
                            ApplyNodeEvent(node);
                        }
                    }
                }
                catch { }
            }
            else if (topic.StartsWith("app/"))
            {
                if (_appCallbacks.TryGetValue(topic, out var callbacks))
                {
                    foreach (var cb in callbacks)
                    {
                        cb(payload, topic);
                    }
                }
            }
        }
    }
}
