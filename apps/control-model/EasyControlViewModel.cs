// SPDX-License-Identifier: GPL-3.0-only

using System.ComponentModel;
using System.Runtime.CompilerServices;

namespace Hibiki.ControlModel;

// WinUI-independent binding surface. The future Window can bind to these
// properties/commands without putting pipe I/O or DSP work on the audio thread.
public sealed class EasyControlViewModel : INotifyPropertyChanged
{
    private readonly EasyControlSession _session = new();
    private readonly ControlCommandFactoryV1 _commands = new();
    private string? _selectedOutputGroup;
    private SceneCard? _selectedScene;
    private string _statusText = "尚未連接 Hibiki 音訊引擎";
    private bool _isExpert;
    private double _requestedVolumeDb = -12.0;
    private bool _muted;
    private ulong _generation;

    public event PropertyChangedEventHandler? PropertyChanged;

    public IReadOnlyList<SceneCard> Scenes => ScenePresetCatalog.EasyDefaults;
    public UiMode Mode => _isExpert ? UiMode.Expert : UiMode.Easy;
    public AudioControlStatus Status => _session.Status;
    public SceneCard? SelectedScene => _selectedScene;
    public string? SelectedOutputGroup
    {
        get => _selectedOutputGroup;
        set
        {
            var normalized = string.IsNullOrWhiteSpace(value) ? null : value.Trim();
            if (normalized == _selectedOutputGroup) return;
            _selectedOutputGroup = normalized;
            OnPropertyChanged();
        }
    }

    public string StatusText
    {
        get => _statusText;
        private set
        {
            if (value == _statusText) return;
            _statusText = value;
            OnPropertyChanged();
        }
    }

    public bool IsExpert
    {
        get => _isExpert;
        set
        {
            if (value == _isExpert) return;
            _isExpert = value;
            _session.SetMode(Mode);
            OnPropertyChanged();
            OnPropertyChanged(nameof(Mode));
        }
    }

    public double RequestedVolumeDb
    {
        get => _requestedVolumeDb;
        set
        {
            if (!double.IsFinite(value)) return;
            var clamped = Math.Clamp(value, -144.0, 12.0);
            if (Math.Abs(clamped - _requestedVolumeDb) < 1e-9) return;
            _requestedVolumeDb = clamped;
            OnPropertyChanged();
        }
    }

    public bool Muted
    {
        get => _muted;
        set
        {
            if (value == _muted) return;
            _muted = value;
            OnPropertyChanged();
        }
    }

    public IpcEnvelopeV1? LastCommand { get; private set; }

    public bool OneTapEnhance()
    {
        var result = _session.OneTapEnhance(SelectedOutputGroup);
        _selectedScene = result.Scene;
        StatusText = result.Message ?? (result.Succeeded ? "已控制" : "控制失敗");
        OnPropertyChanged(nameof(SelectedScene));
        OnPropertyChanged(nameof(Status));
        if (!result.Succeeded) return false;
        LastCommand = _commands.ApplyScene(_selectedScene!.Id, _session.ActiveOutputGroup!);
        OnPropertyChanged(nameof(LastCommand));
        return true;
    }

    public bool SelectScene(string sceneId)
    {
        if (!_session.SelectScene(sceneId))
        {
            StatusText = "找不到這個場景";
            return false;
        }
        _selectedScene = ScenePresetCatalog.EasyDefaults.First(item => item.Id == sceneId);
        StatusText = $"已選擇 {_selectedScene.Name}";
        OnPropertyChanged(nameof(SelectedScene));
        if (_session.ActiveOutputGroup is not null)
        {
            LastCommand = _commands.ApplyScene(_selectedScene.Id, _session.ActiveOutputGroup);
            OnPropertyChanged(nameof(LastCommand));
        }
        return true;
    }

    public IpcEnvelopeV1 BuildVolumeCommand()
    {
        _generation++;
        LastCommand = _commands.SetVolume(RequestedVolumeDb, Muted, _generation);
        OnPropertyChanged(nameof(LastCommand));
        return LastCommand;
    }

    private void OnPropertyChanged([CallerMemberName] string? propertyName = null) =>
        PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(propertyName));
}
