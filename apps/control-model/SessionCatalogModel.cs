// SPDX-License-Identifier: GPL-3.0-only

namespace Hibiki.ControlModel;

public enum SessionCatalogRouteStateV1
{
    Ready,
    Pending,
    Degraded,
    Unavailable
}

public sealed record SessionCatalogEntryV1(
    ulong Handle,
    bool Active,
    SessionCatalogRouteStateV1 RouteState,
    bool VolumeAvailable,
    double RequestedDb,
    bool Muted,
    string Name,
    string AppId,
    string LaneId,
    string OutputGroup)
{
    public string DisplayName => string.IsNullOrWhiteSpace(Name)
        ? (string.IsNullOrWhiteSpace(AppId) ? "Windows 音訊工作階段" : AppId)
        : Name;

    public string RouteStateLabel => RouteState switch
    {
        SessionCatalogRouteStateV1.Ready => "已準備",
        SessionCatalogRouteStateV1.Pending => "等待路由",
        SessionCatalogRouteStateV1.Degraded => "降級中",
        SessionCatalogRouteStateV1.Unavailable => "目前不可用",
        _ => "狀態未知"
    };

    public string AccessibleSummary =>
        $"{DisplayName}：{RouteStateLabel}。輸出 {OutputGroup}。";
}
