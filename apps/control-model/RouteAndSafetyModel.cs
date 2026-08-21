// SPDX-License-Identifier: GPL-3.0-only

namespace Hibiki.ControlModel;

// These records are control-plane only. They project engine truth into the
// UI; they do not create a route, change a volume, or imply an adapter exists.
public enum RouteHealthStateV1
{
    Ready,
    Pending,
    Degraded,
    Bypassed,
    Unavailable
}

public sealed record RouteHealthCardV1(
    string Id,
    string Name,
    RouteHealthStateV1 State,
    string Detail,
    bool RequiresUserAction = false)
{
    public string StateLabel => State switch
    {
        RouteHealthStateV1.Ready => "已可用",
        RouteHealthStateV1.Pending => "等待引擎回報",
        RouteHealthStateV1.Degraded => "降級中",
        RouteHealthStateV1.Bypassed => "繞過 Hibiki",
        RouteHealthStateV1.Unavailable => "目前不可用",
        _ => "狀態未知"
    };

    public string AccessibleSummary => $"{Name}：{StateLabel}。{Detail}";
}

public static class RouteHealthCatalogV1
{
    // Conservative defaults are important: a shell starting before the engine
    // must never claim that a route is active.
    public static IReadOnlyList<RouteHealthCardV1> Defaults { get; } =
    [
        new("windows-session", "Windows App／Session", RouteHealthStateV1.Pending,
            "以 endpoint + session instance 識別；等待引擎回報目前 active session。"),
        new("process-loopback", "Process Loopback", RouteHealthStateV1.Pending,
            "只支援 process-tree capture；不等於 Chrome／Edge 單一分頁擷取。"),
        new("browser-tab", "Chrome／Edge 單分頁", RouteHealthStateV1.Pending,
            "需要瀏覽器 MV3 擴充功能，並由使用者點擊啟動。", true),
        new("direct-path", "Vendor ASIO／WASAPI Exclusive", RouteHealthStateV1.Bypassed,
            "這些 RAW 路徑不會透明攔截；請改選 Hibiki ASIO 或 Hibiki Endpoint。")
    ];
}

public enum VolumeStateOriginV1
{
    Windows,
    HibikiUi,
    Safety,
    Scene,
    Session
}

public enum VolumeActuatorV1
{
    InternalDsp,
    DeviceHardware,
    StrictDirect
}

public sealed record VolumeSafetyStateV1(
    double RequestedDb,
    double SafetyCeilingDb,
    double EffectiveDb,
    bool Muted,
    ulong Generation,
    VolumeStateOriginV1 Origin,
    VolumeActuatorV1 Actuator)
{
    public static VolumeSafetyStateV1 Initial(double requestedDb = -12.0) =>
        new(requestedDb, 0.0, Math.Min(requestedDb, 0.0), false, 0,
            VolumeStateOriginV1.Windows, VolumeActuatorV1.InternalDsp);

    public bool IsSafetyCapped => EffectiveDb + 0.05 < RequestedDb;

    public bool IsValid =>
        double.IsFinite(RequestedDb) && RequestedDb is >= -144.0 and <= 12.0 &&
        double.IsFinite(SafetyCeilingDb) && SafetyCeilingDb is >= -144.0 and <= 12.0 &&
        double.IsFinite(EffectiveDb) && EffectiveDb is >= -144.0 and <= 12.0 &&
        EffectiveDb <= RequestedDb + 0.05 &&
        EffectiveDb <= SafetyCeilingDb + 0.05 &&
        Enum.IsDefined(Origin) && Enum.IsDefined(Actuator);

    public string SafetyStatusText => Muted
        ? "已淡出並靜音；解除靜音會沿用安全有效值"
        : IsSafetyCapped
            ? $"安全限制已介入：實際 {EffectiveDb:0.0} dB（要求 {RequestedDb:0.0} dB）"
            : $"安全上限 {SafetyCeilingDb:0.0} dB；目前未截頂";

    public string OriginLabel => Origin switch
    {
        VolumeStateOriginV1.Windows => "Windows 系統音量",
        VolumeStateOriginV1.HibikiUi => "Hibiki UI",
        VolumeStateOriginV1.Safety => "智慧音量保護",
        VolumeStateOriginV1.Scene => "Scene",
        VolumeStateOriginV1.Session => "App／Session",
        _ => "未知來源"
    };

    public string ActuatorLabel => Actuator switch
    {
        VolumeActuatorV1.InternalDsp => "Hibiki DSP（單次套用）",
        VolumeActuatorV1.DeviceHardware => "裝置硬體音量",
        VolumeActuatorV1.StrictDirect => "Strict Direct（系統音量只讀）",
        _ => "未知致動器"
    };
}
