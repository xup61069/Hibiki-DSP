// SPDX-License-Identifier: GPL-3.0-only

using System.ComponentModel;
using Hibiki.ControlModel;

namespace Hibiki.DesktopPreview;

internal static class Program
{
    [STAThread]
    private static void Main()
    {
        ApplicationConfiguration.Initialize();
        var viewModel = new EasyControlViewModel();
        viewModel.LoadCustomScenes(out _);
        viewModel.LoadRouteRules(out _);
        Application.Run(new PreviewForm(viewModel));
    }
}

internal sealed class PreviewForm : Form
{
    private readonly EasyControlViewModel _viewModel;
    private readonly Label _connection = new() { AutoSize = true };
    private readonly Label _devices = new() { AutoSize = false, Width = 550, Height = 48 };
    private readonly Label _status = new() { AutoSize = false, Height = 48 };
    private readonly Label _routes = new() { AutoSize = false, Width = 550, Height = 58 };
    private readonly Label _sessions = new() { AutoSize = false, Width = 550, Height = 72 };
    private readonly ComboBox _sessionSelector = new() { Width = 460, DropDownStyle = ComboBoxStyle.DropDownList, AccessibleName = "選取 Expert App" };
    private readonly TrackBar _sessionVolume = new() { Minimum = -60, Maximum = 0, TickFrequency = 5, Width = 460, AccessibleName = "選取 App 音量分貝" };
    private readonly Button _applySessionVolume = new() { Text = "套用選取 App 音量", AutoSize = true, AccessibleName = "套用選取 App 音量" };
    private readonly TextBox _sessionLane = new() { Width = 220, PlaceholderText = "Lane ID", AccessibleName = "Lane ID 輸入欄" };
    private readonly TextBox _sessionOutput = new() { Width = 220, PlaceholderText = "Output Group", AccessibleName = "Output Group 輸入欄" };
    private readonly Button _applySessionRoute = new() { Text = "套用選取 App 路由", AutoSize = true, AccessibleName = "套用選取 App 路由" };
    private readonly Label _effective = new() { AutoSize = true };
    private readonly ComboBox _scenes = new() { Width = 460, DropDownStyle = ComboBoxStyle.DropDownList, AccessibleName = "選取情境設定檔" };
    private readonly ComboBox _irModes = new() { Width = 460, DropDownStyle = ComboBoxStyle.DropDownList, AccessibleName = "選取 IR 模式" };
    private readonly TrackBar _irStrength = new() { Minimum = 0, Maximum = 100, TickFrequency = 10, Width = 460, AccessibleName = "IR 強度百分比" };
    private readonly Label _irStatus = new() { AutoSize = false, Width = 550, Height = 58 };
    private readonly Button _loadIr = new() { Text = "載入 IR WAV 並準備", AutoSize = true, AccessibleName = "載入 IR WAV 並準備" };
    private readonly TrackBar _volume = new() { Minimum = -60, Maximum = 0, TickFrequency = 5, Width = 460, AccessibleName = "主音量分貝" };
    private readonly Button _enhance = new() { Text = "一鍵改善", AutoSize = true, Margin = new Padding(3, 12, 3, 3), AccessibleName = "一鍵改善" };
    private readonly System.Windows.Forms.Timer _statusTimer = new() { Interval = 1000 };
    private bool _updatingScene;
    private bool _updatingSession;
    private bool _statusRefreshActive;

