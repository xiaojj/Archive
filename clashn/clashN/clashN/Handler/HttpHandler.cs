﻿using System.IO;
using System.Net.Sockets;
using System.Text;
using System.Windows;
using clashN.Mode;
using clashN.Properties;
using static System.Net.WebRequestMethods;
using File = System.IO.File;

namespace clashN.Handler;

public class HttpHandler
{
    private static TcpListener _tcpListener;
    private static string _pacText;
    private static bool _isRunning;

    public static void Start(Config config)
    {
        var path = Path.Combine(Utils.GetConfigPath(), "pac.txt");
        if (!File.Exists(path))
        {
            File.AppendAllText(path, Resources.ResourceManager.GetString("pac"));
        }

        _pacText = File.ReadAllText(path).Replace("__PROXY__", $"PROXY 127.0.0.1:{config.httpPort};DIRECT;");
        Stop();
        _tcpListener = TcpListener.Create(config.PacPort);
        _isRunning = true;
        _tcpListener.Start();
        Task.Factory.StartNew(() =>
        {
            while (_isRunning)
            {
                try
                {
                    if (!_tcpListener.Pending())
                    {
                        Thread.Sleep(10);
                        continue;
                    }

                    var client = _tcpListener.AcceptTcpClient();
                    Task.Factory.StartNew(() =>
                    {
                        var stream = client.GetStream();
                        var sb = new StringBuilder();
                        sb.AppendLine("HTTP/1.0 200 OK");
                        sb.AppendLine("Content-type:application/x-ns-proxy-autoconfig");
                        sb.AppendLine("Connection:close");
                        sb.AppendLine("Content-Length:" + Encoding.UTF8.GetByteCount(_pacText));
                        sb.AppendLine();
                        sb.Append(_pacText);
                        var content = Encoding.UTF8.GetBytes(sb.ToString());
                        stream.Write(content, 0, content.Length);
                        stream.Flush();
                    });
                }
                catch (Exception e)
                {
                    
                }
                
            }
            
        });
    }

    public static void Stop()
    {
        if (_tcpListener != null)
        {
            try
            {
                _isRunning = false;
                _tcpListener.Stop();
            }
            catch (Exception e)
            {
            }
            
        }
    }
}