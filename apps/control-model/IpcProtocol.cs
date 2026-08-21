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
    SceneApply = 8,
    DeviceSwitch = 9,
    DeviceCatalogSnapshot = 10,
    DeviceCatalogRequest = 11,
    ControlStatusSnapshot = 12,
    ControlStatusRequest = 13,
    SessionCatalogSnapshot = 14,
    SessionCatalogRequest = 15,
    SessionVolumeCommand = 16,
    SessionRouteCommand = 17,
    SessionRouteRuleCommand = 18
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

public enum SessionRouteRuleOperationV1 : byte
{
    Upsert = 1,
    Remove = 2,
    Clear = 3
}

public enum SessionRouteRuleGainOwnerV1 : byte
{
    WindowsSession = 0,
    HibikiInternal = 1
}

public sealed record SessionRouteRuleCommandV1(
    uint SchemaVersion,
    int Priority,
    double MakeupGainDb,
    SessionRouteRuleOperationV1 Operation,
    bool Enabled,
    SessionRouteRuleGainOwnerV1 GainOwner,
    ulong CatalogSequence,
    string RuleId,
    string AppId,
    string DisplayName,
    string LaneId,
    string OutputGroup);

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
        ControlMessageType.SceneApply or ControlMessageType.DeviceSwitch or
        ControlMessageType.DeviceCatalogSnapshot or ControlMessageType.DeviceCatalogRequest or
        ControlMessageType.ControlStatusSnapshot or ControlMessageType.ControlStatusRequest or
        ControlMessageType.SessionCatalogSnapshot or ControlMessageType.SessionCatalogRequest or
        ControlMessageType.SessionVolumeCommand or ControlMessageType.SessionRouteCommand or
        ControlMessageType.SessionRouteRuleCommand;
}

public static class ControlPayloadsV1
{
    public const int VolumeNotificationBytes = 16;
    public const int GroupedVolumeNotificationBytes = 48;
    public const int SceneApplyBytes = 64;
    public const int DeviceSwitchEndpointMaxBytes = 260;
    public const int DeviceSwitchBytes = 288;
    public const int DeviceCatalogSnapshotHeaderBytes = 16;
    public const int DeviceCatalogSnapshotEntryBytes = 416;
    public const int DeviceCatalogSnapshotCapacity = 32;
    public const int DeviceCatalogSnapshotMaxBytes = DeviceCatalogSnapshotHeaderBytes +
                                                      (DeviceCatalogSnapshotEntryBytes *
                                                       DeviceCatalogSnapshotCapacity);
    public const int ControlStatusSnapshotHeaderBytes = 40;
    public const int ControlStatusSnapshotEntryBytes = 224;
    public const int ControlStatusSnapshotCapacity = 8;
    public const int ControlStatusSnapshotMaxBytes = ControlStatusSnapshotHeaderBytes +
                                                     (ControlStatusSnapshotEntryBytes *
                                                      ControlStatusSnapshotCapacity);
    public const int SessionCatalogSnapshotHeaderBytes = 24;
    public const int SessionCatalogSnapshotEntryBytes = 256;
    public const int SessionCatalogSnapshotCapacity = 32;
    public const int SessionCatalogSnapshotMaxBytes = SessionCatalogSnapshotHeaderBytes +
                                                       (SessionCatalogSnapshotEntryBytes *
                                                        SessionCatalogSnapshotCapacity);
    public const int SessionVolumeCommandBytes = 24;
    public const int SessionRouteCommandBytes = 128;
    public const int SessionRouteCommandLaneMaxBytes = 48;
    public const int SessionRouteCommandOutputMaxBytes = 48;
    public const int SessionRouteRuleIdMaxBytes = 64;
    public const int SessionRouteRuleMatchMaxBytes = 128;
    public const int SessionRouteRuleRouteMaxBytes = 64;
    public const int SessionRouteRuleCommandBytes = 480;
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

    public static byte[] EncodeSessionVolumeCommand(ulong handle,
                                                    double requestedDb,
                                                    bool mute,
                                                    ulong catalogSequence)
    {
        if (handle == 0UL || catalogSequence == 0UL || !double.IsFinite(requestedDb) ||
            requestedDb < -144.0 || requestedDb > 12.0)
            throw new ArgumentOutOfRangeException(nameof(requestedDb));
        var payload = new byte[SessionVolumeCommandBytes];
        BinaryPrimitives.WriteUInt64LittleEndian(payload, handle);
        var q16 = checked((int)Math.Round(requestedDb * 65536.0,
                                          MidpointRounding.AwayFromZero));
        BinaryPrimitives.WriteInt32LittleEndian(payload.AsSpan(8), q16);
        payload[12] = mute ? (byte)1 : (byte)0;
        BinaryPrimitives.WriteUInt64LittleEndian(payload.AsSpan(16), catalogSequence);
        return payload;
    }

    public static bool TryDecodeSessionVolumeCommand(ReadOnlySpan<byte> payload,
                                                     out ulong handle,
                                                     out double requestedDb,
                                                     out bool mute,
                                                     out ulong catalogSequence)
    {
        handle = catalogSequence = 0UL;
        requestedDb = 0.0;
        mute = false;
        if (payload.Length != SessionVolumeCommandBytes ||
            payload[12] > 1 || payload[13] != 0 || payload[14] != 0 || payload[15] != 0)
            return false;
        handle = BinaryPrimitives.ReadUInt64LittleEndian(payload);
        catalogSequence = BinaryPrimitives.ReadUInt64LittleEndian(payload[16..]);
        var q16 = BinaryPrimitives.ReadInt32LittleEndian(payload[8..]);
        if (handle == 0UL || catalogSequence == 0UL || q16 < -144 * 65536 || q16 > 12 * 65536)
            return false;
        requestedDb = q16 / 65536.0;
        mute = payload[12] != 0;
        return true;
    }

