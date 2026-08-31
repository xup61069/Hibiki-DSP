// SPDX-License-Identifier: GPL-3.0-only

using System.ComponentModel;
using System.Globalization;
using System.Runtime.CompilerServices;
using System.Linq;

namespace Hibiki.ControlModel;

public sealed record ExpertMatrixRoute(
    string Source,
    string Destination,
    double GainDb,
    bool Enabled)
{
    public string GainDisplayText => $"{GainDb:0.0} dB";
}

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

public sealed record ExpertWizardPeqFilterSummary(
    int Index,
    double FrequencyHz,
    double GainDb,
    double Q)
{
    public string AccessibleText =>
        string.Create(CultureInfo.InvariantCulture,
            $"Filter {Index}: {FrequencyHz:0.#} Hz, {GainDb:+0.##;-0.##;0} dB, Q {Q:0.00}");
}

public sealed record ExpertCalibrationWizardState(
    string TargetCurveName,
    int PointCount,
    int FilterCount,
    bool Limited,
    string Diagnostic,
    IReadOnlyList<ExpertWizardPeqFilterSummary> Filters);

// Read-only Expert surface for the first WinUI shell. It deliberately exposes
// the graph's current shape and safety limits without inventing an IPC edit
// payload. Mutating Matrix/DSP/VST state remains a future versioned command;
// the UI must show that boundary instead of implying an unsent edit succeeded.
public sealed class ExpertSurfaceModel : INotifyPropertyChanged
{
    private bool _isVisible;
    private IReadOnlyList<RouteHealthCardV1> _routeHealth = RouteHealthCatalogV1.Defaults;
    private CalibrationTargetCurveIdV1 _wizardTargetCurve = CalibrationTargetCurveIdV1.Flat;
    private ExpertCalibrationWizardState? _wizardState;

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
            new("equal-loudness", "Equal-loudness", "equal-loudness-derived; calibration required", false),
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

    public string RouteHealthAccessibleSummary =>
        RouteHealth.Count == 0
            ? "路由狀態：尚未收到引擎快照"
            : "路由狀態：" + string.Join("／", RouteHealth.Select(route => route.AccessibleSummary));

    public string StatusText =>
        IsVisible
            ? "Expert 檢視：目前為唯讀 contract；修改必須經版本化 engine command"
            : "Expert 詳細控制已隱藏";

    public string MatrixSummary => "8×8 Matrix contract；目前顯示已驗證的 lane 範例";
    public string DspSummary => "Graph 順序：Group Master → calibration → limiter";
    public string Vst3Summary => "VST3 只允許隔離程序與認證清單；未認證 lane fail-closed";

    public CalibrationTargetCurveIdV1 WizardTargetCurveId => _wizardTargetCurve;

    public string WizardTargetCurveName =>
        CalibrationCompilerV1.TargetCurveName(_wizardTargetCurve);

    public string WizardCurveDisplayName => _wizardTargetCurve switch
    {
        CalibrationTargetCurveIdV1.Flat => "Flat 全平",
        CalibrationTargetCurveIdV1.HarmanInEar => "Harman 入耳式",
        CalibrationTargetCurveIdV1.HarmanOverEar => "Harman 罩耳式",
        _ => "未知目標曲線",
    };

    public int WizardTargetCurveIndex
    {
        get => (int)_wizardTargetCurve;
        set => SetWizardTargetCurve((CalibrationTargetCurveIdV1)value);
    }

    public bool HasWizardResult => _wizardState is not null;

    public ExpertCalibrationWizardState? WizardState => _wizardState;

    public IReadOnlyList<ExpertWizardPeqFilterSummary> WizardPeqPreview =>
        _wizardState?.Filters ?? Array.Empty<ExpertWizardPeqFilterSummary>();

