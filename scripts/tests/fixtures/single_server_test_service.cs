// Installer lifecycle fixture only. Never package this executable for customers.
using System;
using System.IO;
using System.ServiceProcess;
using System.Text.RegularExpressions;

internal sealed class SingleServerTestService : ServiceBase
{
    private SingleServerTestService(string serviceName)
    {
        ServiceName = serviceName;
        CanStop = true;
    }

    private static void Main(string[] arguments)
    {
        string executableName = Path.GetFileNameWithoutExtension(
            System.Diagnostics.Process.GetCurrentProcess().MainModule.FileName);
        string serviceName;
        if (executableName == "px_console")
        {
            serviceName = "Pixels.Console";
        }
        else if (executableName == "px_relay")
        {
            serviceName = "Pixels.Relay";
        }
        else if (executableName == "px_backup" && arguments.Length == 2 && arguments[0] == "service")
        {
            string configuration = File.ReadAllText(arguments[1]);
            Match deploymentMatch = Regex.Match(configuration,
                "\\\"deployment_id\\\"\\s*:\\s*\\\"([0-9a-fA-F-]{36})\\\"");
            if (!deploymentMatch.Success)
            {
                throw new InvalidOperationException("Backup deployment ID is missing.");
            }
            serviceName = "Pixels.Backup." + deploymentMatch.Groups[1].Value.Replace("-", "")
                .Substring(0, 12).ToLowerInvariant();
        }
        else
        {
            throw new InvalidOperationException("Unexpected installer test service command.");
        }
        ServiceBase.Run(new SingleServerTestService(serviceName));
    }
}