    public static byte[] EncodeSessionRouteCommand(ulong handle,
                                                   ulong catalogSequence,
                                                   string laneId,
                                                   string outputGroup)
    {
        if (handle == 0UL || catalogSequence == 0UL)
            throw new ArgumentOutOfRangeException(nameof(handle));
        var lane = StrictUtf8.GetBytes(laneId ?? string.Empty);
        var output = StrictUtf8.GetBytes(outputGroup ?? string.Empty);
        if (lane.Length is < 1 or > SessionRouteCommandLaneMaxBytes ||
            output.Length is < 1 or > SessionRouteCommandOutputMaxBytes ||
            lane.Any(value => value < 0x20) || output.Any(value => value < 0x20))
            throw new ArgumentException("Session route labels are outside the v1 limit.");
        var payload = new byte[SessionRouteCommandBytes];
        BinaryPrimitives.WriteUInt64LittleEndian(payload, handle);
        BinaryPrimitives.WriteUInt64LittleEndian(payload.AsSpan(8), catalogSequence);
        payload[16] = (byte)lane.Length;
        payload[17] = (byte)output.Length;
        lane.CopyTo(payload.AsSpan(20));
        output.CopyTo(payload.AsSpan(68));
        return payload;
    }

    public static bool TryDecodeSessionRouteCommand(ReadOnlySpan<byte> payload,
                                                    out ulong handle,
                                                    out ulong catalogSequence,
                                                    out string laneId,
                                                    out string outputGroup)
    {
        handle = catalogSequence = 0UL;
        laneId = outputGroup = string.Empty;
        if (payload.Length != SessionRouteCommandBytes || payload[16] is < 1 or > SessionRouteCommandLaneMaxBytes ||
            payload[17] is < 1 or > SessionRouteCommandOutputMaxBytes || payload[18] != 0 ||
            payload[19] != 0)
            return false;
        handle = BinaryPrimitives.ReadUInt64LittleEndian(payload);
        catalogSequence = BinaryPrimitives.ReadUInt64LittleEndian(payload[8..]);
        if (handle == 0UL || catalogSequence == 0UL) return false;
        for (var index = payload[16]; index < SessionRouteCommandLaneMaxBytes; index++)
            if (payload[20 + index] != 0) return false;
        for (var index = payload[17]; index < SessionRouteCommandOutputMaxBytes; index++)
            if (payload[68 + index] != 0) return false;
        for (var index = 116; index < payload.Length; index++)
            if (payload[index] != 0) return false;
        try
        {
            laneId = StrictUtf8.GetString(payload.Slice(20, payload[16]));
            outputGroup = StrictUtf8.GetString(payload.Slice(68, payload[17]));
            return !laneId.Any(char.IsControl) && !outputGroup.Any(char.IsControl);
        }
        catch (ArgumentException)
        {
            return false;
        }
    }

    public static byte[] EncodeSessionRouteRuleCommand(SessionRouteRuleCommandV1 command)
    {
        if (command is null || command.SchemaVersion != 1U || command.CatalogSequence == 0UL ||
            !Enum.IsDefined(command.Operation) || !Enum.IsDefined(command.GainOwner) ||
            !double.IsFinite(command.MakeupGainDb) || command.MakeupGainDb is < -144.0 or > 12.0)
            throw new ArgumentException("Session route rule is outside the v1 contract.", nameof(command));
        var ruleId = StrictUtf8.GetBytes(command.RuleId ?? string.Empty);
        var appId = StrictUtf8.GetBytes(command.AppId ?? string.Empty);
        var displayName = StrictUtf8.GetBytes(command.DisplayName ?? string.Empty);
        var lane = StrictUtf8.GetBytes(command.LaneId ?? string.Empty);
        var output = StrictUtf8.GetBytes(command.OutputGroup ?? string.Empty);
        static bool Valid(ReadOnlySpan<byte> value, int capacity)
        {
            if (value.Length is < 1 || value.Length > capacity) return false;
            try
            {
                var text = StrictUtf8.GetString(value);
                return !text.Any(char.IsControl);
            }
            catch (ArgumentException)
            {
                return false;
            }
        }
        if (ruleId.Length > SessionRouteRuleIdMaxBytes || appId.Length > SessionRouteRuleMatchMaxBytes ||
            displayName.Length > SessionRouteRuleMatchMaxBytes || lane.Length > SessionRouteRuleRouteMaxBytes ||
            output.Length > SessionRouteRuleRouteMaxBytes ||
            (command.Operation == SessionRouteRuleOperationV1.Upsert &&
             (!Valid(ruleId, SessionRouteRuleIdMaxBytes) ||
              (!Valid(appId, SessionRouteRuleMatchMaxBytes) &&
               !Valid(displayName, SessionRouteRuleMatchMaxBytes)) ||
              !Valid(lane, SessionRouteRuleRouteMaxBytes) ||
              !Valid(output, SessionRouteRuleRouteMaxBytes))) ||
            (command.Operation == SessionRouteRuleOperationV1.Remove &&
             (!Valid(ruleId, SessionRouteRuleIdMaxBytes) || appId.Length != 0 ||
              displayName.Length != 0 || lane.Length != 0 || output.Length != 0)) ||
            (command.Operation == SessionRouteRuleOperationV1.Clear &&
             (ruleId.Length != 0 || appId.Length != 0 || displayName.Length != 0 ||
              lane.Length != 0 || output.Length != 0)))
            throw new ArgumentException("Session route rule text is outside the v1 limit.", nameof(command));
        var payload = new byte[SessionRouteRuleCommandBytes];
        BinaryPrimitives.WriteUInt32LittleEndian(payload, command.SchemaVersion);
        BinaryPrimitives.WriteInt32LittleEndian(payload.AsSpan(4), command.Priority);
        WriteDbQ16(payload.AsSpan(8), command.MakeupGainDb);
        payload[12] = (byte)command.Operation;
        payload[13] = command.Enabled ? (byte)1 : (byte)0;
        payload[14] = (byte)command.GainOwner;
        BinaryPrimitives.WriteUInt64LittleEndian(payload.AsSpan(16), command.CatalogSequence);
        payload[24] = (byte)ruleId.Length;
        payload[25] = (byte)appId.Length;
        payload[26] = (byte)displayName.Length;
        payload[27] = (byte)lane.Length;
        payload[28] = (byte)output.Length;
        ruleId.CopyTo(payload.AsSpan(32));
        appId.CopyTo(payload.AsSpan(96));
        displayName.CopyTo(payload.AsSpan(224));
        lane.CopyTo(payload.AsSpan(352));
        output.CopyTo(payload.AsSpan(416));
        return payload;
    }