    public string WizardStatusText
    {
        get
        {
            var boundary = "此預覽只在控制平面編譯，不送出 engine 命令。";
            if (_wizardState is null)
            {
                return $"尚未建立 PEQ 預覽；輸入量測點後按下「產生校正 PEQ 預覽」。目前目標曲線：{WizardCurveDisplayName}。{boundary}";
            }

            var limitedNotice = _wizardState.Limited ? "結果受限於 boost/cut 或 filter 上限。" : string.Empty;
            return $"{WizardCurveDisplayName}：已產生 {_wizardState.FilterCount} 段 PEQ 預覽（{_wizardState.PointCount} 點）。{limitedNotice}{boundary}";
        }
    }

    public void SetVisible(bool value)
    {
        IsVisible = value;
        OnPropertyChanged(nameof(StatusText));
    }

    public void SetWizardTargetCurve(CalibrationTargetCurveIdV1 curveId)
    {
        if (!Enum.IsDefined(curveId) || curveId == _wizardTargetCurve)
        {
            return;
        }

        _wizardTargetCurve = curveId;
        OnPropertyChanged(nameof(WizardTargetCurveId));
        OnPropertyChanged(nameof(WizardTargetCurveName));
        OnPropertyChanged(nameof(WizardCurveDisplayName));
        OnPropertyChanged(nameof(WizardTargetCurveIndex));
        OnPropertyChanged(nameof(WizardStatusText));
    }

    public bool TryBuildWizardPeq(
        IReadOnlyList<double>? measuredFrequenciesHz,
        IReadOnlyList<double>? measuredLevelsDb,
        out string error)
    {
        error = string.Empty;
        var response = CalibrationCompilerV1.BuildTargetedResponse(
            deviceId: null,
            sampleRate: 48000.0,
            measuredFrequenciesHz,
            measuredLevelsDb,
            _wizardTargetCurve);
        if (response is null)
        {
            _wizardState = null;
            NotifyWizardChanged();
            error = "量測資料無效：頻率必須嚴格遞增、兩組數量相同、介於 10 Hz–24 kHz 且電平在 −144…+12 dB。";
            return false;
        }

        var compiled = CalibrationCompilerV1.CompileBoundedPeqCorrection(
            response.Points, new CalibrationCompilePolicyV1());
        if (!compiled.Filters.Any() &&
            compiled.Diagnostic.Contains("invalid", StringComparison.Ordinal))
        {
            _wizardState = null;
            NotifyWizardChanged();
            error = "PEQ 編譯失敗：" + compiled.Diagnostic;
            return false;
        }

        var filters = compiled.Filters
            .Select((filter, index) => new ExpertWizardPeqFilterSummary(
                index + 1, filter.FrequencyHz, filter.GainDb, filter.Q))
            .ToArray();
        _wizardState = new ExpertCalibrationWizardState(
            WizardTargetCurveName,
            response.Points.Count,
            filters.Length,
            compiled.Limited,
            compiled.Diagnostic,
            filters);
        NotifyWizardChanged();
        return true;
    }

    private void NotifyWizardChanged()
    {
        OnPropertyChanged(nameof(HasWizardResult));
        OnPropertyChanged(nameof(WizardState));
        OnPropertyChanged(nameof(WizardPeqPreview));
        OnPropertyChanged(nameof(WizardStatusText));
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
            if (string.IsNullOrWhiteSpace(card.Id) ||
                string.IsNullOrWhiteSpace(card.Name) ||
                string.IsNullOrWhiteSpace(card.Detail) ||
                !Utf8TextValidation.IsPrintable(card.Id, 31, allowEmpty: false) ||
                !Utf8TextValidation.IsPrintable(card.Name, 63, allowEmpty: false) ||
                !Utf8TextValidation.IsPrintable(card.Detail, 119, allowEmpty: false) ||
                !seen.Add(card.Id) || !Enum.IsDefined(card.State))
            {
                error = "路由狀態快照含有無效或重複身份";
                return false;
            }
        }
        _routeHealth = cards.ToArray();
        OnPropertyChanged(nameof(RouteHealth));
        OnPropertyChanged(nameof(RouteSummary));
        OnPropertyChanged(nameof(RouteHealthAccessibleSummary));
        return true;
    }

    private void OnPropertyChanged([CallerMemberName] string? propertyName = null) =>
        PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(propertyName));
}
