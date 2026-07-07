using System.Diagnostics;
using System.Net;
using System.Net.Sockets;
using System.Reflection;
using System.Text;

const int DefaultPort = 721;

var port = ReadPort(args);
var listener = new TcpListener(IPAddress.Loopback, port);
listener.Start();

var url = $"http://127.0.0.1:{port}/";
Console.WriteLine($"QuizApp desktop server: {url}");
Console.WriteLine("Close this window to stop the server.");
if (!string.Equals(Environment.GetEnvironmentVariable("QUIZAPP_NO_BROWSER"), "1", StringComparison.Ordinal))
{
    OpenBrowser(url);
}

while (true)
{
    var client = await listener.AcceptTcpClientAsync();
    _ = Task.Run(() => HandleClientAsync(client));
}

static int ReadPort(string[] args)
{
    var raw = args.FirstOrDefault(item => item.StartsWith("--port=", StringComparison.OrdinalIgnoreCase))?.Split('=', 2)[1]
        ?? Environment.GetEnvironmentVariable("QUIZAPP_PORT")
        ?? DefaultPort.ToString();
    return int.TryParse(raw, out var port) && port is > 0 and < 65536 ? port : DefaultPort;
}

static async Task HandleClientAsync(TcpClient client)
{
    try
    {
        using (client)
        {
        using var stream = client.GetStream();
        using var reader = new StreamReader(stream, Encoding.ASCII, leaveOpen: true);
        var requestLine = await reader.ReadLineAsync() ?? string.Empty;
        if (string.IsNullOrWhiteSpace(requestLine))
        {
            return;
        }

        string? line;
        do
        {
            line = await reader.ReadLineAsync();
        } while (!string.IsNullOrEmpty(line));

        var parts = requestLine.Split(' ', StringSplitOptions.RemoveEmptyEntries);
        if (parts.Length < 2 || (parts[0] != "GET" && parts[0] != "HEAD"))
        {
            await WriteResponseAsync(stream, 405, "text/plain; charset=utf-8", Encoding.UTF8.GetBytes("Method Not Allowed"), parts.ElementAtOrDefault(0) == "HEAD");
            return;
        }

        var requestPath = NormalizeRequestPath(parts[1]);
        var body = ReadAsset(requestPath);
        if (body is null)
        {
            await WriteResponseAsync(stream, 404, "text/plain; charset=utf-8", Encoding.UTF8.GetBytes("Not Found"), parts[0] == "HEAD");
            return;
        }

        await WriteResponseAsync(stream, 200, ContentType(requestPath), body, parts[0] == "HEAD");
        }
    }
    catch
    {
        // Ignore broken client connections; the next request can still be served.
    }
}

static string NormalizeRequestPath(string raw)
{
    var path = raw.Split('?', 2)[0].TrimStart('/');
    path = Uri.UnescapeDataString(path);
    if (string.IsNullOrWhiteSpace(path))
    {
        return "index.html";
    }
    path = path.Replace('\\', '/');
    if (path.Contains("../", StringComparison.Ordinal) || path.StartsWith("..", StringComparison.Ordinal))
    {
        return string.Empty;
    }
    return path;
}

static byte[]? ReadAsset(string path)
{
    if (string.IsNullOrWhiteSpace(path))
    {
        return null;
    }

    var assembly = Assembly.GetExecutingAssembly();
    var resourceName = $"assets/{path}";
    var resource = assembly.GetManifestResourceNames()
        .FirstOrDefault(name => string.Equals(name, resourceName, StringComparison.OrdinalIgnoreCase));
    if (resource is null)
    {
        return null;
    }

    using var stream = assembly.GetManifestResourceStream(resource);
    if (stream is null)
    {
        return null;
    }

    using var memory = new MemoryStream();
    stream.CopyTo(memory);
    return memory.ToArray();
}

static string ContentType(string path)
{
    if (path.EndsWith(".html", StringComparison.OrdinalIgnoreCase))
    {
        return "text/html; charset=utf-8";
    }
    if (path.EndsWith(".json", StringComparison.OrdinalIgnoreCase))
    {
        return "application/json; charset=utf-8";
    }
    if (path.EndsWith(".js", StringComparison.OrdinalIgnoreCase))
    {
        return "text/javascript; charset=utf-8";
    }
    if (path.EndsWith(".css", StringComparison.OrdinalIgnoreCase))
    {
        return "text/css; charset=utf-8";
    }
    return "application/octet-stream";
}

static async Task WriteResponseAsync(Stream stream, int status, string contentType, byte[] body, bool headOnly)
{
    var reason = status switch
    {
        200 => "OK",
        404 => "Not Found",
        405 => "Method Not Allowed",
        _ => "OK"
    };
    var headers = Encoding.ASCII.GetBytes(
        $"HTTP/1.1 {status} {reason}\r\n" +
        $"Content-Type: {contentType}\r\n" +
        $"Content-Length: {body.Length}\r\n" +
        "Cache-Control: no-store\r\n" +
        "Connection: close\r\n\r\n");
    await stream.WriteAsync(headers);
    if (!headOnly)
    {
        await stream.WriteAsync(body);
    }
}

static void OpenBrowser(string url)
{
    try
    {
        Process.Start(new ProcessStartInfo(url) { UseShellExecute = true });
    }
    catch
    {
        Console.WriteLine($"Open this URL manually: {url}");
    }
}
