param(
    [Parameter(Mandatory = $true)][int]$OwnerProcessId,
    [Parameter(Mandatory = $true)][string]$OutputPath,
    [int]$Seconds = 90,
    [string]$PeerAddress = '',
    [int]$RelayPort = 4605
)

$ErrorActionPreference = 'Stop'
if ($Seconds -lt 1 -or $Seconds -gt 600) { throw 'Duration must be between 1 and 600 seconds' }
if (Test-Path -LiteralPath $OutputPath) { throw 'Output already exists' }

# Diagnostic helper compilation is serial; product builds use the explicit 18-job entry points.
$monitorSource = @'
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Globalization;
using System.IO;
using System.Net;
using System.Runtime.InteropServices;
using System.Threading;

public static class RelayTcpPathMonitor
{
    [StructLayout(LayoutKind.Sequential)]
    private struct TcpRow
    {
        public uint State;
        public uint LocalAddress;
        public uint LocalPort;
        public uint RemoteAddress;
        public uint RemotePort;
    }

    [DllImport("iphlpapi.dll")]
    private static extern uint GetExtendedTcpTable(byte[] connectionTable, ref uint tableSize,
        bool ordered, uint addressFamily, uint tableClass, uint reserved);

    [DllImport("iphlpapi.dll")]
    private static extern uint GetPerTcpConnectionEStats(ref TcpRow connection, uint statisticsType,
        byte[] configuration, uint configurationVersion, uint configurationSize,
        IntPtr staticStatistics, uint staticVersion, uint staticSize,
        byte[] dynamicStatistics, uint dynamicVersion, uint dynamicSize);

    [DllImport("iphlpapi.dll")]
    private static extern uint SetPerTcpConnectionEStats(ref TcpRow connection, uint statisticsType,
        byte[] configuration, uint configurationVersion, uint configurationSize, uint offset);

    private sealed class ObservedConnection : IDisposable
    {
        public TcpRow Connection;
        public string Identity;
        private readonly HashSet<uint> enabledTypes = new HashSet<uint>();

        public ObservedConnection(TcpRow connection, string identity)
        {
            Connection = connection;
            Identity = identity;
        }

        public uint Read(uint statisticsType, byte[] measurements)
        {
            byte[] configuration = new byte[1];
            uint outcome = GetPerTcpConnectionEStats(ref Connection, statisticsType,
                configuration, 0, 1, IntPtr.Zero, 0, 0, measurements, 0, (uint)measurements.Length);
            if (outcome != 0) return outcome;
            if (configuration[0] == 0)
            {
                configuration[0] = 1;
                outcome = SetPerTcpConnectionEStats(ref Connection, statisticsType, configuration, 0, 1, 0);
                if (outcome != 0) return outcome;
                enabledTypes.Add(statisticsType);
                outcome = GetPerTcpConnectionEStats(ref Connection, statisticsType,
                    configuration, 0, 1, IntPtr.Zero, 0, 0, measurements, 0, (uint)measurements.Length);
            }
            return outcome == 0 && configuration[0] == 0 ? 50U : outcome;
        }

        public void Dispose()
        {
            foreach (uint statisticsType in enabledTypes)
            {
                byte[] configuration = new byte[1];
                uint outcome = SetPerTcpConnectionEStats(ref Connection, statisticsType, configuration, 0, 1, 0);
                // ERROR_NOT_FOUND is expected when the observed connection has already closed.
                if (outcome != 0 && outcome != 1168)
                    Console.Error.WriteLine("restore_estats flow=" + Identity + " type=" + statisticsType + " error=" + outcome);
            }
            enabledTypes.Clear();
        }
    }

    private static uint ReadUInt(byte[] buffer, int offset) { return BitConverter.ToUInt32(buffer, offset); }
    private static int HostPort(uint networkPort) { return (int)(((networkPort & 255) << 8) | ((networkPort >> 8) & 255)); }
    private static string Address(uint networkAddress) { return new IPAddress(BitConverter.GetBytes(networkAddress)).ToString(); }

