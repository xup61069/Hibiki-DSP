// SPDX-License-Identifier: GPL-3.0-only

using System.Buffers.Binary;
using System.IO.Pipes;

namespace Hibiki.ControlModel;

// This mirrors src/hub/include/hibiki/ipc.hpp. Keep the numeric values stable:
// the bytes are exchanged over the versioned control pipe, not serialized as
// .NET enum names.
public enum ControlMessageType : ushort
{
    Hello = 1,
    VolumeNotification = 2,
    GraphPrepare = 3,
    GraphCommit = 4,
    GraphRollback = 5,
    Ack = 6,
    Error = 7,
    SceneApply = 8
}

public enum IpcDecodeError
{
    None,
    Truncated,
    InvalidMagic,
    UnsupportedVersion,
    InvalidType,
    OversizedPayload,
    LengthMismatch
}

public sealed record IpcEnvelopeV1(
    ControlMessageType Type,
    ulong RequestId,
    ReadOnlyMemory<byte> Payload)
{
    public uint PayloadBytes => checked((uint)Payload.Length);
}

public static class IpcCodecV1
{
    public const uint Magic = 0x314B4948U;
    public const ushort Version = 1;
    public const int HeaderBytes = 20;
    public const int MaxPayloadBytes = 1024 * 1024;

    public static byte[] Encode(IpcEnvelopeV1 envelope)
    {
        if (!IsValidType(envelope.Type)) throw new ArgumentOutOfRangeException(nameof(envelope));
        if (envelope.Payload.Length > MaxPayloadBytes)
            throw new ArgumentException("IPC payload exceeds the v1 limit.", nameof(envelope));

        var bytes = new byte[HeaderBytes + envelope.Payload.Length];
        var span = bytes.AsSpan();
        BinaryPrimitives.WriteUInt32LittleEndian(span, Magic);
        BinaryPrimitives.WriteUInt16LittleEndian(span[4..], Version);
        BinaryPrimitives.WriteUInt16LittleEndian(span[6..], (ushort)envelope.Type);
        BinaryPrimitives.WriteUInt32LittleEndian(span[8..], (uint)envelope.Payload.Length);
        BinaryPrimitives.WriteUInt64LittleEndian(span[12..], envelope.RequestId);
        envelope.Payload.Span.CopyTo(span[HeaderBytes..]);
        return bytes;
    }

    public static bool TryDecode(ReadOnlySpan<byte> bytes,
                                 out IpcEnvelopeV1? envelope,
                                 out IpcDecodeError error)
    {
        envelope = null;
        error = IpcDecodeError.None;
        if (bytes.Length < HeaderBytes)
        {
            error = IpcDecodeError.Truncated;
            return false;
        }
        if (BinaryPrimitives.ReadUInt32LittleEndian(bytes) != Magic)
        {
            error = IpcDecodeError.InvalidMagic;
            return false;
        }
        if (BinaryPrimitives.ReadUInt16LittleEndian(bytes[4..]) != Version)
        {
            error = IpcDecodeError.UnsupportedVersion;
            return false;
        }
        var rawType = BinaryPrimitives.ReadUInt16LittleEndian(bytes[6..]);
        if (!Enum.IsDefined(typeof(ControlMessageType), rawType))
        {
            error = IpcDecodeError.InvalidType;
            return false;
        }
        var payloadBytes = BinaryPrimitives.ReadUInt32LittleEndian(bytes[8..]);
        if (payloadBytes > MaxPayloadBytes)
        {
            error = IpcDecodeError.OversizedPayload;
            return false;
        }
        if (bytes.Length - HeaderBytes != payloadBytes)
        {
            error = IpcDecodeError.LengthMismatch;
            return false;
        }
        var payload = bytes[HeaderBytes..].ToArray();
        envelope = new IpcEnvelopeV1(
            (ControlMessageType)rawType,
            BinaryPrimitives.ReadUInt64LittleEndian(bytes[12..]),
            payload);
        return true;
    }

    public static bool IsValidType(ControlMessageType type) =>
        type is ControlMessageType.Hello or ControlMessageType.VolumeNotification or
        ControlMessageType.GraphPrepare or ControlMessageType.GraphCommit or
        ControlMessageType.GraphRollback or ControlMessageType.Ack or ControlMessageType.Error or
        ControlMessageType.SceneApply;
}

public static class ControlPayloadsV1
{
    public const int VolumeNotificationBytes = 16;
    public const int SceneApplyBytes = 64;
    private static readonly System.Text.UTF8Encoding StrictUtf8 =
        new(encoderShouldEmitUTF8Identifier: false, throwOnInvalidBytes: true);