    public static bool TryDecodeSessionRouteRuleCommand(
        ReadOnlySpan<byte> payload, out SessionRouteRuleCommandV1? command)
    {
        command = null;
        if (payload.Length != SessionRouteRuleCommandBytes || payload[15] != 0 ||
            payload[29] != 0 || payload[30] != 0 || payload[31] != 0 ||
            BinaryPrimitives.ReadUInt32LittleEndian(payload) != 1U || payload[13] > 1 ||
            !Enum.IsDefined(typeof(SessionRouteRuleOperationV1), payload[12]) ||
            !Enum.IsDefined(typeof(SessionRouteRuleGainOwnerV1), payload[14]) ||
            BinaryPrimitives.ReadUInt64LittleEndian(payload[16..]) == 0UL)
            return false;
        var q16 = BinaryPrimitives.ReadInt32LittleEndian(payload[8..]);
        if (q16 < -144 * 65536 || q16 > 12 * 65536) return false;
        var ruleBytes = payload[24];
        var appBytes = payload[25];
        var displayBytes = payload[26];
        var laneBytes = payload[27];
        var outputBytes = payload[28];
        if (ruleBytes > SessionRouteRuleIdMaxBytes || appBytes > SessionRouteRuleMatchMaxBytes ||
            displayBytes > SessionRouteRuleMatchMaxBytes || laneBytes > SessionRouteRuleRouteMaxBytes ||
            outputBytes > SessionRouteRuleRouteMaxBytes)
            return false;
        static bool TryText(ReadOnlySpan<byte> bytes, int used, out string value)
        {
            value = string.Empty;
            if (!IsZero(bytes, used, bytes.Length)) return false;
            try
            {
                value = StrictUtf8.GetString(bytes[..used]);
                return !value.Any(char.IsControl);
            }
            catch (ArgumentException)
            {
                return false;
            }
        }
        if (!TryText(payload.Slice(32, SessionRouteRuleIdMaxBytes), ruleBytes, out var ruleId) ||
            !TryText(payload.Slice(96, SessionRouteRuleMatchMaxBytes), appBytes, out var appId) ||
            !TryText(payload.Slice(224, SessionRouteRuleMatchMaxBytes), displayBytes, out var display) ||
            !TryText(payload.Slice(352, SessionRouteRuleRouteMaxBytes), laneBytes, out var lane) ||
            !TryText(payload.Slice(416, SessionRouteRuleRouteMaxBytes), outputBytes, out var output))
            return false;
        var operation = (SessionRouteRuleOperationV1)payload[12];
        if ((operation == SessionRouteRuleOperationV1.Upsert &&
             (ruleBytes == 0 || (appBytes == 0 && displayBytes == 0) || laneBytes == 0 || outputBytes == 0)) ||
            (operation == SessionRouteRuleOperationV1.Remove &&
             (ruleBytes == 0 || appBytes != 0 || displayBytes != 0 || laneBytes != 0 || outputBytes != 0)) ||
            (operation == SessionRouteRuleOperationV1.Clear &&
             (ruleBytes != 0 || appBytes != 0 || displayBytes != 0 || laneBytes != 0 || outputBytes != 0)))
            return false;
        command = new SessionRouteRuleCommandV1(
            1U,
            BinaryPrimitives.ReadInt32LittleEndian(payload[4..]),
            q16 / 65536.0,
            operation,
            payload[13] != 0,
            (SessionRouteRuleGainOwnerV1)payload[14],
            BinaryPrimitives.ReadUInt64LittleEndian(payload[16..]),
            ruleId, appId, display, lane, output);
        return true;
    }

    public static byte[] EncodeGroupedVolumeNotification(string outputGroup,
                                                          double requestedDb,
                                                          bool mute,
                                                          ulong generation)
    {
        var group = StrictUtf8.GetBytes(outputGroup ?? string.Empty);
        if (group.Length is < 1 or > 31 || group.Any(value => value < 0x20))
            throw new ArgumentException("Output-group ID must be 1..31 printable UTF-8 bytes.",
                                        nameof(outputGroup));
        var volume = EncodeVolumeNotification(requestedDb, mute, generation);
        var payload = new byte[GroupedVolumeNotificationBytes];
        volume.CopyTo(payload, 0);
        payload[16] = (byte)group.Length;
        group.CopyTo(payload.AsSpan(17));
        return payload;
    }