    public static void Monitor(int ownerProcessId, int seconds, string outputPath, string peerAddress, int relayPort)
    {
        var observed = new Dictionary<string, ObservedConnection>();
        var connectionTable = new byte[65536];
        var elapsed = Stopwatch.StartNew();
        try
        {
            using (var output = new StreamWriter(outputPath, false))
            {
                output.WriteLine("utc,elapsed_ms,flow,path_error,send_error,receive_error,fast_retrans,timeouts,retrans_packets,retrans_bytes,dup_acks_in,sacks_received,send_stalls,sample_rtt_ms,smoothed_rtt_ms,rto_ms,send_retrans_queue,send_application_queue,receive_dup_acks,receive_reassembly_queue,receive_application_queue");
                while (elapsed.Elapsed.TotalSeconds < seconds)
                {
                    uint tableSize = (uint)connectionTable.Length;
                    uint tableOutcome = GetExtendedTcpTable(connectionTable, ref tableSize, false, 2, 5, 0);
                    if (tableOutcome == 122 && tableSize <= 4 * 1024 * 1024)
                    {
                        connectionTable = new byte[tableSize];
                        continue;
                    }
                    if (tableOutcome != 0) throw new InvalidOperationException("TCP table error=" + tableOutcome);
                    uint connectionCount = ReadUInt(connectionTable, 0);
                    for (uint connectionIndex = 0; connectionIndex < connectionCount; ++connectionIndex)
                    {
                        int rowOffset = 4 + checked((int)connectionIndex) * 24;
                        if (ReadUInt(connectionTable, rowOffset + 20) != (uint)ownerProcessId) continue;
                        var connection = new TcpRow
                        {
                            State = ReadUInt(connectionTable, rowOffset),
                            LocalAddress = ReadUInt(connectionTable, rowOffset + 4),
                            LocalPort = ReadUInt(connectionTable, rowOffset + 8),
                            RemoteAddress = ReadUInt(connectionTable, rowOffset + 12),
                            RemotePort = ReadUInt(connectionTable, rowOffset + 16)
                        };
                        if (connection.State != 5 || (HostPort(connection.LocalPort) != relayPort && HostPort(connection.RemotePort) != relayPort)) continue;
                        string remoteAddress = Address(connection.RemoteAddress);
                        if (peerAddress.Length != 0 && remoteAddress != peerAddress) continue;
                        string identity = Address(connection.LocalAddress) + ":" + HostPort(connection.LocalPort) + "-" + remoteAddress + ":" + HostPort(connection.RemotePort);
                        ObservedConnection flow;
                        if (!observed.TryGetValue(identity, out flow))
                        {
                            flow = new ObservedConnection(connection, identity);
                            observed.Add(identity, flow);
                        }
                        var pathMeasurements = new byte[160];
                        var sendMeasurements = new byte[32];
                        var receiveMeasurements = new byte[72];
                        uint pathOutcome = flow.Read(3, pathMeasurements);
                        uint sendOutcome = flow.Read(4, sendMeasurements);
                        uint receiveOutcome = flow.Read(5, receiveMeasurements);
                        var columns = new List<string>
                        {
                            DateTimeOffset.UtcNow.ToString("o"), elapsed.Elapsed.TotalMilliseconds.ToString("F3", CultureInfo.InvariantCulture), identity,
                            pathOutcome.ToString(), sendOutcome.ToString(), receiveOutcome.ToString()
                        };
                        foreach (int fieldIndex in new[] { 0, 1, 5, 6, 7, 8, 17, 26, 27, 33 })
                            columns.Add(pathOutcome == 0 ? ReadUInt(pathMeasurements, fieldIndex * 4).ToString() : "");
                        columns.Add(sendOutcome == 0 ? BitConverter.ToUInt64(sendMeasurements, 0).ToString() : "");
                        columns.Add(sendOutcome == 0 ? BitConverter.ToUInt64(sendMeasurements, 16).ToString() : "");
                        columns.Add(receiveOutcome == 0 ? ReadUInt(receiveMeasurements, 20).ToString() : "");
                        columns.Add(receiveOutcome == 0 ? ReadUInt(receiveMeasurements, 36).ToString() : "");
                        columns.Add(receiveOutcome == 0 ? BitConverter.ToUInt64(receiveMeasurements, 48).ToString() : "");
                        output.WriteLine(String.Join(",", columns));
                    }
                    output.Flush();
                    Thread.Sleep(100);
                }
            }
        }
        finally
        {
            foreach (ObservedConnection flow in observed.Values) flow.Dispose();
        }
        Console.WriteLine("Observed " + observed.Count + " Relay TCP connections; collection settings restored.");
    }
}
'@

if ($PSVersionTable.PSVersion.Major -ge 7) {
    Add-Type -CompilerOptions '/parallel-' -TypeDefinition $monitorSource
} else {
    $compilerParameters = New-Object System.CodeDom.Compiler.CompilerParameters
    $compilerParameters.CompilerOptions = '/optimize+'
    [void]$compilerParameters.ReferencedAssemblies.Add('System.dll')
    [void]$compilerParameters.ReferencedAssemblies.Add('System.Core.dll')
    Add-Type -CompilerParameters $compilerParameters -TypeDefinition $monitorSource
}

[RelayTcpPathMonitor]::Monitor($OwnerProcessId, $Seconds, $OutputPath, $PeerAddress, $RelayPort)
