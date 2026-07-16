using System.Diagnostics;
using System.Net;
using System.Net.Sockets;
using System.Reflection;
using System.Text;

const int DefaultPort = 721;

var port = ReadPort(args);
var bankDirectory = EnsureBankDirectory();
ExtractEmbeddedBankFiles(bankDirectory);
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
    _ = Task.Run(() => HandleClientAsync(client, bankDirectory));
}

static int ReadPort(string[] args)
{
    var raw = args.FirstOrDefault(item => item.StartsWith("--port=", StringComparison.OrdinalIgnoreCase))?.Split('=', 2)[1]
        ?? Environment.GetEnvironmentVariable("QUIZAPP_PORT")
        ?? DefaultPort.ToString();
    return int.TryParse(raw, out var port) && port is > 0 and < 65536 ? port : DefaultPort;
}

static async Task HandleClientAsync(TcpClient client, string bankDirectory)
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
        if (requestPath == "__quizapp/open-bank-directory")
        {
            var opened = OpenFolder(bankDirectory);
            var text = opened ? $"已打开默认题库文件夹：{RelativeBankPath()}" : $"无法打开默认题库文件夹，请手动打开：{RelativeBankPath()}";
            await WriteResponseAsync(stream, opened ? 200 : 500, "text/plain; charset=utf-8", Encoding.UTF8.GetBytes(text), parts[0] == "HEAD");
            return;
        }

        var body = ReadAsset(requestPath, bankDirectory);
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

static byte[]? ReadAsset(string path, string bankDirectory)
{
    if (string.IsNullOrWhiteSpace(path))
    {
        return null;
    }

    if (path.StartsWith("data/", StringComparison.OrdinalIgnoreCase))
    {
        var relative = path["data/".Length..].Replace('/', Path.DirectorySeparatorChar);
        var physicalPath = Path.GetFullPath(Path.Combine(bankDirectory, relative));
        var bankRoot = Path.GetFullPath(bankDirectory);
        if (physicalPath.StartsWith(bankRoot, StringComparison.OrdinalIgnoreCase) && File.Exists(physicalPath))
        {
            return File.ReadAllBytes(physicalPath);
        }
    }

    var assembly = Assembly.GetExecutingAssembly();
    var resourceName = $"assets/{path}";
    var resource = assembly.GetManifestResourceNames()
        .FirstOrDefault(name => string.Equals(name.Replace('\\', '/'), resourceName, StringComparison.OrdinalIgnoreCase));
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

static string EnsureBankDirectory()
{
    var directory = Path.Combine(AppContext.BaseDirectory, "data");
    Directory.CreateDirectory(directory);
    return directory;
}

static void ExtractEmbeddedBankFiles(string bankDirectory)
{
    var assembly = Assembly.GetExecutingAssembly();
    foreach (var resourceName in assembly.GetManifestResourceNames())
    {
        if (!resourceName.StartsWith("assets/data/", StringComparison.OrdinalIgnoreCase) || !resourceName.EndsWith(".json", StringComparison.OrdinalIgnoreCase))
        {
            continue;
        }

        var fileName = resourceName["assets/data/".Length..];
        var targetPath = Path.Combine(bankDirectory, fileName);
        var targetDirectory = Path.GetDirectoryName(targetPath);
        if (!string.IsNullOrWhiteSpace(targetDirectory))
        {
            Directory.CreateDirectory(targetDirectory);
        }
        if (File.Exists(targetPath))
        {
            continue;
        }

        using var input = assembly.GetManifestResourceStream(resourceName);
        if (input is null)
        {
            continue;
        }
        using var output = File.Create(targetPath);
        input.CopyTo(output);
    }
}

static bool OpenFolder(string directory)
{
    try
    {
        Directory.CreateDirectory(directory);
        Process.Start(new ProcessStartInfo(directory) { UseShellExecute = true });
        return true;
    }
    catch
    {
        return false;
    }
}

static string RelativeBankPath()
{
    return ".\\data";
}