    internal PreviewForm(EasyControlViewModel viewModel)
    {
        _viewModel = viewModel;
        Text = "Hibiki DSP — Compatibility Preview";
        ClientSize = new Size(620, 540);
        StartPosition = FormStartPosition.CenterScreen;
        Font = new Font("Segoe UI", 10);

        var panel = new FlowLayoutPanel { Dock = DockStyle.Fill, Padding = new Padding(24), FlowDirection = FlowDirection.TopDown, WrapContents = false, AutoScroll = true };
        panel.Controls.Add(new Label { Text = "Hibiki DSP", AutoSize = true, Font = new Font("Segoe UI", 24, FontStyle.Bold) });
        panel.Controls.Add(new Label { Text = "本機 Compatibility Preview：自帶 .NET runtime，不需要 Windows App Runtime；不含 driver、系統攔截或正式音訊處理。", AutoSize = false, Width = 550, Height = 42 });
        panel.Controls.Add(new Label { Text = "離線預覽模式：先啟動 user-space Engine Preview，才能測試場景或音量命令。", AutoSize = true, ForeColor = Color.DimGray });
        panel.Controls.Add(new Label { Text = "輸出群組", AutoSize = true, Margin = new Padding(3, 12, 3, 0) });
        var groups = new ComboBox { Width = 460, DropDownStyle = ComboBoxStyle.DropDownList, AccessibleName = "選取輸出群組", DataSource = _viewModel.OutputGroups.ToList(), DisplayMember = "Name", ValueMember = "Id" };
        groups.SelectedIndexChanged += (_, _) => { if (groups.SelectedValue is string id) _viewModel.SelectedOutputGroup = id; };
        panel.Controls.Add(groups);
        panel.Controls.Add(new Label { Text = "本機裝置 catalog（僅 metadata）", AutoSize = true, Margin = new Padding(3, 12, 3, 0) });
        panel.Controls.Add(_devices);
        var connect = new Button { Text = "嘗試連接已啟動的 Hibiki 引擎", AutoSize = true, Margin = new Padding(3, 12, 3, 3), AccessibleName = "嘗試連接已啟動的 Hibiki 引擎" };
        connect.Click += async (_, _) => { await _viewModel.ConnectAsync(TimeSpan.FromSeconds(3)); RefreshView(); };
        panel.Controls.Add(connect);
        panel.Controls.Add(_connection);
        panel.Controls.Add(new Label { Text = "場景", AutoSize = true, Margin = new Padding(3, 12, 3, 0) });
        _scenes.DataSource = _viewModel.Scenes.ToList();
        _scenes.DisplayMember = "Name";
        _scenes.ValueMember = "Id";
        _scenes.SelectedIndexChanged += async (_, _) =>
        {
            if (_updatingScene || _scenes.SelectedValue is not string id || !_viewModel.IsConnected)
                return;
            await _viewModel.SelectSceneAsync(id);
            RefreshView();
        };
        panel.Controls.Add(_scenes);
        panel.Controls.Add(new Label { Text = "IR 檔案與相位", AutoSize = true, Margin = new Padding(3, 12, 3, 0) });
        _irModes.DataSource = _viewModel.IrPhaseModeOptions.ToList();
        _irModes.DisplayMember = "Label";
        _irModes.ValueMember = "Mode";
        _irModes.SelectedValueChanged += (_, _) =>
        {
            if (_irModes.SelectedValue is IrPhaseMode mode)
                _viewModel.IrPhaseMode = mode;
            RefreshView();
        };
        _irStrength.ValueChanged += (_, _) =>
        {
            _viewModel.IrPhaseStrength = _irStrength.Value / 100.0;
            RefreshView();
        };
        panel.Controls.Add(_irModes);
        panel.Controls.Add(_irStrength);
        _loadIr.Click += async (_, _) =>
        {
            using var dialog = new OpenFileDialog
            {
                Filter = "IR WAV 檔案 (*.wav)|*.wav|所有檔案 (*.*)|*.*",
                CheckFileExists = true,
                Multiselect = false,
                Title = "選擇 IR WAV"
            };
            if (dialog.ShowDialog(this) == DialogResult.OK)
            {
                await _viewModel.PrepareIrAsync(dialog.FileName);
                RefreshView();
            }
        };
        panel.Controls.Add(_loadIr);
        panel.Controls.Add(_irStatus);
        _enhance.Click += async (_, _) => { await _viewModel.OneTapEnhanceAsync(); RefreshView(); };
        panel.Controls.Add(_enhance);
        panel.Controls.Add(new Label { Text = "系統音量（dB）", AutoSize = true, Margin = new Padding(3, 12, 3, 0) });
        _volume.Value = (int)_viewModel.RequestedVolumeDb;
        _volume.ValueChanged += async (_, _) => { _viewModel.RequestedVolumeDb = _volume.Value; if (_viewModel.IsConnected) await _viewModel.QueueVolumeAsync(); RefreshView(); };
        panel.Controls.Add(_volume);
        panel.Controls.Add(_effective);
        panel.Controls.Add(_routes);
        panel.Controls.Add(new Label { Text = "Expert App／工作階段（需以 -EnableSessionRouting 啟動）", AutoSize = true, Margin = new Padding(3, 12, 3, 0) });
        panel.Controls.Add(_sessions);
        _sessionSelector.SelectedIndexChanged += (_, _) =>
        {
            if (_updatingSession || _sessionSelector.SelectedItem is not SessionCatalogEntryV1 entry) return;
            _viewModel.SelectSession(entry.Handle);
            SyncSessionControls();
            RefreshView();
        };
        panel.Controls.Add(_sessionSelector);
        _sessionVolume.ValueChanged += async (_, _) =>
        {
            if (_updatingSession || _viewModel.SelectedSession is null) return;
            _viewModel.SessionVolumeDb = _sessionVolume.Value;
            RefreshView();
        };
        panel.Controls.Add(_sessionVolume);
        _applySessionVolume.Click += async (_, _) =>
        {
            await _viewModel.ApplySelectedSessionVolumeAsync();
            RefreshView();
        };
        panel.Controls.Add(_applySessionVolume);
        _sessionLane.TextChanged += (_, _) =>
        {
            if (!_updatingSession) _viewModel.SessionRouteLaneId = _sessionLane.Text;
        };
        _sessionOutput.TextChanged += (_, _) =>
        {
            if (!_updatingSession) _viewModel.SessionRouteOutputGroup = _sessionOutput.Text;
        };
        var sessionRouteFields = new FlowLayoutPanel
        {
            AutoSize = true,
            FlowDirection = FlowDirection.LeftToRight,
            WrapContents = false
        };
        sessionRouteFields.Controls.Add(_sessionLane);
        sessionRouteFields.Controls.Add(_sessionOutput);
        panel.Controls.Add(sessionRouteFields);
        _applySessionRoute.Click += async (_, _) =>
        {
            await _viewModel.ApplySelectedSessionRouteAsync();
            RefreshView();
        };
        panel.Controls.Add(_applySessionRoute);
        panel.Controls.Add(_status);
        Controls.Add(panel);
        _viewModel.PropertyChanged += OnViewModelChanged;
        _statusTimer.Tick += async (_, _) =>
        {
            if (!_viewModel.IsConnected || _viewModel.IsBusy || _statusRefreshActive) return;
            _statusRefreshActive = true;
            try
            {
                await _viewModel.RefreshControlStatusAsync();
                RefreshView();
            }
            finally
            {
                _statusRefreshActive = false;
            }
        };
        _statusTimer.Start();
        FormClosed += async (_, _) =>
        {
            _statusTimer.Stop();
            _statusTimer.Dispose();
            await _viewModel.DisconnectAsync();
        };
        Shown += async (_, _) =>
        {
            // The compatibility preview is deliberately useful as a single
            // double-click experience when the local Engine Preview is already
            // running, while still remaining safe when no engine is present.
            await _viewModel.ConnectAsync(TimeSpan.FromSeconds(1));
            RefreshView();
        };
        RefreshView();
    }