    public static byte[] EncodeVolumeNotification(double requestedDb,
                                                   bool mute,
                                                   ulong generation)
    {
        if (!double.IsFinite(requestedDb) || requestedDb < -144.0 || requestedDb > 12.0)
            throw new ArgumentOutOfRangeException(nameof(requestedDb));
        var payload = new byte[VolumeNotificationBytes];
        var q16 = checked((int)Math.Round(Math.Clamp(requestedDb, -144.0, 12.0) * 65536.0,
                                          MidpointRounding.AwayFromZero));
        BinaryPrimitives.WriteInt32LittleEndian(payload, q16);
        payload[4] = mute ? (byte)1 : (byte)0;
        BinaryPrimitives.WriteUInt64LittleEndian(payload.AsSpan(8), generation);
        return payload;
    }

    public static bool TryDecodeVolumeNotification(ReadOnlySpan<byte> payload,
                                                   out double requestedDb,
                                                   out bool mute,
                                                   out ulong generation)
    {
        requestedDb = 0.0;
        mute = false;
        generation = 0UL;
        if (payload.Length != VolumeNotificationBytes || (payload[4] != 0 && payload[4] != 1) ||
            payload[5] != 0 || payload[6] != 0 || payload[7] != 0)
            return false;
        var q16 = BinaryPrimitives.ReadInt32LittleEndian(payload);
        if (q16 < -144 * 65536 || q16 > 12 * 65536) return false;
        requestedDb = q16 / 65536.0;
        mute = payload[4] != 0;
        generation = BinaryPrimitives.ReadUInt64LittleEndian(payload[8..]);
        return true;
    }

    public static byte[] EncodeSceneApply(string sceneId, string outputGroup)
    {
        var scene = StrictUtf8.GetBytes(sceneId ?? string.Empty);
        var output = StrictUtf8.GetBytes(outputGroup ?? string.Empty);
        if (scene.Length is < 1 or > 31 || output.Length is < 1 or > 31 ||
            scene.Any(value => value < 0x20) || output.Any(value => value < 0x20))
            throw new ArgumentException("Scene and output-group IDs must be 1..31 printable UTF-8 bytes.");
        var payload = new byte[SceneApplyBytes];
        payload[0] = (byte)scene.Length;
        scene.CopyTo(payload.AsSpan(1));
        payload[32] = (byte)output.Length;
        output.CopyTo(payload.AsSpan(33));
        return payload;
    }

    public static bool TryDecodeSceneApply(ReadOnlySpan<byte> payload,
                                           out string sceneId,
                                           out string outputGroup)
    {
        sceneId = string.Empty;
        outputGroup = string.Empty;
        if (payload.Length != SceneApplyBytes || payload[0] is < 1 or > 31 ||
            payload[32] is < 1 or > 31)
            return false;
        var sceneBytes = payload.Slice(1, payload[0]);
        var outputBytes = payload.Slice(33, payload[32]);
        for (var index = 1 + payload[0]; index < 32; index++)
            if (payload[index] != 0) return false;
        for (var index = 33 + payload[32]; index < 64; index++)
            if (payload[index] != 0) return false;
        try
        {
            sceneId = StrictUtf8.GetString(sceneBytes);
            outputGroup = StrictUtf8.GetString(outputBytes);
            return !string.IsNullOrWhiteSpace(sceneId) && !string.IsNullOrWhiteSpace(outputGroup);
        }
        catch (ArgumentException)
        {
            return false;
        }
    }
}

// The UI/control plane owns request IDs; the audio thread never creates or
// waits for one. This small session object makes request/reply correlation
// explicit before a transport (named pipe or test stream) is attached.
public sealed class IpcRequestSession
{
    private ulong _nextRequestId = 1;

    public IpcEnvelopeV1 Create(ControlMessageType type, ReadOnlySpan<byte> payload = default)
    {
        var requestId = _nextRequestId++;
        return new IpcEnvelopeV1(type, requestId, payload.ToArray());
    }

    public static bool IsReplyTo(IpcEnvelopeV1 request, IpcEnvelopeV1 reply) =>
        request.RequestId != 0 && request.RequestId == reply.RequestId &&
        reply.Type is ControlMessageType.Ack or ControlMessageType.Error;
}

public sealed class ControlCommandFactoryV1
{
    private readonly IpcRequestSession _requests = new();

    public IpcEnvelopeV1 Hello() => _requests.Create(ControlMessageType.Hello);

