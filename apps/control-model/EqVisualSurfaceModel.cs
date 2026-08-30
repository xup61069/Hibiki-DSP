// SPDX-License-Identifier: GPL-3.0-only

using System.ComponentModel;
using System.Runtime.CompilerServices;

namespace Hibiki.ControlModel;

public enum EqVisualSourceV1
{
    None = 0,
    EqualLoudness = 1,
    AdaptiveCorrection = 2,
}

public sealed record EqVisualPointV1(double FrequencyHz, double GainDb)
{
    public static bool IsValid(EqVisualPointV1? point)
    {
        return point is not null &&
               double.IsFinite(point.FrequencyHz) &&
               point.FrequencyHz >= 20.0 &&
               point.FrequencyHz <= 20000.0 &&
               double.IsFinite(point.GainDb) &&
               point.GainDb >= -24.0 &&
               point.GainDb <= 24.0;
    }
}

public sealed record EqVisualFrameV1(
    ulong Sequence,
    EqVisualSourceV1 Source,
    IReadOnlyList<EqVisualPointV1> Points)
{
    public bool IsValid =>
        Sequence != 0UL &&
        Points is not null &&
        Points.Count is >= 4 and <= 32 &&
        Points.All(point => EqVisualPointV1.IsValid(point)) &&
        Points.Zip(Points.Skip(1), (left, right) =>
            right.FrequencyHz > left.FrequencyHz).All(isIncreasing => isIncreasing) &&
        Enum.IsDefined(Source);
}

public sealed class EqVisualSurfaceModelV1 : INotifyPropertyChanged
{
    public const int MinPoints = 4;
    public const int MaxPoints = 32;
    private const double TransitionSeconds = 0.18;
    internal static readonly DateTimeOffset EpochUtc = new(2020, 1, 1, 0, 0, 0, TimeSpan.Zero);

    private IReadOnlyList<EqVisualPointV1> _points =
        new EqVisualPointV1[] { new(31, 0), new(250, 0), new(1000, 0), new(8000, 0) };
    private IReadOnlyList<EqVisualPointV1> _targetPoints = _initialPoints();
    private EqVisualSourceV1 _source = EqVisualSourceV1.None;
    private DateTimeOffset _transitionStartUtc = DateTimeOffset.MinValue;
    private Func<DateTimeOffset> _utcNowProvider = static () => DateTimeOffset.UtcNow;
    private bool _hasConfirmedFrame;
    private ulong _lastAppliedSequence;

    private static IReadOnlyList<EqVisualPointV1> _initialPoints() =>
        new EqVisualPointV1[]
        {
            new(31.0, 0.0), new(250.0, 0.0), new(1000.0, 0.0), new(8000.0, 0.0),
        };

    public event PropertyChangedEventHandler? PropertyChanged;

    public IReadOnlyList<EqVisualPointV1> Points => _points;
    public IReadOnlyList<EqVisualPointV1> TargetPoints => _targetPoints;
    public EqVisualSourceV1 Source => _source;
    public bool HasConfirmedFrame => _hasConfirmedFrame;
    public ulong LastAppliedSequence => _lastAppliedSequence;
    public string StateText
    {
        get
        {
            if (!_hasConfirmedFrame)
            {
                return Source switch
                {
                    EqVisualSourceV1.EqualLoudness => "等響度：尚未確認；不顯示變化",
                    EqVisualSourceV1.AdaptiveCorrection => "自適應校正：尚未確認；不顯示變化",
                    _ => "等化器：離線；等待引擎確認",
                };
            }
            return Source switch
            {
                EqVisualSourceV1.EqualLoudness => "等響度補償已更新",
                EqVisualSourceV1.AdaptiveCorrection => _targetPoints.Count > 0
                    ? $"自適應低頻校正已更新（{_targetPoints[0].FrequencyHz:0.#} Hz {_targetPoints[0].GainDb:+0.0;-0.0;0.0} dB）"
                    : "自適應低頻校正已更新",
                _ => "等化器狀態已同步",
            };
        }
    }

    public double TransitionProgress
    {
        get
        {
            if (_transitionStartUtc == DateTimeOffset.MinValue) return 1.0;
            var seconds = (_utcNowProvider() - _transitionStartUtc).TotalSeconds;
            return Math.Clamp(seconds / TransitionSeconds, 0.0, 1.0);
        }
    }

    public void SetTransitionClockForTesting(Func<DateTimeOffset>? utcNowProvider)
    {
        _utcNowProvider = utcNowProvider ?? (static () => DateTimeOffset.UtcNow);
    }

    public bool ApplyFrame(EqVisualFrameV1 frame)
    {
        if (frame is null || !frame.IsValid) return false;
        var source = frame.Source;
        var target = frame.Points.ToArray();
        var start = _hasConfirmedFrame && Source != EqVisualSourceV1.None ? _points : _initialPoints();
        if (!start.Select(point => point.FrequencyHz).SequenceEqual(target.Select(point => point.FrequencyHz)))
        {
            start = _initialPoints();
        }
        _points = start;
        _targetPoints = target;
        _source = source;
        _transitionStartUtc = _utcNowProvider();
        _hasConfirmedFrame = true;
        _lastAppliedSequence = frame.Sequence;
        OnPropertyChanged(nameof(Points));
        OnPropertyChanged(nameof(TargetPoints));
        OnPropertyChanged(nameof(Source));
        OnPropertyChanged(nameof(HasConfirmedFrame));
        OnPropertyChanged(nameof(StateText));
        OnPropertyChanged(nameof(TransitionProgress));
        return true;
    }

    public bool Reset()
    {
        if (!_hasConfirmedFrame &&
            Source == EqVisualSourceV1.None &&
            ReferenceEquals(_points, TargetPoints)) return false;
        _points = _targetPoints = _initialPoints();
        _source = EqVisualSourceV1.None;
        _transitionStartUtc = DateTimeOffset.MinValue;
        _hasConfirmedFrame = false;
        OnPropertyChanged(nameof(Points));
        OnPropertyChanged(nameof(TargetPoints));
        OnPropertyChanged(nameof(Source));
        OnPropertyChanged(nameof(HasConfirmedFrame));
        OnPropertyChanged(nameof(StateText));
        OnPropertyChanged(nameof(TransitionProgress));
        return true;
    }

    private void OnPropertyChanged([CallerMemberName] string? propertyName = null) =>
        PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(propertyName));
}