    private void OnViewModelChanged(object? sender, PropertyChangedEventArgs e)
    {
        if (!IsDisposed && IsHandleCreated) BeginInvoke(RefreshView);
    }

    private void RefreshView()
    {
        _connection.Text = _viewModel.ConnectionStatusText;
        var renderDevices = _viewModel.PhysicalDevices
            .Where(device => device.Flow == PhysicalDeviceFlowV1.Render)
            .ToArray();
        var captureDevices = _viewModel.PhysicalDevices
            .Where(device => device.Flow == PhysicalDeviceFlowV1.Capture)
            .ToArray();
        var defaultRender = renderDevices.FirstOrDefault(device => device.IsDefault)?.DisplayName;
        _devices.Text = _viewModel.PhysicalDevices.Count == 0
            ? "裝置 catalog：尚未收到引擎快照；不會自行枚舉或建立裝置。"
            : $"裝置 catalog：{_viewModel.PhysicalDevices.Count} 筆（render {renderDevices.Length}／capture {captureDevices.Length}）；" +
              $"預設輸出：{defaultRender ?? "未指定"}\r\n只顯示 metadata；physical sink 與實體切換尚未啟用。";
        _enhance.Enabled = _viewModel.IsConnected;
        _scenes.Enabled = _viewModel.IsConnected;
        _volume.Enabled = _viewModel.IsConnected;
        var sessions = _viewModel.SessionCatalog.ToArray();
        _sessions.Text = sessions.Length == 0
            ? "App catalog：尚未同步；請以 -EnableSessionRouting 啟動引擎，或目前沒有可控制的工作階段。"
            : $"App catalog：{sessions.Length} 筆；只顯示 bounded metadata。套用 App 音量會寫入 Windows session，" +
              "實體 per-App 重新送出仍未驗證。";
        _updatingSession = true;
        try
        {
            var selectedHandle = _viewModel.SelectedSessionHandle;
            _sessionSelector.DataSource = null;
            _sessionSelector.DisplayMember = nameof(SessionCatalogEntryV1.DisplayName);
            _sessionSelector.ValueMember = nameof(SessionCatalogEntryV1.Handle);
            _sessionSelector.DataSource = sessions;
            if (selectedHandle != 0UL)
            {
                var selectedIndex = Array.FindIndex(sessions, entry => entry.Handle == selectedHandle);
                if (selectedIndex >= 0) _sessionSelector.SelectedIndex = selectedIndex;
            }
            SyncSessionControls();
        }
        finally
        {
            _updatingSession = false;
        }
        var hasSession = _viewModel.HasSelectedSession;
        _sessionSelector.Enabled = _viewModel.IsConnected && sessions.Length > 0;
        _sessionVolume.Enabled = _viewModel.IsConnected && hasSession && _viewModel.SelectedSession?.VolumeAvailable == true;
        _applySessionVolume.Enabled = _sessionVolume.Enabled;
        _sessionLane.Enabled = _viewModel.IsConnected && hasSession;
        _sessionOutput.Enabled = _viewModel.IsConnected && hasSession;
        _applySessionRoute.Enabled = _viewModel.IsConnected && hasSession;
        _loadIr.Enabled = _viewModel.IsConnected && _viewModel.IrPhaseMode != IrPhaseMode.Bypass;
        _irStrength.Enabled = _viewModel.IrPhaseMode is IrPhaseMode.MixedPhase or IrPhaseMode.LinearPhase;
        _effective.Text = $"實際有效音量：{_viewModel.EffectiveVolumeDb:0.0} dB；{_viewModel.VolumeOriginText}；{_viewModel.VolumeActuatorText}";
        _routes.Text = _viewModel.Expert.RouteHealthAccessibleSummary;
        _status.Text = _viewModel.StatusText;
        _irStatus.Text = $"{_viewModel.IrPhaseModeText}；實測延遲 {_viewModel.IrAddedDelayMs:0.0} ms。\r\n{_viewModel.IrPrepareStatus}";
        var requested = Math.Clamp((int)Math.Round(_viewModel.RequestedVolumeDb), _volume.Minimum, _volume.Maximum);
        if (_volume.Value != requested) _volume.Value = requested;
        if (_irModes.SelectedValue is not IrPhaseMode currentMode || currentMode != _viewModel.IrPhaseMode)
            _irModes.SelectedValue = _viewModel.IrPhaseMode;
        var strength = Math.Clamp((int)Math.Round(_viewModel.IrPhaseStrength * 100.0),
                                  _irStrength.Minimum, _irStrength.Maximum);
        if (_irStrength.Value != strength) _irStrength.Value = strength;
        var selectedScene = _viewModel.SelectedScene?.Id;
        if (selectedScene is not null && !string.Equals(_scenes.SelectedValue as string, selectedScene,
                                                         StringComparison.Ordinal))
        {
            _updatingScene = true;
            _scenes.SelectedValue = selectedScene;
            _updatingScene = false;
        }
    }

    private void SyncSessionControls()
    {
        var selected = _viewModel.SelectedSession;
        var requested = Math.Clamp((int)Math.Round(_viewModel.SessionVolumeDb),
                                   _sessionVolume.Minimum, _sessionVolume.Maximum);
        if (_sessionVolume.Value != requested) _sessionVolume.Value = requested;
        if (_sessionLane.Text != _viewModel.SessionRouteLaneId)
            _sessionLane.Text = _viewModel.SessionRouteLaneId;
        if (_sessionOutput.Text != _viewModel.SessionRouteOutputGroup)
            _sessionOutput.Text = _viewModel.SessionRouteOutputGroup;
        if (selected is null) _sessionSelector.SelectedIndex = -1;
    }
}