    public IpcEnvelopeV1 SetVolume(double requestedDb, bool mute, ulong generation) =>
        _requests.Create(ControlMessageType.VolumeNotification,
            ControlPayloadsV1.EncodeVolumeNotification(requestedDb, mute, generation));

    public IpcEnvelopeV1 CommitGraph() => _requests.Create(ControlMessageType.GraphCommit);

    public IpcEnvelopeV1 RollbackGraph() => _requests.Create(ControlMessageType.GraphRollback);

    public IpcEnvelopeV1 ApplyScene(string sceneId, string outputGroup) =>
        _requests.Create(ControlMessageType.SceneApply,
            ControlPayloadsV1.EncodeSceneApply(sceneId, outputGroup));
}

// Thin asynchronous client for the control worker. It owns no UI state and
// never runs on the audio callback. The pipe name is the stable logical name
// from distribution-profile.yml; Windows adds the local named-pipe namespace.
public sealed class NamedPipeControlClientV1 : IAsyncDisposable
{
    public const string DefaultPipeName = "HibikiDSP_v1_control";

    private readonly string _pipeName;
    private NamedPipeClientStream? _stream;

    public NamedPipeControlClientV1(string pipeName = DefaultPipeName)
    {
        if (string.IsNullOrWhiteSpace(pipeName) || pipeName.IndexOfAny(['\\', '/']) >= 0)
            throw new ArgumentException("Pipe name must be a stable logical name.", nameof(pipeName));
        _pipeName = pipeName;
    }

    public bool IsConnected => _stream?.IsConnected == true;

    public async Task ConnectAsync(TimeSpan timeout, CancellationToken cancellationToken = default)
    {
        if (timeout <= TimeSpan.Zero || timeout > TimeSpan.FromSeconds(30))
            throw new ArgumentOutOfRangeException(nameof(timeout));
        await DisposeAsync().ConfigureAwait(false);
        var stream = new NamedPipeClientStream(".", _pipeName, PipeDirection.InOut,
            PipeOptions.Asynchronous);
        try
        {
            await stream.ConnectAsync(timeout, cancellationToken).ConfigureAwait(false);
            _stream = stream;
        }
        catch
        {
            await stream.DisposeAsync().ConfigureAwait(false);
            throw;
        }
    }

    public async Task<IpcEnvelopeV1> RoundTripAsync(IpcEnvelopeV1 request,
                                                     CancellationToken cancellationToken = default)
    {
        var stream = _stream ?? throw new InvalidOperationException("Control pipe is not connected.");
        var encoded = IpcCodecV1.Encode(request);
        if (encoded.Length > IpcCodecV1.MaxPayloadBytes + IpcCodecV1.HeaderBytes)
            throw new InvalidDataException("Encoded IPC frame exceeds the v1 limit.");
        var length = new byte[sizeof(uint)];
        BinaryPrimitives.WriteUInt32LittleEndian(length, checked((uint)encoded.Length));
        await stream.WriteAsync(length, cancellationToken).ConfigureAwait(false);
        await stream.WriteAsync(encoded, cancellationToken).ConfigureAwait(false);
        await stream.FlushAsync(cancellationToken).ConfigureAwait(false);

        await ReadExactAsync(stream, length, cancellationToken).ConfigureAwait(false);
        var responseLength = BinaryPrimitives.ReadUInt32LittleEndian(length);
        if (responseLength < IpcCodecV1.HeaderBytes ||
            responseLength > IpcCodecV1.HeaderBytes + IpcCodecV1.MaxPayloadBytes)
            throw new InvalidDataException("Control worker returned an invalid frame length.");
        var responseBytes = new byte[checked((int)responseLength)];
        await ReadExactAsync(stream, responseBytes, cancellationToken).ConfigureAwait(false);
        if (!IpcCodecV1.TryDecode(responseBytes, out var response, out var error) || response is null)
            throw new InvalidDataException($"Control worker returned an invalid frame: {error}.");
        if (response.RequestId != request.RequestId)
            throw new InvalidDataException("Control worker response request ID does not match.");
        return response;
    }

    public async ValueTask DisposeAsync()
    {
        if (_stream is not null)
        {
            await _stream.DisposeAsync().ConfigureAwait(false);
            _stream = null;
        }
    }

    private static async Task ReadExactAsync(Stream stream,
                                              Memory<byte> destination,
                                              CancellationToken cancellationToken)
    {
        var offset = 0;
        while (offset < destination.Length)
        {
            var read = await stream.ReadAsync(destination[offset..], cancellationToken)
                .ConfigureAwait(false);
            if (read == 0) throw new EndOfStreamException("Control pipe closed mid-frame.");
            offset += read;
        }
    }
}