    public static bool TryDecodeGroupedVolumeNotification(ReadOnlySpan<byte> payload,
                                                           out string outputGroup,
                                                           out double requestedDb,
                                                           out bool mute,
                                                           out ulong generation)
    {
        outputGroup = string.Empty;
        requestedDb = 0.0;
        mute = false;
        generation = 0UL;
        if (payload.Length != GroupedVolumeNotificationBytes || payload[16] is < 1 or > 31 ||
            !TryDecodeVolumeNotification(payload[..VolumeNotificationBytes], out requestedDb,
                                          out mute, out generation))
            return false;
        for (var index = 17 + payload[16]; index < GroupedVolumeNotificationBytes; index++)
            if (payload[index] != 0) return false;
        try
        {
            outputGroup = StrictUtf8.GetString(payload.Slice(17, payload[16]));
            return !string.IsNullOrWhiteSpace(outputGroup);
        }
        catch (ArgumentException)
        {
            return false;
        }
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

    public static byte[] EncodeDeviceSwitch(string endpointId,
                                             int channels,
                                             int sampleRate,
                                             int bufferFrames,
                                             ulong catalogSequence)
    {
        var endpoint = StrictUtf8.GetBytes(endpointId ?? string.Empty);
        if (endpoint.Length is < 1 or > DeviceSwitchEndpointMaxBytes ||
            endpoint.Any(value => value < 0x20) ||
            channels is not (1 or 2 or 6 or 8) ||
            sampleRate is not (44100 or 48000 or 96000 or 192000) ||
            bufferFrames is < 16 or > 4096)
            throw new ArgumentException("Physical device request is outside the v1 contract.");
        var payload = new byte[DeviceSwitchBytes];
        BinaryPrimitives.WriteUInt16LittleEndian(payload, (ushort)endpoint.Length);
        endpoint.CopyTo(payload.AsSpan(2));
        BinaryPrimitives.WriteUInt32LittleEndian(payload.AsSpan(264), (uint)channels);
        BinaryPrimitives.WriteUInt32LittleEndian(payload.AsSpan(268), (uint)sampleRate);
        BinaryPrimitives.WriteUInt32LittleEndian(payload.AsSpan(272), (uint)bufferFrames);
        BinaryPrimitives.WriteUInt64LittleEndian(payload.AsSpan(280), catalogSequence);
        return payload;
    }

    public static bool TryDecodeDeviceSwitch(ReadOnlySpan<byte> payload,
                                              out string endpointId,
                                              out int channels,
                                              out int sampleRate,
                                              out int bufferFrames,
                                              out ulong catalogSequence)
    {
        endpointId = string.Empty;
        channels = sampleRate = bufferFrames = 0;
        catalogSequence = 0UL;
        if (payload.Length != DeviceSwitchBytes) return false;
        var endpointBytes = BinaryPrimitives.ReadUInt16LittleEndian(payload);
        if (endpointBytes is < 1 or > DeviceSwitchEndpointMaxBytes ||
            payload[262] != 0 || payload[263] != 0 ||
            payload[276] != 0 || payload[277] != 0 || payload[278] != 0 || payload[279] != 0)
            return false;
        for (var index = endpointBytes; index < DeviceSwitchEndpointMaxBytes; index++)
            if (payload[2 + index] != 0) return false;
        try
        {
            endpointId = StrictUtf8.GetString(payload.Slice(2, endpointBytes));
        }
        catch (ArgumentException)
        {
            return false;
        }
        if (endpointId.Any(value => value < 0x20)) return false;
        channels = checked((int)BinaryPrimitives.ReadUInt32LittleEndian(payload[264..]));
        sampleRate = checked((int)BinaryPrimitives.ReadUInt32LittleEndian(payload[268..]));
        bufferFrames = checked((int)BinaryPrimitives.ReadUInt32LittleEndian(payload[272..]));
        catalogSequence = BinaryPrimitives.ReadUInt64LittleEndian(payload[280..]);
        return channels is 1 or 2 or 6 or 8 &&
               sampleRate is 44100 or 48000 or 96000 or 192000 &&
               bufferFrames is >= 16 and <= 4096;
    }

    public static byte[] EncodeDeviceCatalogSnapshot(IReadOnlyList<PhysicalDeviceCard> devices,
                                                      ulong catalogSequence)
    {
        if (devices is null || devices.Count > DeviceCatalogSnapshotCapacity)
            throw new ArgumentException("Physical device snapshot exceeds the v1 limit.",
                                        nameof(devices));
        var payload = new byte[DeviceCatalogSnapshotHeaderBytes +
                               (devices.Count * DeviceCatalogSnapshotEntryBytes)];
        BinaryPrimitives.WriteUInt16LittleEndian(payload, (ushort)devices.Count);
        BinaryPrimitives.WriteUInt64LittleEndian(payload.AsSpan(4), catalogSequence);
        var seen = new HashSet<string>(StringComparer.Ordinal);
        var defaults = new HashSet<PhysicalDeviceFlowV1>();
        for (var index = 0; index < devices.Count; index++)
        {
            var device = devices[index];
            var endpoint = StrictUtf8.GetBytes(device.EndpointId ?? string.Empty);
            var display = StrictUtf8.GetBytes(device.DisplayName ?? string.Empty);
            if (endpoint.Length is < 1 or > DeviceSwitchEndpointMaxBytes ||
                display.Length is < 1 or > 128 || endpoint.Any(value => value < 0x20) ||
                display.Any(value => value < 0x20) || !seen.Add(device.EndpointId ?? string.Empty) ||
                !Enum.IsDefined(device.Flow) || !Enum.IsDefined(device.Availability) ||
                (device.IsDefault && (!device.IsSelectable || !defaults.Add(device.Flow))) ||
                device.Channels is not (1 or 2 or 6 or 8) ||
                device.SampleRate is not (44100 or 48000 or 96000 or 192000) ||
                device.BufferFrames is < 16 or > 4096)
                throw new ArgumentException("Physical device snapshot contains an invalid entry.",
                                            nameof(devices));
            var offset = DeviceCatalogSnapshotHeaderBytes +
                         (index * DeviceCatalogSnapshotEntryBytes);
            BinaryPrimitives.WriteUInt16LittleEndian(payload.AsSpan(offset), (ushort)endpoint.Length);
            BinaryPrimitives.WriteUInt16LittleEndian(payload.AsSpan(offset + 2), (ushort)display.Length);
            payload[offset + 4] = (byte)device.Flow;
            payload[offset + 5] = (byte)device.Availability;
            BinaryPrimitives.WriteUInt16LittleEndian(payload.AsSpan(offset + 6),
                                                     device.IsDefault ? (ushort)1 : (ushort)0);
            endpoint.CopyTo(payload.AsSpan(offset + 8));
            display.CopyTo(payload.AsSpan(offset + 268));
            BinaryPrimitives.WriteUInt32LittleEndian(payload.AsSpan(offset + 396),
                                                     (uint)device.Channels);
            BinaryPrimitives.WriteUInt32LittleEndian(payload.AsSpan(offset + 400),
                                                     (uint)device.SampleRate);
            BinaryPrimitives.WriteUInt32LittleEndian(payload.AsSpan(offset + 404),
                                                     (uint)device.BufferFrames);
            BinaryPrimitives.WriteUInt64LittleEndian(payload.AsSpan(offset + 408),
                                                     device.LastSequence);
        }
        return payload;
    }

    public static bool TryDecodeDeviceCatalogSnapshot(ReadOnlySpan<byte> payload,
                                                       out ulong catalogSequence,
                                                       out IReadOnlyList<PhysicalDeviceCard> devices)
    {
        catalogSequence = 0UL;
        devices = Array.Empty<PhysicalDeviceCard>();
        if (payload.Length < DeviceCatalogSnapshotHeaderBytes ||
            payload.Length > DeviceCatalogSnapshotMaxBytes || payload[2] != 0 || payload[3] != 0 ||
            payload[12] != 0 || payload[13] != 0 || payload[14] != 0 || payload[15] != 0)
            return false;
        var count = BinaryPrimitives.ReadUInt16LittleEndian(payload);
        var expected = DeviceCatalogSnapshotHeaderBytes +
                       (count * DeviceCatalogSnapshotEntryBytes);
        if (count > DeviceCatalogSnapshotCapacity || payload.Length != expected) return false;
        catalogSequence = BinaryPrimitives.ReadUInt64LittleEndian(payload[4..]);
        var list = new List<PhysicalDeviceCard>(count);
        var seen = new HashSet<string>(StringComparer.Ordinal);
        var defaults = new HashSet<PhysicalDeviceFlowV1>();
        for (var index = 0; index < count; index++)
        {
            var offset = DeviceCatalogSnapshotHeaderBytes +
                         (index * DeviceCatalogSnapshotEntryBytes);
            var entry = payload.Slice(offset, DeviceCatalogSnapshotEntryBytes);
            var endpointBytes = BinaryPrimitives.ReadUInt16LittleEndian(entry);
            var displayBytes = BinaryPrimitives.ReadUInt16LittleEndian(entry[2..]);
            var rawFlow = entry[4];
            var rawAvailability = entry[5];
            var flags = BinaryPrimitives.ReadUInt16LittleEndian(entry[6..]);
            if (endpointBytes is < 1 or > DeviceSwitchEndpointMaxBytes ||
                displayBytes is < 1 or > 128 || rawFlow > 1 || rawAvailability > 3 ||
                (flags & 0xfffe) != 0)
                return false;
            for (var pad = endpointBytes; pad < DeviceSwitchEndpointMaxBytes; pad++)
                if (entry[8 + pad] != 0) return false;
            for (var pad = displayBytes; pad < 128; pad++)
                if (entry[268 + pad] != 0) return false;
            string endpoint;
            string display;
            try
            {
                endpoint = StrictUtf8.GetString(entry.Slice(8, endpointBytes));
                display = StrictUtf8.GetString(entry.Slice(268, displayBytes));
            }
            catch (ArgumentException)
            {
                return false;
            }
            if (endpoint.Any(value => char.IsControl(value)) ||
                display.Any(value => char.IsControl(value)) || !seen.Add(endpoint))
                return false;
            var channels = BinaryPrimitives.ReadUInt32LittleEndian(entry[396..]);
            var sampleRate = BinaryPrimitives.ReadUInt32LittleEndian(entry[400..]);
            var bufferFrames = BinaryPrimitives.ReadUInt32LittleEndian(entry[404..]);
            if (channels is not (1U or 2U or 6U or 8U) ||
                sampleRate is not (44100U or 48000U or 96000U or 192000U) ||
                bufferFrames is < 16U or > 4096U)
                return false;
            var flow = (PhysicalDeviceFlowV1)rawFlow;
            var availability = (PhysicalDeviceAvailabilityV1)rawAvailability;
            var isDefault = (flags & 1) != 0;
            var card = new PhysicalDeviceCard(endpoint, display, flow, availability,
                                               (int)channels, (int)sampleRate, (int)bufferFrames,
                                               isDefault,
                                               BinaryPrimitives.ReadUInt64LittleEndian(entry[408..]));
            if (isDefault && (!card.IsSelectable || !defaults.Add(flow))) return false;
            list.Add(card);
        }
        devices = list;
        return true;
    }

    public static byte[] EncodeSessionCatalogSnapshot(
        ulong sequence,
        ulong generation,
        IReadOnlyList<SessionCatalogEntryV1> sessions)
    {
        if (sequence == 0UL || sessions is null || sessions.Count > SessionCatalogSnapshotCapacity)
            throw new ArgumentException("Session catalog snapshot exceeds the v1 limit.",
                                        nameof(sessions));
        var payload = new byte[SessionCatalogSnapshotHeaderBytes +
                               (sessions.Count * SessionCatalogSnapshotEntryBytes)];
        BinaryPrimitives.WriteUInt16LittleEndian(payload, (ushort)sessions.Count);
        BinaryPrimitives.WriteUInt64LittleEndian(payload.AsSpan(4), sequence);
        BinaryPrimitives.WriteUInt64LittleEndian(payload.AsSpan(12), generation);
        var seen = new HashSet<ulong>();
        for (var index = 0; index < sessions.Count; index++)
        {
            var session = sessions[index];
            var name = StrictUtf8.GetBytes(session.Name ?? string.Empty);
            var app = StrictUtf8.GetBytes(session.AppId ?? string.Empty);
            var lane = StrictUtf8.GetBytes(session.LaneId ?? string.Empty);
            var output = StrictUtf8.GetBytes(session.OutputGroup ?? string.Empty);
            if (session.Handle == 0UL || !seen.Add(session.Handle) ||
                name.Length > 64 || app.Length > 64 || lane.Length > 48 || output.Length > 48 ||
                name.Any(value => value < 0x20) || app.Any(value => value < 0x20) ||
                lane.Any(value => value < 0x20) || output.Any(value => value < 0x20) ||
                !Enum.IsDefined(session.RouteState) ||
                !double.IsFinite(session.RequestedDb) ||
                (session.VolumeAvailable && session.RequestedDb is < -144.0 or > 12.0))
                throw new ArgumentException("Session catalog entry is invalid.", nameof(sessions));
            var offset = SessionCatalogSnapshotHeaderBytes +
                         (index * SessionCatalogSnapshotEntryBytes);
            BinaryPrimitives.WriteUInt64LittleEndian(payload.AsSpan(offset), session.Handle);
            payload[offset + 8] = session.Active ? (byte)1 : (byte)0;
            payload[offset + 9] = (byte)session.RouteState;
            BinaryPrimitives.WriteUInt16LittleEndian(payload.AsSpan(offset + 10),
                                                     session.VolumeAvailable ? (ushort)1 : (ushort)0);
            WriteDbQ16(payload.AsSpan(offset + 12), session.VolumeAvailable ? session.RequestedDb : 0.0);
            payload[offset + 16] = session.Muted ? (byte)1 : (byte)0;
            BinaryPrimitives.WriteUInt16LittleEndian(payload.AsSpan(offset + 20), (ushort)name.Length);
            BinaryPrimitives.WriteUInt16LittleEndian(payload.AsSpan(offset + 22), (ushort)app.Length);
            BinaryPrimitives.WriteUInt16LittleEndian(payload.AsSpan(offset + 24), (ushort)lane.Length);
            BinaryPrimitives.WriteUInt16LittleEndian(payload.AsSpan(offset + 26), (ushort)output.Length);
            name.CopyTo(payload.AsSpan(offset + 28));
            app.CopyTo(payload.AsSpan(offset + 92));
            lane.CopyTo(payload.AsSpan(offset + 156));
            output.CopyTo(payload.AsSpan(offset + 204));
        }
        return payload;
    }

    public static bool TryDecodeSessionCatalogSnapshot(
        ReadOnlySpan<byte> payload,
        out ulong sequence,
        out ulong generation,
        out IReadOnlyList<SessionCatalogEntryV1> sessions)
    {
        sequence = generation = 0UL;
        sessions = Array.Empty<SessionCatalogEntryV1>();
        if (payload.Length < SessionCatalogSnapshotHeaderBytes ||
            payload.Length > SessionCatalogSnapshotMaxBytes || payload[2] != 0 || payload[3] != 0 ||
            payload[20] != 0 || payload[21] != 0 || payload[22] != 0 || payload[23] != 0)
            return false;
        var count = BinaryPrimitives.ReadUInt16LittleEndian(payload);
        var expected = SessionCatalogSnapshotHeaderBytes +
                       (count * SessionCatalogSnapshotEntryBytes);
        if (count > SessionCatalogSnapshotCapacity || payload.Length != expected) return false;
        sequence = BinaryPrimitives.ReadUInt64LittleEndian(payload[4..]);
        generation = BinaryPrimitives.ReadUInt64LittleEndian(payload[12..]);
        if (sequence == 0UL) return false;
        var list = new List<SessionCatalogEntryV1>(count);
        var seen = new HashSet<ulong>();
        for (var index = 0; index < count; index++)
        {
            var offset = SessionCatalogSnapshotHeaderBytes +
                         (index * SessionCatalogSnapshotEntryBytes);
            var entry = payload.Slice(offset, SessionCatalogSnapshotEntryBytes);
            var handle = BinaryPrimitives.ReadUInt64LittleEndian(entry);
            var active = entry[8];
            var rawState = entry[9];
            var flags = BinaryPrimitives.ReadUInt16LittleEndian(entry[10..]);
            var muted = entry[16];
            var nameBytes = BinaryPrimitives.ReadUInt16LittleEndian(entry[20..]);
            var appBytes = BinaryPrimitives.ReadUInt16LittleEndian(entry[22..]);
            var laneBytes = BinaryPrimitives.ReadUInt16LittleEndian(entry[24..]);
            var outputBytes = BinaryPrimitives.ReadUInt16LittleEndian(entry[26..]);
            if (handle == 0UL || !seen.Add(handle) || active > 1 ||
                rawState > (byte)SessionCatalogRouteStateV1.Unavailable || (flags & 0xfffe) != 0 ||
                muted > 1 || entry[17] != 0 || entry[18] != 0 || entry[19] != 0 ||
                nameBytes > 64 || appBytes > 64 || laneBytes > 48 || outputBytes > 48 ||
                !IsZero(entry[28..], nameBytes, 64) || !IsZero(entry[92..], appBytes, 64) ||
                !IsZero(entry[156..], laneBytes, 48) || !IsZero(entry[204..], outputBytes, 48) ||
                !IsZero(entry[252..], 0, 4))
                return false;
            string name;
            string app;
            string lane;
            string output;
            try
            {
                name = StrictUtf8.GetString(entry.Slice(28, nameBytes));
                app = StrictUtf8.GetString(entry.Slice(92, appBytes));
                lane = StrictUtf8.GetString(entry.Slice(156, laneBytes));
                output = StrictUtf8.GetString(entry.Slice(204, outputBytes));
            }
            catch (ArgumentException)
            {
                return false;
            }
            if (name.Any(char.IsControl) || app.Any(char.IsControl) || lane.Any(char.IsControl) ||
                output.Any(char.IsControl)) return false;
            var volumeAvailable = (flags & 1) != 0;
            var requestedDb = ReadDbQ16(entry[12..]);
            if (volumeAvailable && (requestedDb is < -144.0 or > 12.0)) return false;
            list.Add(new SessionCatalogEntryV1(handle, active != 0,
                                                (SessionCatalogRouteStateV1)rawState,
                                                volumeAvailable, requestedDb, muted != 0,
                                                name, app, lane, output));
        }
        sessions = list;
        return true;
    }

    public static byte[] EncodeControlStatusSnapshot(
        ulong sequence,
        VolumeSafetyStateV1 volume,
        IReadOnlyList<RouteHealthCardV1> routes)
    {
        if (!volume.IsValid || routes is null || routes.Count > ControlStatusSnapshotCapacity)
            throw new ArgumentException("Control status snapshot is outside the v1 limit.");
        var payload = new byte[ControlStatusSnapshotHeaderBytes +
                               (routes.Count * ControlStatusSnapshotEntryBytes)];
        BinaryPrimitives.WriteUInt16LittleEndian(payload, (ushort)routes.Count);
        BinaryPrimitives.WriteUInt64LittleEndian(payload.AsSpan(4), sequence);
        WriteDbQ16(payload.AsSpan(12), volume.RequestedDb);
        WriteDbQ16(payload.AsSpan(16), volume.SafetyCeilingDb);
        WriteDbQ16(payload.AsSpan(20), volume.EffectiveDb);
        payload[24] = volume.Muted ? (byte)1 : (byte)0;
        payload[25] = (byte)volume.Origin;
        payload[26] = (byte)volume.Actuator;
        BinaryPrimitives.WriteUInt64LittleEndian(payload.AsSpan(28), volume.Generation);
        var seen = new HashSet<string>(StringComparer.Ordinal);
        for (var index = 0; index < routes.Count; index++)
        {
            var route = routes[index];
            var routeId = route.Id ?? string.Empty;
            var id = StrictUtf8.GetBytes(routeId);
            var name = StrictUtf8.GetBytes(route.Name ?? string.Empty);
            var detail = StrictUtf8.GetBytes(route.Detail ?? string.Empty);
            if (id.Length is < 1 or > 31 || name.Length is < 1 or > 63 ||
                detail.Length is < 1 or > 119 || id.Any(value => value < 0x20) ||
                name.Any(value => value < 0x20) || detail.Any(value => value < 0x20) ||
                !Enum.IsDefined(route.State) || !seen.Add(routeId))
                throw new ArgumentException("Route health entry is invalid.", nameof(routes));
            var offset = ControlStatusSnapshotHeaderBytes +
                         (index * ControlStatusSnapshotEntryBytes);
            payload[offset] = (byte)id.Length;
            payload[offset + 1] = (byte)route.State;
            BinaryPrimitives.WriteUInt16LittleEndian(payload.AsSpan(offset + 2),
                                                     route.RequiresUserAction ? (ushort)1 : (ushort)0);
            BinaryPrimitives.WriteUInt16LittleEndian(payload.AsSpan(offset + 4), (ushort)name.Length);
            BinaryPrimitives.WriteUInt16LittleEndian(payload.AsSpan(offset + 6), (ushort)detail.Length);
            id.CopyTo(payload.AsSpan(offset + 8));
            name.CopyTo(payload.AsSpan(offset + 40));
            detail.CopyTo(payload.AsSpan(offset + 104));
        }
        return payload;
    }

    public static bool TryDecodeControlStatusSnapshot(
        ReadOnlySpan<byte> payload,
        out ulong sequence,
        out VolumeSafetyStateV1 volume,
        out IReadOnlyList<RouteHealthCardV1> routes)
    {
        sequence = 0UL;
        volume = VolumeSafetyStateV1.Initial();
        routes = Array.Empty<RouteHealthCardV1>();
        if (payload.Length < ControlStatusSnapshotHeaderBytes ||
            payload.Length > ControlStatusSnapshotMaxBytes || payload[2] != 0 || payload[3] != 0 ||
            payload[27] != 0 || payload[36] != 0 || payload[37] != 0 ||
            payload[38] != 0 || payload[39] != 0)
            return false;
        var count = BinaryPrimitives.ReadUInt16LittleEndian(payload);
        var expected = ControlStatusSnapshotHeaderBytes +
                       (count * ControlStatusSnapshotEntryBytes);
        if (count > ControlStatusSnapshotCapacity || payload.Length != expected ||
            payload[24] > 1 || payload[25] > (byte)VolumeStateOriginV1.Session ||
            payload[26] > (byte)VolumeActuatorV1.StrictDirect)
            return false;
        var requested = ReadDbQ16(payload[12..]);
        var ceiling = ReadDbQ16(payload[16..]);
        var effective = ReadDbQ16(payload[20..]);
        volume = new VolumeSafetyStateV1(requested, ceiling, effective, payload[24] != 0,
                                          BinaryPrimitives.ReadUInt64LittleEndian(payload[28..]),
                                          (VolumeStateOriginV1)payload[25],
                                          (VolumeActuatorV1)payload[26]);
        if (!volume.IsValid) return false;
        sequence = BinaryPrimitives.ReadUInt64LittleEndian(payload[4..]);
        var list = new List<RouteHealthCardV1>(count);
        var seen = new HashSet<string>(StringComparer.Ordinal);
        for (var index = 0; index < count; index++)
        {
            var offset = ControlStatusSnapshotHeaderBytes +
                         (index * ControlStatusSnapshotEntryBytes);
            var entry = payload.Slice(offset, ControlStatusSnapshotEntryBytes);
            var idBytes = entry[0];
            var rawState = entry[1];
            var flags = BinaryPrimitives.ReadUInt16LittleEndian(entry[2..]);
            var nameBytes = BinaryPrimitives.ReadUInt16LittleEndian(entry[4..]);
            var detailBytes = BinaryPrimitives.ReadUInt16LittleEndian(entry[6..]);
            if (idBytes is < 1 or > 31 || nameBytes is < 1 or > 63 ||
                detailBytes is < 1 or > 119 || rawState > (byte)RouteHealthStateV1.Unavailable ||
                (flags & 0xfffe) != 0 || !IsZero(entry[8..], idBytes, 32) ||
                !IsZero(entry[40..], nameBytes, 64) || !IsZero(entry[104..], detailBytes, 120))
                return false;
            string id;
            string name;
            string detail;
            try
            {
                id = StrictUtf8.GetString(entry.Slice(8, idBytes));
                name = StrictUtf8.GetString(entry.Slice(40, nameBytes));
                detail = StrictUtf8.GetString(entry.Slice(104, detailBytes));
            }
            catch (ArgumentException)
            {
                return false;
            }
            if (id.Any(char.IsControl) || name.Any(char.IsControl) || detail.Any(char.IsControl) ||
                !seen.Add(id)) return false;
            list.Add(new RouteHealthCardV1(id, name, (RouteHealthStateV1)rawState, detail,
                                           (flags & 1) != 0));
        }
        routes = list;
        return true;
    }

    private static void WriteDbQ16(Span<byte> destination, double db)
    {
        var q16 = checked((int)Math.Round(db * 65536.0, MidpointRounding.AwayFromZero));
        BinaryPrimitives.WriteInt32LittleEndian(destination, q16);
    }

    private static double ReadDbQ16(ReadOnlySpan<byte> source) =>
        BinaryPrimitives.ReadInt32LittleEndian(source) / 65536.0;

    private static bool IsZero(ReadOnlySpan<byte> source, int used, int capacity)
    {
        for (var index = used; index < capacity; index++)
            if (source[index] != 0) return false;
        return true;
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
        (reply.Type is ControlMessageType.Ack or ControlMessageType.Error ||
         request.Type == ControlMessageType.DeviceCatalogRequest &&
         reply.Type == ControlMessageType.DeviceCatalogSnapshot ||
         request.Type == ControlMessageType.ControlStatusRequest &&
         reply.Type == ControlMessageType.ControlStatusSnapshot ||
         request.Type == ControlMessageType.SessionCatalogRequest &&
         reply.Type == ControlMessageType.SessionCatalogSnapshot);
}

public sealed class ControlCommandFactoryV1
{
    private readonly IpcRequestSession _requests = new();

    public IpcEnvelopeV1 Hello() => _requests.Create(ControlMessageType.Hello);

    public IpcEnvelopeV1 SetVolume(double requestedDb, bool mute, ulong generation,
                                   string? outputGroup = null) =>
        _requests.Create(ControlMessageType.VolumeNotification,
            string.IsNullOrWhiteSpace(outputGroup)
                ? ControlPayloadsV1.EncodeVolumeNotification(requestedDb, mute, generation)
                : ControlPayloadsV1.EncodeGroupedVolumeNotification(outputGroup, requestedDb, mute,
                                                                     generation));

    public IpcEnvelopeV1 CommitGraph() => _requests.Create(ControlMessageType.GraphCommit);

    public IpcEnvelopeV1 RollbackGraph() => _requests.Create(ControlMessageType.GraphRollback);

    public IpcEnvelopeV1 ApplyScene(string sceneId, string outputGroup) =>
        _requests.Create(ControlMessageType.SceneApply,
            ControlPayloadsV1.EncodeSceneApply(sceneId, outputGroup));

    public IpcEnvelopeV1 SwitchDevice(PhysicalDeviceCard device) =>
        _requests.Create(ControlMessageType.DeviceSwitch,
            ControlPayloadsV1.EncodeDeviceSwitch(device.EndpointId, device.Channels,
                                                 device.SampleRate, device.BufferFrames,
                                                 device.LastSequence));

    public IpcEnvelopeV1 RequestDeviceCatalog() =>
        _requests.Create(ControlMessageType.DeviceCatalogRequest);

    public IpcEnvelopeV1 RequestSessionCatalog() =>
        _requests.Create(ControlMessageType.SessionCatalogRequest);

    public IpcEnvelopeV1 SetSessionVolume(ulong handle, double requestedDb, bool mute,
                                          ulong catalogSequence) =>
        _requests.Create(ControlMessageType.SessionVolumeCommand,
            ControlPayloadsV1.EncodeSessionVolumeCommand(handle, requestedDb, mute,
                                                          catalogSequence));

    public IpcEnvelopeV1 SetSessionRoute(ulong handle, ulong catalogSequence,
                                         string laneId, string outputGroup) =>
        _requests.Create(ControlMessageType.SessionRouteCommand,
            ControlPayloadsV1.EncodeSessionRouteCommand(handle, catalogSequence, laneId,
                                                        outputGroup));

    public IpcEnvelopeV1 UpsertSessionRouteRule(ulong catalogSequence,
                                                string ruleId,
                                                string appId,
                                                string displayName,
                                                string laneId,
                                                string outputGroup,
                                                int priority = 0,
                                                double makeupGainDb = 0.0,
                                                bool enabled = true,
                                                SessionRouteRuleGainOwnerV1 gainOwner =
                                                    SessionRouteRuleGainOwnerV1.WindowsSession) =>
        _requests.Create(ControlMessageType.SessionRouteRuleCommand,
            ControlPayloadsV1.EncodeSessionRouteRuleCommand(new SessionRouteRuleCommandV1(
                1U, priority, makeupGainDb, SessionRouteRuleOperationV1.Upsert, enabled,
                gainOwner, catalogSequence, ruleId, appId, displayName, laneId, outputGroup)));

    public IpcEnvelopeV1 RemoveSessionRouteRule(ulong catalogSequence, string ruleId) =>
        _requests.Create(ControlMessageType.SessionRouteRuleCommand,
            ControlPayloadsV1.EncodeSessionRouteRuleCommand(new SessionRouteRuleCommandV1(
                1U, 0, 0.0, SessionRouteRuleOperationV1.Remove, true,
                SessionRouteRuleGainOwnerV1.WindowsSession, catalogSequence, ruleId,
                string.Empty, string.Empty, string.Empty, string.Empty)));

    public IpcEnvelopeV1 ClearSessionRouteRules(ulong catalogSequence) =>
        _requests.Create(ControlMessageType.SessionRouteRuleCommand,
            ControlPayloadsV1.EncodeSessionRouteRuleCommand(new SessionRouteRuleCommandV1(
                1U, 0, 0.0, SessionRouteRuleOperationV1.Clear, true,
                SessionRouteRuleGainOwnerV1.WindowsSession, catalogSequence,
                string.Empty, string.Empty, string.Empty, string.Empty, string.Empty)));

    public IpcEnvelopeV1 RequestControlStatus() =>
        _requests.Create(ControlMessageType.ControlStatusRequest);
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

        var response = await ReadFrameAsync(cancellationToken).ConfigureAwait(false);
        if (response.RequestId != request.RequestId)
            throw new InvalidDataException("Control worker response request ID does not match.");
        return response;
    }

    // Reads an unsolicited engine frame, such as DeviceCatalogSnapshot. A
    // caller-owned worker decides whether to apply it to the ViewModel.
    public async Task<IpcEnvelopeV1> ReadFrameAsync(CancellationToken cancellationToken = default)
    {
        var stream = _stream ?? throw new InvalidOperationException("Control pipe is not connected.");
        var length = new byte[sizeof(uint)];
        await ReadExactAsync(stream, length, cancellationToken).ConfigureAwait(false);
        var responseLength = BinaryPrimitives.ReadUInt32LittleEndian(length);
        if (responseLength < IpcCodecV1.HeaderBytes ||
            responseLength > IpcCodecV1.HeaderBytes + IpcCodecV1.MaxPayloadBytes)
            throw new InvalidDataException("Control worker returned an invalid frame length.");
        var responseBytes = new byte[checked((int)responseLength)];
        await ReadExactAsync(stream, responseBytes, cancellationToken).ConfigureAwait(false);
        if (!IpcCodecV1.TryDecode(responseBytes, out var response, out var error) || response is null)
            throw new InvalidDataException($"Control worker returned an invalid frame: {error}.");
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
