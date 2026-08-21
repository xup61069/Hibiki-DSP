// SPDX-License-Identifier: GPL-3.0-only

using System.ComponentModel;
using System.Runtime.CompilerServices;

namespace Hibiki.ControlModel;

public sealed record ExpertMatrixRoute(
    string Source,
    string Destination,
    double GainDb,
    bool Enabled);

public sealed record ExpertDspNode(
    string Id,
    string Name,
    string Detail,
    bool Enabled);

public sealed record ExpertVst3Lane(
    string LaneId,
    string Name,
    string Status,
    bool Trusted);

public sealed record ExpertCalibrationSummary(
    string Mode,
    string Device,
    string Status,
    string Detail);

// Read-only Expert surface for the first WinUI shell. It deliberately exposes
// the graph's current shape and safety limits without inventing an IPC edit
// payload. Mutating Matrix/DSP/VST state remains a future versioned command;
// the UI must show that boundary instead of implying an unsent edit succeeded.
public sealed class ExpertSurfaceModel : INotifyPropertyChanged
{
    private bool _isVisible;
    private IReadOnlyList<RouteHealthCardV1> _routeHealth = RouteHealthCatalogV1.Defaults;

    public ExpertSurfaceModel()
    {
        MatrixRoutes =
        [
            new("Input L", "Main L", 0.0, true),
            new("Input R", "Main R", 0.0, true),
            new("Input L", "Surround FL", 0.0, true),
            new("Input R", "Surround FR", 0.0, true)
        ];
        DspGraph =
        [
            new("group-master", "Group Master", "Windows dB + 8 ms ramp", true),
            new("iso226", "Equal-loudness", "ISO 226-derived; calibration required", false),
            new("calibration", "PEQ / IR calibration", "Measured response or imported profile", false),
            new("limiter", "True-peak limiter", "−1 dBTP safety ceiling", true)
        ];
        Vst3Lanes =
        [
            new("lane-0", "Low Latency Lane", "尚未認證 plugin", false),
            new("lane-1", "Movie Lane", "尚未認證 plugin", false)
        ];
        Calibration = new(
            "Relative Compensation",
            "目前輸出群組",
            "未校準",
            "沒有 SPL anchor；不顯示 phon 或聲學保證");
    }

    public event PropertyChangedEventHandler? PropertyChanged;

    public bool IsVisible
    {
        get => _isVisible;
        private set
        {
            if (value == _isVisible) return;
            _isVisible = value;
            OnPropertyChanged();
        }
    }

    public IReadOnlyList<ExpertMatrixRoute> MatrixRoutes { get; }
    public IReadOnlyList<ExpertDspNode> DspGraph { get; }
    public IReadOnlyList<ExpertVst3Lane> Vst3Lanes { get; }
    public ExpertCalibrationSummary Calibration { get; }
    public IReadOnlyList<RouteHealthCardV1> RouteHealth => _routeHealth;

    public string RouteSummary =>
        "每個來源的處理邊界獨立顯示；未收到引擎快照前不宣稱已路由。";

    public string StatusText =>
        IsVisible
            ? "Expert 檢視：目前為唯讀 contract；修改必須經版本化 engine command"
            : "Expert 詳細控制已隱藏";

    public string MatrixSummary => "8×8 Matrix contract；目前顯示已驗證的 lane 範例";
    public string DspSummary => "Graph 順序：Group Master → calibration → limiter";
    public string Vst3Summary => "VST3 只允許隔離程序與認證清單；未認證 lane fail-closed";

    public void SetVisible(bool value)
    {
        IsVisible = value;
        OnPropertyChanged(nameof(StatusText));
    }

    public bool TryApplyRouteHealth(IReadOnlyList<RouteHealthCardV1> cards,
                                    out string error)
    {
        error = string.Empty;
        if (cards is null || cards.Count is < 1 or > 16)
        {
            error = "路由狀態快照超過容量";
            return false;
        }
        var seen = new HashSet<string>(StringComparer.Ordinal);
        foreach (var card in cards)
        {
            if (string.IsNullOrWhiteSpace(card.Id) || card.Id.Length > 31 ||
                card.Id.Any(char.IsControl) || !seen.Add(card.Id) ||
                string.IsNullOrWhiteSpace(card.Name) || string.IsNullOrWhiteSpace(card.Detail) ||
                !Enum.IsDefined(card.State))
            {
                error = "路由狀態快照含有無效或重複身份";
                return false;
            }
        }
        _routeHealth = cards.ToArray();
        OnPropertyChanged(nameof(RouteHealth));
        OnPropertyChanged(nameof(RouteSummary));
        return true;
    }

    private void OnPropertyChanged([CallerMemberName] string? propertyName = null) =>
        PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(propertyName));
}
