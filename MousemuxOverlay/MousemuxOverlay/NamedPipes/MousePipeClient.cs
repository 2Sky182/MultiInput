using System;
using System.Buffers.Binary;
using System.Collections.Generic;
using System.IO;
using System.IO.Pipes;
using System.Text;

namespace MousemuxOverlay.NamedPipes
{
    public sealed class MousePipeClient
    {
        private const string PipeName = "MouseDataPipe";
        private const int HeaderSize = 12;
        private const int MousePacketSize = 62;
        private const uint MaxMice = 256;

        public event Action<MouseData[]>? DataReceived;

        public async Task RunAsync(CancellationToken token)
        {
            while (!token.IsCancellationRequested)
            {
                try
                {
                    using var pipe = new NamedPipeClientStream(
                        ".",
                        PipeName,
                        PipeDirection.In,
                        PipeOptions.Asynchronous);

                    await pipe.ConnectAsync(token);

                    await ReceiveLoopAsync(pipe, token);
                }
                catch (OperationCanceledException)
                    when (token.IsCancellationRequested)
                {
                    break;
                }
                catch (Exception ex) when (
                    ex is IOException ||
                    ex is InvalidDataException ||
                    ex is ObjectDisposedException)
                {
                    // Verbindung verloren: Reconnect
                }

                if (!token.IsCancellationRequested)
                    await Task.Delay(1000, token);
            }
        }

        private async Task ReceiveLoopAsync(
            Stream pipe,
            CancellationToken token)
        {
            byte[] header = new byte[HeaderSize];

            while (!token.IsCancellationRequested)
            {
                await ReadExactlyAsync(pipe, header, token);

                uint version = ReadUInt32(header, 0);
                uint count = ReadUInt32(header, 4);
                uint packetSize = ReadUInt32(header, 8);

                if (version != 1 || count > MaxMice)
                    throw new InvalidDataException("Invalid header");

                uint expected = (uint)HeaderSize +
                                count * MousePacketSize;

                if (packetSize != expected)
                    throw new InvalidDataException("Invalid packet size");

                int payloadSize = checked((int)(count * MousePacketSize));

                byte[] payload = new byte[payloadSize];

                if (payloadSize > 0)
                    await ReadExactlyAsync(pipe, payload, token);

                var mice = new MouseData[count];

                for (int i = 0; i < mice.Length; i++)
                {
                    mice[i] = ParseMousePacket(
                        payload,
                        i * MousePacketSize);
                }

                DataReceived?.Invoke(mice);
            }
        }

        private static async Task ReadExactlyAsync(Stream stream, byte[] buffer, CancellationToken token)
        {
            int offset = 0;

            while (offset < buffer.Length)
            {
                int read = await stream.ReadAsync(
                    buffer.AsMemory(offset),
                    token);

                if (read == 0)
                    throw new EndOfStreamException();

                offset += read;
            }
        }

        private static MouseData ParseMousePacket(
            byte[] b, int offset)
        {
            uint version = ReadUInt32(b, offset);

            if (version != 1)
            {
                throw new InvalidDataException(
                    $"Unbekannte MousePacket-Version: {version}");
            }

            return new MouseData
            {
                X = ReadInt32(b, offset + 4),
                Y = ReadInt32(b, offset + 8),
                DX = ReadInt32(b, offset + 12),
                DY = ReadInt32(b, offset + 16),

                LeftDown = b[offset + 20] != 0,
                RightDown = b[offset + 21] != 0,
                MiddleDown = b[offset + 22] != 0,

                OldLeftDown = b[offset + 23] != 0,
                OldRightDown = b[offset + 24] != 0,
                OldMiddleDown = b[offset + 25] != 0,

                Wheel = ReadInt16(b, offset + 26),

                Device = ReadUInt64(b, offset + 28),

                HasBlocked = b[offset + 36] != 0,
                IsLinked = b[offset + 37] != 0,

                TargetWindow = ReadUInt64(b, offset + 38),

                R = ReadSingle(b, offset + 46),
                G = ReadSingle(b, offset + 50),
                B = ReadSingle(b, offset + 54),
                A = ReadSingle(b, offset + 58)
            };
        }

        private static uint ReadUInt32(byte[] b, int offset) =>
            BinaryPrimitives.ReadUInt32LittleEndian(
                b.AsSpan(offset, 4));

        private static int ReadInt32(byte[] b, int offset) =>
            BinaryPrimitives.ReadInt32LittleEndian(
                b.AsSpan(offset, 4));

        private static short ReadInt16(byte[] b, int offset) =>
            BinaryPrimitives.ReadInt16LittleEndian(
                b.AsSpan(offset, 2));

        private static ulong ReadUInt64(byte[] b, int offset) =>
            BinaryPrimitives.ReadUInt64LittleEndian(
                b.AsSpan(offset, 8));

        private static float ReadSingle(byte[] b, int offset) =>
            BitConverter.Int32BitsToSingle(
                ReadInt32(b, offset));

        // Deine bisherigen ParseMousePacket- und
        // ReadUInt32/ReadInt32/ReadInt16/ReadUInt64/
        // ReadSingle-Methoden hier unverändert übernehmen.
    }

    public sealed class MouseData
    {
        public int X { get; init; }
        public int Y { get; init; }
        public int DX { get; init; }
        public int DY { get; init; }

        public bool LeftDown { get; init; }
        public bool RightDown { get; init; }
        public bool MiddleDown { get; init; }

        public bool OldLeftDown { get; init; }
        public bool OldRightDown { get; init; }
        public bool OldMiddleDown { get; init; }

        public short Wheel { get; init; }

        // Numerische Werte; keine direkt verwendbaren C#-Pointer.
        public ulong Device { get; init; }
        public ulong TargetWindow { get; init; }

        public bool HasBlocked { get; init; }
        public bool IsLinked { get; init; }

        public float R { get; init; }
        public float G { get; init; }
        public float B { get; init; }
        public float A { get; init; }
    }
}
