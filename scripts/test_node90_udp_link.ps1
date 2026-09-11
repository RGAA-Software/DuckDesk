param([ValidateRange(5,15)][int]$StageSeconds=10,[ValidateRange(1024,65535)][int]$Port=4613,
      [ValidateRange(576,1400)][int]$DatagramSize=1400,[switch]$Loopback)
$ErrorActionPreference='Stop'
$repo=Split-Path $PSScriptRoot -Parent
$nonce=[guid]::NewGuid().ToString('N')
$output=Join-Path $repo ('test-results/udp-media-v2/link-'+(Get-Date -Format 'yyyyMMdd-HHmmss'))
New-Item -ItemType Directory -Path $output -Force | Out-Null
$source=@'
using System;
using System.Net;
using System.Net.Sockets;
using System.Diagnostics;
using System.Collections.Generic;
using System.Threading;
using System.Threading.Tasks;
public sealed class PixelsLinkStage {
    public int Mbps;
    public int Packets;
    public double Seconds;
}
public static class PixelsBoundedLinkProbe {
    public static Task<int[]> Receive(string host, int port, string token, int packetSize, CancellationToken cancel) {
        return Task.Run(() => {
            using (var socket = new UdpClient(AddressFamily.InterNetwork)) {
                socket.Client.ReceiveBufferSize = 8 * 1024 * 1024;
                socket.Client.ReceiveTimeout = 200;
                socket.Connect(host, port);
                var hello = System.Text.Encoding.ASCII.GetBytes(token);
                var seen = new HashSet<long>[4];
                for (int i = 0; i < 4; ++i) seen[i] = new HashSet<long>();
                var clock = Stopwatch.StartNew();
                bool started = false;
                double finishAt = 80;
                IPEndPoint peer = new IPEndPoint(IPAddress.Any, 0);
                while (!cancel.IsCancellationRequested && clock.Elapsed.TotalSeconds < finishAt) {
                    if (!started) socket.Send(hello, hello.Length);
                    try {
                        var packet = socket.Receive(ref peer);
                        if (packet.Length != packetSize || System.Text.Encoding.ASCII.GetString(packet, 0, 32) != token) continue;
                        int stage = BitConverter.ToInt32(packet, 32);
                        if (stage == 4) { finishAt = Math.Min(finishAt, clock.Elapsed.TotalSeconds + 1); continue; }
                        if (stage < 0 || stage > 3) continue;
                        started = true;
                        seen[stage].Add(BitConverter.ToInt64(packet, 36));
                    } catch (SocketException e) {
                        if (e.SocketErrorCode != SocketError.TimedOut && e.SocketErrorCode != SocketError.ConnectionReset) throw;
                    }
                }
                var counts = new int[4];
                for (int i = 0; i < 4; ++i) counts[i] = seen[i].Count;
                return counts;
            }
        }, cancel);
    }
    public static PixelsLinkStage[] Send(int port, string token, int seconds, int packetSize) {
        using (var socket = new UdpClient(AddressFamily.InterNetwork)) {
            socket.Client.ExclusiveAddressUse = true;
            socket.Client.SendBufferSize = 1024 * 1024;
            socket.Client.ReceiveTimeout = 1000;
            socket.Client.Bind(new IPEndPoint(IPAddress.Any, port));
            IPEndPoint peer = new IPEndPoint(IPAddress.Any, 0);
            var handshake = Stopwatch.StartNew();
            bool admitted = false;
            while (handshake.Elapsed.TotalSeconds < 10) {
                try {
                    var hello = socket.Receive(ref peer);
                    if (System.Text.Encoding.ASCII.GetString(hello) == token) { admitted = true; break; }
                } catch (SocketException e) { if (e.SocketErrorCode != SocketError.TimedOut) throw; }
            }
            if (!admitted) throw new TimeoutException("No matching probe handshake; no traffic sent");
            socket.Connect(peer);
            int[] rates = { 8, 12, 16, 20 };
            var results = new PixelsLinkStage[4];
            var packet = new byte[packetSize];
            Buffer.BlockCopy(System.Text.Encoding.ASCII.GetBytes(token), 0, packet, 0, 32);
            for (int stage = 0; stage < rates.Length; ++stage) {
                Buffer.BlockCopy(BitConverter.GetBytes(stage), 0, packet, 32, 4);
                var clock = Stopwatch.StartNew();
                long sent = 0;
                // Budget includes Ethernet + IPv4 + UDP headers, not merely the UDP payload.
                double ticksPerPacket = Stopwatch.Frequency * ((packetSize + 42.0) * 8) / (rates[stage] * 1000000.0);
                while (clock.Elapsed.TotalSeconds < seconds) {
                    double due = sent * ticksPerPacket;
                    while (clock.ElapsedTicks < due) Thread.SpinWait(100);
                    if (clock.Elapsed.TotalSeconds >= seconds) break;
                    Buffer.BlockCopy(BitConverter.GetBytes(sent), 0, packet, 36, 8);
                    socket.Send(packet, packet.Length);
                    ++sent;
                }
                results[stage] = new PixelsLinkStage { Mbps = rates[stage], Packets = (int)sent, Seconds = clock.Elapsed.TotalSeconds };
            }
            Buffer.BlockCopy(BitConverter.GetBytes(4), 0, packet, 32, 4);
            socket.Send(packet, packet.Length);
            return results;
        }
    }
}
'@
$oldTrusted=(Get-Item WSMan:\localhost\Client\TrustedHosts).Value
$session=$null
$cancel=[Threading.CancellationTokenSource]::new()
$receiver=$null
try {
    if(-not $Loopback){
        Set-Item WSMan:\localhost\Client\TrustedHosts -Value '39.71.45.66' -Force
        $machineText=Get-Content (Join-Path $repo '.env/test_machine.md') -Raw
        $secret=[regex]::Match($machineText,'(?m)^\s*-\s*密码\s*[:：]\s*(.+?)\s*$').Groups[1].Value
        $session=New-PSSession -ComputerName 39.71.45.66 -Credential ([pscredential]::new('administrator',(ConvertTo-SecureString $secret -AsPlainText -Force)))
        Invoke-Command -Session $session -ArgumentList $source,$Port -ScriptBlock {
            param($code,$testPort)
            if(Get-NetUDPEndpoint -LocalPort $testPort -ErrorAction SilentlyContinue){throw "UDP $testPort is in use; refusing to disturb active Render"}
            Add-Type $code
        }
    } elseif(Get-NetUDPEndpoint -LocalPort $Port -ErrorAction SilentlyContinue){
        throw "Local UDP $Port is in use"
    }
    if(-not ('PixelsBoundedLinkProbe' -as [type])){Add-Type $source}
    Get-NetAdapterStatistics | Select-Object Name,ReceivedDiscardedPackets,ReceivedPacketErrors |
        ConvertTo-Json | Set-Content "$output/nic-before.json"
    $target=if($Loopback){'127.0.0.1'}else{'39.71.45.66'}
    $receiver=[PixelsBoundedLinkProbe]::Receive($target,$Port,$nonce,$DatagramSize,$cancel.Token)
    Write-Output "LINK_PROBE output=$output target=$target port=$Port bytes=$DatagramSize stage_seconds=$StageSeconds rates_mbps=8,12,16,20"
    $sent=if($Loopback){[PixelsBoundedLinkProbe]::Send($Port,$nonce,$StageSeconds,$DatagramSize)}else{
        Invoke-Command -Session $session -ArgumentList $nonce,$StageSeconds,$Port,$DatagramSize -ScriptBlock {
            param($token,$seconds,$testPort,$packetSize)
            [PixelsBoundedLinkProbe]::Send($testPort,$token,$seconds,$packetSize)
        }
    }
    if(-not $receiver.Wait(5000)){throw 'Probe receiver did not see end marker'}
    $counts=$receiver.GetAwaiter().GetResult()
    $results=for($index=0;$index -lt 4;++$index){
        [pscustomobject]@{Target=$target;Port=$Port;DatagramSize=$DatagramSize;TargetMbps=$sent[$index].Mbps;Sent=$sent[$index].Packets;Received=$counts[$index];
            LossPercent=[Math]::Round(100*(1-$counts[$index]/[double]$sent[$index].Packets),3);
            ActualSendMbps=[Math]::Round($sent[$index].Packets*($DatagramSize+42)*8/$sent[$index].Seconds/1e6,3)}
    }
    $results | ConvertTo-Json | Set-Content "$output/result.json"
    $results | Format-Table
} finally {
    $cancel.Cancel()
    try {if($receiver){[void]$receiver.Wait(2000)}} catch {Write-Warning 'Receiver stopped with an error'}
    $cancel.Dispose()
    Get-NetAdapterStatistics | Select-Object Name,ReceivedDiscardedPackets,ReceivedPacketErrors |
        ConvertTo-Json | Set-Content "$output/nic-after.json"
    try {if($session){Remove-PSSession $session}} finally {
        if(-not $Loopback){Set-Item WSMan:\localhost\Client\TrustedHosts -Value $oldTrusted -Force}
    }
}
