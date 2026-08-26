// SPDX-License-Identifier: GPL-3.0-only

using System.ComponentModel;
using System.Drawing;
using System.Globalization;
using System.Windows.Forms;
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
    private static readonly Color ShellBackground = Color.FromArgb(244, 247, 251);
    private static readonly Color CardBackground = Color.FromArgb(255, 255, 255);
    private static readonly Color GroupBackground = Color.FromArgb(235, 242, 249);
    private static readonly Color TextPrimary = Color.FromArgb(31, 41, 55);
    private static readonly Color TextSecondary = Color.FromArgb(82, 96, 112);
    private static readonly Color Accent = Color.FromArgb(143, 56, 173);
    private static readonly Color AccentHover = Color.FromArgb(121, 42, 151);
    private static readonly Color Border = Color.FromArgb(207, 218, 231);
    private static readonly Color DisabledBackground = Color.FromArgb(216, 225, 235);
    private static readonly Color DisabledText = Color.FromArgb(78, 93, 112);
    private static readonly Color SuccessBackground = Color.FromArgb(228, 246, 236);
    private static readonly Color SuccessText = Color.FromArgb(31, 105, 62);
    private static readonly Color WarningBackground = Color.FromArgb(255, 244, 218);
    private static readonly Color WarningText = Color.FromArgb(125, 82, 0);

    private readonly EasyControlViewModel _viewModel;
    private readonly Label _connection = new()
    {
        AutoSize = false,
        Height = 42,
        TextAlign = ContentAlignment.MiddleLeft,
        AccessibleName = "引擎連線狀態",
        Tag = "connection-status",
    };
    private readonly Label _devices = new() { AutoSize = false, Width = 550, Height = 48 };
    private readonly Label _physicalDeviceEmptyState = new()
    {
        AutoSize = false,
        Height = 48,
        Visible = false,
        AccessibleName = "實體輸出裝置狀態",
        Tag = "empty-state",
    };
    private readonly Label _status = new() { AutoSize = false, Height = 48 };
    private readonly Label _routes = new() { AutoSize = false, Width = 550, Height = 58 };
    private readonly Label _sessions = new() { AutoSize = false, Width = 550, Height = 72 };
    private readonly ComboBox _sessionSelector = new() { Width = 460, DropDownStyle = ComboBoxStyle.DropDownList, AccessibleName = "選取 Expert App" };
    private readonly TextBox _sessionFilter = new() { Width = 460, PlaceholderText = "篩選 App 名稱或 App ID", AccessibleName = "Expert App 清單篩選" };
    private readonly ComboBox _physicalDeviceSelector = new() { Width = 460, DropDownStyle = ComboBoxStyle.DropDownList, AccessibleName = "選取實體輸出裝置" };
    private readonly Button _switchDevice = new() { Text = "切換實體裝置", AutoSize = true, AccessibleName = "切換實體輸出裝置" };
    private readonly Button _refreshDevices = new() { Text = "重新掃描裝置", AutoSize = true, AccessibleName = "重新掃描實體輸出裝置清單" };
    private readonly TrackBar _sessionVolume = new() { Minimum = -60, Maximum = 0, TickFrequency = 5, Width = 460, AccessibleName = "選取 App 音量分貝" };
    private readonly Label _sessionVolumeReadout = new() { AutoSize = true, Width = 80, TextAlign = ContentAlignment.MiddleLeft, AccessibleName = "App 音量分貝數值" };
    private readonly CheckBox _sessionMuted = new() { Text = "App 靜音", AutoSize = true, AccessibleName = "App 工作階段靜音" };
    private readonly Button _applySessionVolume = new() { Text = "套用選取 App 音量", AutoSize = true, AccessibleName = "套用選取 App 音量" };
    private readonly TextBox _sessionLane = new() { Width = 220, PlaceholderText = "Lane ID", AccessibleName = "Lane ID 輸入欄" };
    private readonly TextBox _sessionOutput = new() { Width = 220, PlaceholderText = "Output Group", AccessibleName = "Output Group 輸入欄" };
    private readonly Button _applySessionRoute = new() { Text = "套用選取 App 路由", AutoSize = true, AccessibleName = "套用選取 App 路由" };
    private readonly ListBox _routeRuleList = new() { Width = 550, Height = 110, AccessibleName = "App 路由預設列表" };
    private readonly Label _routeRuleEmptyState = new()
    {
        AutoSize = false,
        Height = 48,
        Visible = false,
        AccessibleName = "App 路由預設空狀態",
        Tag = "empty-state",
    };
    private readonly TextBox _routeRuleId = new() { Width = 260, PlaceholderText = "預設 ID（小寫英文／數字／- _ .）", AccessibleName = "App 路由預設 ID" };
    private readonly TextBox _routeRuleAppId = new() { Width = 260, PlaceholderText = "App ID（例如 game.exe，可留空）", AccessibleName = "App 路由預設 App ID" };
    private readonly TextBox _routeRuleDisplayName = new() { Width = 260, PlaceholderText = "顯示名稱（可留空）", AccessibleName = "App 路由預設顯示名稱" };
    private readonly TextBox _routeRuleLaneId = new() { Width = 260, PlaceholderText = "Lane ID", AccessibleName = "App 路由預設 Lane ID" };
    private readonly TextBox _routeRuleOutputGroup = new() { Width = 260, PlaceholderText = "Output Group（main／low-latency／surround）", AccessibleName = "App 路由預設 Output Group" };
    private readonly NumericUpDown _routeRulePriority = new() { Minimum = -1000000, Maximum = 1000000, Value = 0, Width = 160, AccessibleName = "App 路由預設優先級" };
    private readonly NumericUpDown _routeRuleMakeupGain = new() { Minimum = -144, Maximum = 12, DecimalPlaces = 1, Increment = 0.5M, Width = 160, AccessibleName = "App 路由預設補償增益分貝" };
    private readonly CheckBox _routeRuleEnabled = new() { Text = "啟用預設", AutoSize = true, Checked = true, AccessibleName = "啟用 App 路由預設" };
    private readonly ComboBox _routeRuleGainOwner = new() { Width = 220, DropDownStyle = ComboBoxStyle.DropDownList, AccessibleName = "App 路由預設增益控制者" };
    private readonly Button _applyRouteRule = new() { Text = "新增／更新預設", AutoSize = true, AccessibleName = "新增或更新 App 路由預設" };
    private readonly Button _removeRouteRule = new() { Text = "移除選取預設", AutoSize = true, AccessibleName = "移除選取的 App 路由預設" };
    private readonly Button _clearRouteRules = new() { Text = "清除全部預設", AutoSize = true, AccessibleName = "清除全部 App 路由預設" };
    private readonly Button _exportRouteRules = new() { Text = "匯出預設", AutoSize = true, AccessibleName = "匯出 App 路由預設到檔案" };
    private readonly Button _importRouteRules = new() { Text = "匯入預設", AutoSize = true, AccessibleName = "從檔案匯入 App 路由預設" };
    private readonly Label _effective = new() { AutoSize = true };
    private readonly Label _listeningDose = new() { AutoSize = true, AccessibleName = "聆聽劑量（今日）" };
    private readonly ComboBox _scenes = new() { Width = 460, DropDownStyle = ComboBoxStyle.DropDownList, AccessibleName = "選取情境設定檔" };
    private readonly TextBox _customSceneId = new() { Width = 220, PlaceholderText = "Scene ID", AccessibleName = "自訂場景 ID" };
    private readonly TextBox _customSceneName = new() { Width = 220, PlaceholderText = "名稱", AccessibleName = "自訂場景名稱" };
    private readonly TextBox _customSceneDescription = new() { Width = 460, PlaceholderText = "說明", AccessibleName = "自訂場景說明" };
    private readonly CheckBox _customSceneLoudnessLiveUpdate = new() { Text = "音量連動等響度", AutoSize = true, AccessibleName = "音量連動等響度" };
    private readonly Label _customSceneQueueStatus = new() { AutoSize = true, AccessibleName = "離線場景同步佇列狀態" };
    private readonly Button _addCustomScene = new() { Text = "加入自訂場景", AutoSize = true, AccessibleName = "加入自訂場景" };
    private readonly Button _removeCustomScene = new() { Text = "移除選取的自訂場景", AutoSize = true, AccessibleName = "移除選取的自訂場景" };
    private readonly Button _exportCustomScenes = new() { Text = "匯出場景", AutoSize = true, AccessibleName = "匯出自訂場景到檔案" };
    private readonly Button _importCustomScenes = new() { Text = "匯入場景", AutoSize = true, AccessibleName = "從檔案匯入自訂場景" };
    private readonly ComboBox _irModes = new() { Width = 460, DropDownStyle = ComboBoxStyle.DropDownList, AccessibleName = "選取 IR 模式" };
    private readonly TrackBar _irStrength = new() { Minimum = 0, Maximum = 100, TickFrequency = 10, Width = 460, AccessibleName = "IR 強度百分比" };
    private readonly Label _irStrengthReadout = new() { AutoSize = true, Width = 80, TextAlign = ContentAlignment.MiddleLeft, AccessibleName = "IR 強度百分比數值" };
    private readonly Label _irStatus = new() { AutoSize = false, Width = 550, Height = 58 };
    private readonly Label _eqStatus = new() { AutoSize = false, Width = 550, Height = 32, AccessibleName = "等化器視覺狀態" };
    private readonly Label _safetyStatus = new() { AutoSize = true, AccessibleName = "安全上限狀態" };
    private readonly Label _deviceSwitchStatus = new() { AutoSize = true, AccessibleName = "裝置切換狀態" };
    private readonly Label _lastSendDiagnostics = new() { AutoSize = false, Width = 550, Height = 32, AccessibleName = "最近命令診斷" };
    private readonly Button _loadIr = new() { Text = "載入 IR WAV 並準備", AutoSize = true, AccessibleName = "載入 IR WAV 並準備" };
    private readonly TrackBar _volume = new() { Minimum = -60, Maximum = 0, TickFrequency = 5, Width = 460, AccessibleName = "主音量分貝" };
    private readonly Label _volumeReadout = new() { AutoSize = true, Width = 80, TextAlign = ContentAlignment.MiddleLeft, AccessibleName = "主音量分貝數值" };
    private readonly CheckBox _muted = new() { Text = "靜音", AutoSize = true, AccessibleName = "主輸出靜音" };
    private readonly Button _enhance = new() { Text = "一鍵改善", AutoSize = true, Margin = new Padding(3, 12, 3, 3), AccessibleName = "一鍵改善" };
    private readonly Button _connect = new() { Text = "連接引擎", AutoSize = true, Margin = new Padding(3, 12, 3, 3), AccessibleName = "連接或重新連接 Hibiki 音訊引擎" };
    private readonly System.Windows.Forms.Timer _statusTimer = new() { Interval = 1000 };
    private bool _updatingScene;
    private bool _updatingPhysicalDevices;
    private bool _updatingSession;
    private bool _updatingRouteRules;
    private bool _statusRefreshActive;
    private bool _restoredPersistedState;
    private bool _windowBoundsRestored;
    private ComboBox _outputGroups = null!;
    internal PreviewForm(EasyControlViewModel viewModel)
    {
        _viewModel = viewModel;
        Text = "Hibiki DSP — Compatibility Preview";
        StartPosition = FormStartPosition.CenterScreen;
        AutoScaleMode = AutoScaleMode.Dpi;
        ClientSize = new Size(1040, 700);
        MinimumSize = new Size(900, 620);
        BackColor = ShellBackground;
        ForeColor = TextPrimary;
        Font = new Font("Segoe UI", 10);

        var panel = new FlowLayoutPanel
        {
            Dock = DockStyle.Fill,
            Padding = new Padding(32, 28, 32, 28),
            FlowDirection = FlowDirection.TopDown,
            WrapContents = false,
            AutoScroll = true,
            BackColor = ShellBackground,
        };
        var header = CreateHeaderCard();
        panel.Controls.Add(header);
        panel.Controls.Add(CreateSectionHeader("引擎連線"));
        panel.Controls.Add(_connect);
        panel.Controls.Add(_connection);
        panel.Controls.Add(CreateSectionHeader("輸出與裝置"));
        var groups = new ComboBox { Width = 460, DropDownStyle = ComboBoxStyle.DropDownList, AccessibleName = "選取輸出群組", DataSource = _viewModel.OutputGroups.ToList(), DisplayMember = "Name", ValueMember = "Id" };
        _outputGroups = groups;
        groups.SelectedIndexChanged += (_, _) => { if (groups.SelectedValue is string id) _viewModel.SelectedOutputGroup = id; };
        panel.Controls.Add(groups);
        panel.Controls.Add(_devices);
        _physicalDeviceSelector.SelectedIndexChanged += (_, _) =>
        {
            if (_updatingPhysicalDevices || _physicalDeviceSelector.SelectedValue is not string endpointId) return;
            _viewModel.SelectedPhysicalDeviceId = endpointId;
            PersistUiState();
        };
        _switchDevice.Click += async (_, _) =>
        {
            if (_physicalDeviceSelector.SelectedValue is not string switchId) return;
            _switchDevice.Enabled = false;
            try
            {
                await _viewModel.SwitchPhysicalDeviceAsync(switchId);
            }
            finally
            {
                _switchDevice.Enabled = true;
            }
            RefreshView();
        };
        _refreshDevices.Click += async (_, _) =>
        {
            await _viewModel.RefreshPhysicalDevicePickerAsync();
            RefreshView();
        };
        panel.Controls.Add(CreateSectionHeader("實體輸出裝置"));
        panel.Controls.Add(_physicalDeviceSelector);
        panel.Controls.Add(_physicalDeviceEmptyState);
        var physicalDeviceActions = new FlowLayoutPanel
        {
            AutoSize = true,
            FlowDirection = FlowDirection.LeftToRight,
            WrapContents = false
        };
        physicalDeviceActions.Controls.Add(_switchDevice);
        physicalDeviceActions.Controls.Add(_refreshDevices);
        panel.Controls.Add(physicalDeviceActions);
        _connect.Click += async (_, _) =>
        {
            _connect.Enabled = false;
            try
            {
                if (_viewModel.IsConnected) await _viewModel.DisconnectAsync();
                await _viewModel.ConnectAsync(TimeSpan.FromSeconds(3));
            }
            finally
            {
                _connect.Enabled = true;
            }
            RefreshView();
        };
        panel.Controls.Add(CreateSectionHeader("場景"));
        SyncSceneList();
        _scenes.SelectedIndexChanged += async (_, _) =>
        {
            if (_updatingScene || _scenes.SelectedValue is not string id || !_viewModel.IsConnected)
                return;
            await _viewModel.SelectSceneAsync(id);
            PersistUiState();
            RefreshView();
        };
        panel.Controls.Add(_scenes);
        panel.Controls.Add(CreateSectionHeader("自訂場景（最多 32 筆）"));
        _customSceneId.TextChanged += (_, _) => _viewModel.CustomSceneId = _customSceneId.Text;
        _customSceneName.TextChanged += (_, _) => _viewModel.CustomSceneName = _customSceneName.Text;
        _customSceneDescription.TextChanged += (_, _) => _viewModel.CustomSceneDescription = _customSceneDescription.Text;
        _customSceneLoudnessLiveUpdate.CheckedChanged += (_, _) => _viewModel.CustomSceneLoudnessLiveUpdate = _customSceneLoudnessLiveUpdate.Checked;
        var customSceneIdentity = new FlowLayoutPanel
        {
            AutoSize = true,
            FlowDirection = FlowDirection.LeftToRight,
            WrapContents = false
        };
        customSceneIdentity.Controls.Add(_customSceneId);
        customSceneIdentity.Controls.Add(_customSceneName);
        panel.Controls.Add(CreateFieldLabel("場景識別"));
        panel.Controls.Add(customSceneIdentity);
        panel.Controls.Add(CreateFieldLabel("場景說明"));
        panel.Controls.Add(_customSceneDescription);
        panel.Controls.Add(CreateFieldLabel("同步選項"));
        panel.Controls.Add(_customSceneLoudnessLiveUpdate);
        panel.Controls.Add(_customSceneQueueStatus);
        _addCustomScene.Click += async (_, _) =>
        {
            await _viewModel.AddCustomSceneAsync();
            SyncSceneList();
            RefreshView();
        };
        _removeCustomScene.Click += async (_, _) =>
        {
            if (_scenes.SelectedValue is string sceneId)
                await _viewModel.RemoveCustomSceneAsync(sceneId);
            SyncSceneList();
            RefreshView();
        };
        _exportCustomScenes.Click += (_, _) => ExportCustomScenes();
        _importCustomScenes.Click += (_, _) => ImportCustomScenes();
        var customSceneActions = new FlowLayoutPanel
        {
            AutoSize = true,
            FlowDirection = FlowDirection.LeftToRight,
            WrapContents = false
        };
        customSceneActions.Controls.Add(_addCustomScene);
        customSceneActions.Controls.Add(_removeCustomScene);
        customSceneActions.Controls.Add(_exportCustomScenes);
        customSceneActions.Controls.Add(_importCustomScenes);
        panel.Controls.Add(customSceneActions);
        panel.Controls.Add(CreateSectionHeader("IR 檔案與相位"));
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
            _irStrengthReadout.Text = $"{_irStrength.Value} %";
            RefreshView();
        };
        panel.Controls.Add(CreateFieldLabel("相位模式"));
        panel.Controls.Add(_irModes);
        panel.Controls.Add(CreateFieldLabel("IR 強度"));
        panel.Controls.Add(_irStrength);
        panel.Controls.Add(_irStrengthReadout);

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
        panel.Controls.Add(CreateSectionHeader("音量與安全"));
        _volume.Value = (int)_viewModel.RequestedVolumeDb;
        _volume.ValueChanged += async (_, _) => { _volumeReadout.Text = $"{_volume.Value} dB"; _viewModel.RequestedVolumeDb = _volume.Value; if (_viewModel.IsConnected) await _viewModel.QueueVolumeAsync(); RefreshView(); };
        _muted.CheckedChanged += async (_, _) =>
        {
            _viewModel.Muted = _muted.Checked;
            if (_viewModel.IsConnected) await _viewModel.QueueVolumeAsync();
            RefreshView();
        };
        panel.Controls.Add(CreateFieldLabel("主音量"));
        panel.Controls.Add(_volume);
        panel.Controls.Add(_volumeReadout);
        panel.Controls.Add(_muted);
        panel.Controls.Add(_effective);
        panel.Controls.Add(_listeningDose);
        panel.Controls.Add(_safetyStatus);
        panel.Controls.Add(_deviceSwitchStatus);
        panel.Controls.Add(_lastSendDiagnostics);
        panel.Controls.Add(_routes);
        panel.Controls.Add(CreateSectionHeader("Expert App／工作階段（需以 -EnableSessionRouting 啟動）"));
        panel.Controls.Add(_sessions);
        _sessionFilter.TextChanged += (_, _) => SyncSessionList();
        panel.Controls.Add(CreateFieldLabel("篩選工作階段"));
        panel.Controls.Add(_sessionFilter);
        _sessionSelector.SelectedIndexChanged += (_, _) =>
        {
            if (_updatingSession || _sessionSelector.SelectedItem is not SessionCatalogEntryV1 entry) return;
            _viewModel.SelectSession(entry.Handle);
            SyncSessionControls();
            RefreshView();
        };
        panel.Controls.Add(CreateFieldLabel("工作階段"));
        panel.Controls.Add(_sessionSelector);
        _sessionVolume.ValueChanged += async (_, _) =>
        {
            if (_updatingSession || _viewModel.SelectedSession is null) return;
            _viewModel.SessionVolumeDb = _sessionVolume.Value;
            _sessionVolumeReadout.Text = $"{_sessionVolume.Value} dB";
            RefreshView();
        };
        panel.Controls.Add(CreateFieldLabel("App 音量"));
        panel.Controls.Add(_sessionVolume);
        panel.Controls.Add(_sessionVolumeReadout);
        _applySessionVolume.Click += async (_, _) =>
        {
            await _viewModel.ApplySelectedSessionVolumeAsync();
            RefreshView();
        };
        panel.Controls.Add(_applySessionVolume);
        _sessionMuted.CheckedChanged += (_, _) => { if (!_updatingSession) _viewModel.SessionMuted = _sessionMuted.Checked; };
        panel.Controls.Add(_sessionMuted);
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
        panel.Controls.Add(CreateFieldLabel("App 路由"));
        panel.Controls.Add(sessionRouteFields);
        _applySessionRoute.Click += async (_, _) =>
        {
            await _viewModel.ApplySelectedSessionRouteAsync();
            RefreshView();
        };
        panel.Controls.Add(_applySessionRoute);
        panel.Controls.Add(CreateSectionHeader("App 路由預設（Expert）"));
        panel.Controls.Add(new Label
        {
            Text = "建立後會保存到本機；只有 App 清單已同步且引擎回覆 Ack，才會顯示為已套用。App ID 或顯示名稱至少填一項。",
            AutoSize = false,
            Width = 550,
            Height = 42
        });
        SyncRouteRuleList();
        panel.Controls.Add(CreateFieldLabel("已建立的 App 路由預設"));
        panel.Controls.Add(_routeRuleList);
        panel.Controls.Add(_routeRuleEmptyState);
        _routeRuleId.TextChanged += (_, _) => _viewModel.RouteRuleId = _routeRuleId.Text;
        _routeRuleAppId.TextChanged += (_, _) => _viewModel.RouteRuleAppId = _routeRuleAppId.Text;
        _routeRuleDisplayName.TextChanged += (_, _) => _viewModel.RouteRuleDisplayName = _routeRuleDisplayName.Text;
        _routeRuleLaneId.TextChanged += (_, _) => _viewModel.RouteRuleLaneId = _routeRuleLaneId.Text;
        _routeRuleOutputGroup.TextChanged += (_, _) => _viewModel.RouteRuleOutputGroup = _routeRuleOutputGroup.Text;
        _routeRulePriority.ValueChanged += (_, _) => _viewModel.RouteRulePriority = (int)_routeRulePriority.Value;
        _routeRuleMakeupGain.ValueChanged += (_, _) => _viewModel.RouteRuleMakeupGainDb = (double)_routeRuleMakeupGain.Value;
        _routeRuleEnabled.CheckedChanged += (_, _) => _viewModel.RouteRuleEnabled = _routeRuleEnabled.Checked;
        _routeRuleGainOwner.DataSource = _viewModel.RouteRuleGainOwners.ToList();
        _routeRuleGainOwner.SelectedIndexChanged += (_, _) =>
        {
            if (_updatingRouteRules || _routeRuleGainOwner.SelectedItem is not SessionRouteRuleGainOwnerV1 owner) return;
            _viewModel.RouteRuleGainOwner = owner;
        };
        _updatingRouteRules = true;
        try
        {
            _routeRuleGainOwner.SelectedItem = _viewModel.RouteRuleGainOwner;
        }
        finally
        {
            _updatingRouteRules = false;
        }
        var routeRuleIdentityFields = new FlowLayoutPanel
        {
            AutoSize = true,
            FlowDirection = FlowDirection.LeftToRight,
            WrapContents = false
        };
        routeRuleIdentityFields.Controls.Add(_routeRuleId);
        routeRuleIdentityFields.Controls.Add(_routeRuleAppId);
        panel.Controls.Add(CreateFieldLabel("識別與 App"));
        panel.Controls.Add(routeRuleIdentityFields);
        var routeRuleMatcherFields = new FlowLayoutPanel
        {
            AutoSize = true,
            FlowDirection = FlowDirection.LeftToRight,
            WrapContents = false
        };
        routeRuleMatcherFields.Controls.Add(_routeRuleDisplayName);
        routeRuleMatcherFields.Controls.Add(_routeRuleLaneId);
        panel.Controls.Add(CreateFieldLabel("顯示與 Lane"));
        panel.Controls.Add(routeRuleMatcherFields);
        panel.Controls.Add(CreateFieldLabel("輸出群組"));
        panel.Controls.Add(_routeRuleOutputGroup);
        var routeRuleNumericFields = new FlowLayoutPanel
        {
            AutoSize = true,
            FlowDirection = FlowDirection.LeftToRight,
            WrapContents = false
        };
        routeRuleNumericFields.Controls.Add(_routeRulePriority);
        routeRuleNumericFields.Controls.Add(_routeRuleMakeupGain);
        panel.Controls.Add(CreateFieldLabel("優先級／補償增益（dB）"));
        panel.Controls.Add(routeRuleNumericFields);
        var routeRuleOptionFields = new FlowLayoutPanel
        {
            AutoSize = true,
            FlowDirection = FlowDirection.LeftToRight,
            WrapContents = false
        };
        routeRuleOptionFields.Controls.Add(_routeRuleEnabled);
        routeRuleOptionFields.Controls.Add(_routeRuleGainOwner);
        panel.Controls.Add(CreateFieldLabel("預設選項"));
        panel.Controls.Add(routeRuleOptionFields);
        _applyRouteRule.Click += async (_, _) =>
        {
            await _viewModel.ApplyRouteRuleAsync();
            SyncRouteRuleList();
            RefreshView();
        };
        _removeRouteRule.Click += async (_, _) =>
        {
            if (_routeRuleList.SelectedValue is string ruleId)
            {
                await _viewModel.ApplyRemoveRouteRuleAsync(ruleId);
                SyncRouteRuleList();
            }
            RefreshView();
        };
        _clearRouteRules.Click += async (_, _) =>
        {
            await _viewModel.ApplyClearRouteRulesAsync();
            SyncRouteRuleList();
            RefreshView();
        };
        _exportRouteRules.Click += (_, _) => ExportRouteRules();
        _importRouteRules.Click += (_, _) => ImportRouteRules();
        var routeRuleActions = new FlowLayoutPanel
        {
            AutoSize = true,
            FlowDirection = FlowDirection.LeftToRight,
            WrapContents = false
        };
        routeRuleActions.Controls.Add(_applyRouteRule);
        routeRuleActions.Controls.Add(_removeRouteRule);
        routeRuleActions.Controls.Add(_clearRouteRules);
        routeRuleActions.Controls.Add(_exportRouteRules);
        routeRuleActions.Controls.Add(_importRouteRules);
        panel.Controls.Add(routeRuleActions);
        _routeRuleList.SelectedIndexChanged += (_, _) =>
        {
            if (_updatingRouteRules) return;
            RefreshView();
        };
        panel.Controls.Add(CreateSectionHeader("等化器"));
        panel.Controls.Add(_eqStatus);
        panel.Controls.Add(_status);
        Controls.Add(panel);
        ApplyModernStyles(panel);
        panel.Resize += (_, _) => ResizeModernSurface(panel, header);
        ResizeModernSurface(panel, header);
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
            RestoreWindowBounds();
            RestorePersistedSelections();
            RefreshView();
        };
        ResizeEnd += (_, _) => PersistUiState();
        FormClosing += (_, _) => PersistUiState();
        RefreshView();
    }

    private static Panel CreateHeaderCard()
    {
        var card = new Panel
        {
            Height = 122,
            BackColor = CardBackground,
            Margin = new Padding(0, 0, 0, 8),
            Tag = "header-card",
        };
        var title = new Label
        {
            Text = "Hibiki DSP",
            AutoSize = true,
            Font = new Font("Segoe UI", 24, FontStyle.Bold),
            ForeColor = TextPrimary,
            Location = new Point(24, 16),
            Tag = "header-title",
        };
        var subtitle = new Label
        {
            Name = "header-subtitle",
            Text = "本機 Compatibility Preview：自帶 .NET runtime，不需要 Windows App Runtime；不含 driver、系統攔截或正式音訊處理。",
            AutoSize = false,
            Height = 36,
            ForeColor = TextSecondary,
            Location = new Point(24, 55),
            Tag = "header-subtitle",
        };
        var mode = new Label
        {
            Text = "離線預覽模式：先啟動 user-space Engine Preview，才能測試場景或音量命令。",
            AutoSize = true,
            ForeColor = TextSecondary,
            Location = new Point(24, 96),
            Tag = "header-mode",
        };
        card.Controls.Add(mode);
        card.Controls.Add(subtitle);
        card.Controls.Add(title);
        card.Paint += (_, e) =>
        {
            using var borderPen = new Pen(Border);
            using var accentBrush = new SolidBrush(Accent);
            e.Graphics.DrawRectangle(borderPen, 0, 0, Math.Max(0, card.Width - 1), Math.Max(0, card.Height - 1));
            e.Graphics.FillRectangle(accentBrush, 0, 0, 5, card.Height);
        };
        return card;
    }

    private static Label CreateSectionHeader(string text) => new()
    {
        Text = text,
        AutoSize = true,
        Font = new Font("Segoe UI", 12, FontStyle.Bold),
        ForeColor = Accent,
        Margin = new Padding(0, 16, 0, 4),
        Tag = "section-header",
    };

    private static Label CreateFieldLabel(string text) => new()
    {
        Text = text,
        AutoSize = true,
        Font = new Font("Segoe UI", 9, FontStyle.Bold),
        ForeColor = TextSecondary,
        Margin = new Padding(0, 8, 8, 0),
        Tag = "field-label",
    };

    private void ApplyModernStyles(Control root)
    {
        foreach (Control control in root.Controls)
        {
            if (control is Button button)
            {
                StyleModernButton(button);
            }
            else if (control is TextBox textBox)
            {
                textBox.BorderStyle = BorderStyle.FixedSingle;
                textBox.AutoSize = false;
                textBox.Height = Math.Max(36, textBox.Height);
                textBox.Margin = new Padding(0, 4, 8, 4);
                StyleModernInput(textBox, CardBackground);
            }
            else if (control is ComboBox comboBox)
            {
                comboBox.FlatStyle = FlatStyle.Standard;
                comboBox.Height = Math.Max(36, comboBox.Height);
                comboBox.Margin = new Padding(0, 4, 8, 4);
                StyleModernInput(comboBox, CardBackground);
            }
            else if (control is NumericUpDown numericUpDown)
            {
                numericUpDown.BorderStyle = BorderStyle.FixedSingle;
                numericUpDown.Height = Math.Max(36, numericUpDown.Height);
                numericUpDown.Margin = new Padding(0, 4, 8, 4);
                StyleModernInput(numericUpDown, CardBackground);
            }
            else if (control is ListBox listBox)
            {
                listBox.BorderStyle = BorderStyle.FixedSingle;
                listBox.Margin = new Padding(0, 4, 8, 4);
                StyleModernInput(listBox, CardBackground);
            }
            else if (control is CheckBox checkBox)
            {
                checkBox.Margin = new Padding(0, 7, 12, 7);
                checkBox.EnabledChanged += (_, _) => UpdateModernInputColors(checkBox, CardBackground);
                UpdateModernInputColors(checkBox, CardBackground);
            }
            else if (control is TrackBar trackBar)
            {
                trackBar.Margin = new Padding(0, 6, 8, 6);
                StyleModernInput(trackBar, GroupBackground);
            }
            else if (control is Label label && label.Tag is not string)
            {
                label.ForeColor = TextSecondary;
                label.Margin = new Padding(0, 4, 8, 4);
            }

            if (control is FlowLayoutPanel flow && !ReferenceEquals(flow, root))
            {
                flow.BackColor = GroupBackground;
                flow.Padding = new Padding(8);
                flow.Margin = new Padding(0, 4, 0, 4);
            }

            if (control.HasChildren)
                ApplyModernStyles(control);
        }

        foreach (var label in new[]
                 {
                     _connection, _devices, _physicalDeviceEmptyState, _status, _routes, _sessions, _irStatus,
                     _eqStatus, _lastSendDiagnostics, _customSceneQueueStatus, _routeRuleEmptyState,
                 })
        {
            label.BackColor = GroupBackground;
            label.ForeColor = TextSecondary;
            label.Padding = new Padding(10, 7, 10, 7);
            label.Margin = new Padding(0, 4, 8, 4);
        }
    }

    private static void StyleModernInput(Control control, Color enabledBackground)
    {
        control.EnabledChanged += (_, _) => UpdateModernInputColors(control, enabledBackground);
        UpdateModernInputColors(control, enabledBackground);
    }

    private static void UpdateModernInputColors(Control control, Color enabledBackground)
    {
        control.BackColor = control.Enabled ? enabledBackground : DisabledBackground;
        control.ForeColor = control.Enabled ? TextPrimary : DisabledText;
    }

    private void StyleModernButton(Button button)
    {
        var primary = IsPrimaryButton(button);
        button.AutoSize = true;
        button.FlatStyle = FlatStyle.Flat;
        button.FlatAppearance.BorderSize = 0;
        button.FlatAppearance.MouseOverBackColor = primary ? AccentHover : Border;
        button.FlatAppearance.MouseDownBackColor = primary ? AccentHover : Border;
        button.UseVisualStyleBackColor = false;
        button.Padding = new Padding(14, 8, 14, 8);
        button.Margin = new Padding(0, 4, 8, 4);
        button.Cursor = Cursors.Hand;
        button.EnabledChanged += (_, _) => UpdateModernButtonColors(button, primary);
        UpdateModernButtonColors(button, primary);
    }

    private bool IsPrimaryButton(Button button) =>
        ReferenceEquals(button, _connect) ||
        ReferenceEquals(button, _enhance) ||
        ReferenceEquals(button, _addCustomScene) ||
        ReferenceEquals(button, _loadIr) ||
        ReferenceEquals(button, _applySessionVolume) ||
        ReferenceEquals(button, _applySessionRoute) ||
        ReferenceEquals(button, _applyRouteRule);

    private static void UpdateModernButtonColors(Button button, bool primary)
    {
        if (!button.Enabled)
        {
            button.BackColor = DisabledBackground;
            button.ForeColor = DisabledText;
            return;
        }

        button.BackColor = primary ? Accent : CardBackground;
        button.ForeColor = primary ? Color.White : TextPrimary;
    }

    private void ResizeModernSurface(FlowLayoutPanel panel, Panel header)
    {
        var contentWidth = Math.Max(620, panel.ClientSize.Width - panel.Padding.Horizontal - SystemInformation.VerticalScrollBarWidth);
        header.Width = contentWidth;
        if (header.Controls["header-subtitle"] is Label subtitle)
            subtitle.Width = Math.Max(420, contentWidth - 48);

        _outputGroups.Width = contentWidth;
        _physicalDeviceSelector.Width = contentWidth;
        _scenes.Width = contentWidth;
        _customSceneDescription.Width = contentWidth;
        _irModes.Width = contentWidth;
        _irStrength.Width = Math.Max(360, contentWidth - 72);
        _volume.Width = Math.Max(360, contentWidth - 72);
        _sessionFilter.Width = contentWidth;
        _sessionSelector.Width = contentWidth;
        _sessionVolume.Width = Math.Max(360, contentWidth - 72);
        _routeRuleList.Width = contentWidth;
        _routeRuleOutputGroup.Width = contentWidth;

        foreach (var label in new[]
                 {
                     _connection, _devices, _physicalDeviceEmptyState, _status, _routes, _sessions, _irStatus,
                     _eqStatus, _lastSendDiagnostics, _routeRuleEmptyState,
                 })
        {
            label.Width = contentWidth;
        }

        var halfWidth = Math.Max(260, (contentWidth - 26) / 2);
        foreach (var fields in panel.Controls.OfType<FlowLayoutPanel>())
        {
            fields.AutoSize = false;
            fields.Width = contentWidth;
            var children = fields.Controls.Cast<Control>().ToArray();
            if (children.Length == 2 && children.All(control => control is TextBox or NumericUpDown))
            {
                foreach (var child in children)
                    child.Width = halfWidth;
            }

            var rowHeight = children.Length == 0
                ? 0
                : children.Max(control => control.PreferredSize.Height + control.Margin.Vertical);
            fields.Height = Math.Max(fields.Height, rowHeight + fields.Padding.Vertical + 4);
        }
    }

    private void RestoreWindowBounds()
    {
        if (_windowBoundsRestored || IsDisposed) return;
        _windowBoundsRestored = true;
        var state = PreviewUiState.Load();
        if (state.WindowX is not int x || state.WindowY is not int y ||
            state.WindowWidth is not int width || state.WindowHeight is not int height) return;
        if (width < MinimumSize.Width || height < MinimumSize.Height) return;
        if (!IsVisibleOnAnyScreen(new Rectangle(x, y, width, height))) return;
        StartPosition = FormStartPosition.Manual;
        Bounds = new Rectangle(x, y, width, height);
    }

    private static bool IsVisibleOnAnyScreen(Rectangle bounds)
    {
        foreach (var screen in Screen.AllScreens)
        {
            if (screen.WorkingArea.IntersectsWith(bounds)) return true;
        }
        return false;
    }

    private void OnViewModelChanged(object? sender, PropertyChangedEventArgs e)
    {
        if (IsDisposed || !IsHandleCreated) return;
        if (e.PropertyName == nameof(EasyControlViewModel.RouteRules))
        {
            BeginInvoke(SyncRouteRuleList);
            return;
        }
        BeginInvoke(RefreshView);
    }

    private void RestorePersistedSelections()
    {
        if (_restoredPersistedState || IsDisposed) return;
        _restoredPersistedState = true;
        var state = PreviewUiState.Load();
        if (state.SelectedPhysicalDeviceEndpointId is string endpointId)
        {
            var devices = _viewModel.PhysicalDevices
                .Where(device => device.Flow == PhysicalDeviceFlowV1.Render && device.IsSelectable)
                .ToArray();
            if (Array.Exists(devices, device => device.EndpointId == endpointId))
                _viewModel.SelectedPhysicalDeviceId = endpointId;
        }
        if (_viewModel.IsConnected && state.SelectedSceneId is string sceneId &&
            _viewModel.Scenes.Any(scene => scene.Id == sceneId))
        {
            _ = ApplyRestoredSceneAsync(sceneId);
        }
        if (state.SelectedOutputGroupId is string groupId &&
            _viewModel.OutputGroups.Any(group => group.Id == groupId))
        {
            _viewModel.SelectedOutputGroup = groupId;
            _outputGroups.SelectedValue = groupId;
        }
    }

    private async Task ApplyRestoredSceneAsync(string sceneId)
    {
        try
        {
            await _viewModel.SelectSceneAsync(sceneId);
            RefreshView();
        }
        catch (Exception)
        {
            // Restoring a persisted scene is best-effort and never blocks use.
        }
    }

    private void PersistUiState() =>
        PreviewUiState.Save(_viewModel.SelectedPhysicalDeviceId,
            _scenes.SelectedValue as string ?? _viewModel.SelectedScene?.Id,
            _viewModel.SelectedOutputGroup,
            WindowState == FormWindowState.Normal ? Bounds : null);

    private void RefreshView()
    {
        _connection.Text = _viewModel.ConnectionStatusText;
        _connection.BackColor = _viewModel.IsConnected ? SuccessBackground : WarningBackground;
        _connection.ForeColor = _viewModel.IsConnected ? SuccessText : WarningText;
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
              $"預設輸出：{defaultRender ?? "未指定"}\r\n切換會先預熱新裝置再以 30 ms 交叉淡化；失敗自動回復上一個裝置。";
        SyncPhysicalDeviceList();
        var selectableRenderCount = renderDevices.Count(device => device.IsSelectable);
        _physicalDeviceEmptyState.Visible = selectableRenderCount == 0;
        _physicalDeviceEmptyState.Text = _viewModel.IsConnected
            ? "沒有可用的已驗證實體輸出裝置。"
            : "尚未連接引擎；連接後才會顯示已驗證的實體輸出裝置。";
        _physicalDeviceEmptyState.BackColor = _viewModel.IsConnected ? WarningBackground : GroupBackground;
        _physicalDeviceEmptyState.ForeColor = _viewModel.IsConnected ? WarningText : TextSecondary;
        _physicalDeviceSelector.Enabled = _viewModel.IsConnected && selectableRenderCount > 0;
        _switchDevice.Enabled = _viewModel.IsConnected && !_viewModel.IsBusy &&
                                _physicalDeviceSelector.SelectedValue is string;
        _refreshDevices.Enabled = _viewModel.IsConnected && !_viewModel.IsBusy;
        _enhance.Enabled = _viewModel.IsConnected;
        _connect.Text = _viewModel.IsConnected ? "重新連接" : "連接引擎";
        _connect.Enabled = !_viewModel.IsBusy;
        _scenes.Enabled = _viewModel.IsConnected;
        _volume.Enabled = _viewModel.IsConnected;
        var sessionCount = _viewModel.SessionCatalog.Count;
        var seqText = _viewModel.SessionCatalogSequenceDisplayText;
        _sessions.Text = sessionCount == 0
            ? $"App catalog：尚未同步（{seqText}）；請以 -EnableSessionRouting 啟動引擎，或目前沒有可控制的工作階段。"
            : $"App catalog：{sessionCount} 筆；{seqText} 只顯示 bounded metadata。套用 App 音量會寫入 Windows session，" +
              "實體 per-App 送出已由 process-loopback E2E 覆蓋；仍屬使用者空間控制證據。";
        SyncSessionList();
        var hasSession = _viewModel.HasSelectedSession;
        _sessionSelector.Enabled = _viewModel.IsConnected && _sessionSelector.Items.Count > 0;
        _sessionVolume.Enabled = _viewModel.IsConnected && hasSession && _viewModel.SelectedSession?.VolumeAvailable == true;
        _applySessionVolume.Enabled = _sessionVolume.Enabled;
        _sessionMuted.Enabled = _sessionVolume.Enabled;
        _sessionLane.Enabled = _viewModel.IsConnected && hasSession;
        _sessionOutput.Enabled = _viewModel.IsConnected && hasSession;
        _applySessionRoute.Enabled = _viewModel.IsConnected && hasSession;
        _removeRouteRule.Enabled = _viewModel.RouteRules.Count > 0 && _routeRuleList.SelectedValue is string;
        _clearRouteRules.Enabled = _viewModel.RouteRules.Count > 0;
        _loadIr.Enabled = _viewModel.IsConnected && _viewModel.IrPhaseMode != IrPhaseMode.Bypass;
        _irStrength.Enabled = _viewModel.IrPhaseMode is IrPhaseMode.MixedPhase or IrPhaseMode.LinearPhase;
        _muted.Enabled = _viewModel.IsConnected;
        _effective.Text = $"實際有效音量：{_viewModel.EffectiveVolumeDb:0.0} dB；{_viewModel.VolumeOriginText}；{_viewModel.VolumeActuatorText}";
        var pauseHint = _viewModel.ListeningDose.PauseHintText;
        _listeningDose.Text = $"{_viewModel.ListeningDose.StateText}｜剩餘安全時間：{_viewModel.ListeningDose.RemainingSafeTimeText}" + (string.IsNullOrEmpty(pauseHint) ? "" : $"｜{pauseHint}");
        _safetyStatus.Text = _viewModel.SafetyStatusText;
        _deviceSwitchStatus.Text = _viewModel.DeviceSwitchStatusText;
        _lastSendDiagnostics.Text = string.IsNullOrEmpty(_viewModel.LastSendDiagnostics)
            ? "最近命令診斷：無異常紀錄"
            : $"最近命令診斷：{_viewModel.LastSendDiagnostics}";
        _routes.Text = _viewModel.Expert.RouteHealthAccessibleSummary;
        _status.Text = _viewModel.StatusText;
        _eqStatus.Text = _viewModel.EqSurface.StateText;
        _irStatus.Text = $"{_viewModel.IrPhaseModeText}；實測延遲 {_viewModel.IrAddedDelayMs:0.0} ms。\r\n{_viewModel.IrPrepareStatus}";
        var requested = Math.Clamp((int)Math.Round(_viewModel.RequestedVolumeDb), _volume.Minimum, _volume.Maximum);
        if (_volume.Value != requested) _volume.Value = requested;
        _volumeReadout.Text = $"{_volume.Value} dB";
        _muted.Checked = _viewModel.Muted;
        if (_irModes.SelectedValue is not IrPhaseMode currentMode || currentMode != _viewModel.IrPhaseMode)
            _irModes.SelectedValue = _viewModel.IrPhaseMode;
        var strength = Math.Clamp((int)Math.Round(_viewModel.IrPhaseStrength * 100.0),
                                  _irStrength.Minimum, _irStrength.Maximum);
        if (_irStrength.Value != strength) _irStrength.Value = strength;
        _irStrengthReadout.Text = $"{_irStrength.Value} %";
        if (!string.Equals(_customSceneId.Text, _viewModel.CustomSceneId, StringComparison.Ordinal))
            _customSceneId.Text = _viewModel.CustomSceneId;
        if (!string.Equals(_customSceneName.Text, _viewModel.CustomSceneName, StringComparison.Ordinal))
            _customSceneName.Text = _viewModel.CustomSceneName;
        if (!string.Equals(_customSceneDescription.Text, _viewModel.CustomSceneDescription, StringComparison.Ordinal))
            _customSceneDescription.Text = _viewModel.CustomSceneDescription;
        _customSceneLoudnessLiveUpdate.Checked = _viewModel.CustomSceneLoudnessLiveUpdate;
        _customSceneQueueStatus.Text = _viewModel.PendingSceneCatalogOpsCount == 0 &&
                                       _viewModel.DroppedSceneCatalogOperations == 0
            ? "離線場景同步佇列：無待同步變更"
            : $"離線場景同步佇列：{_viewModel.PendingSceneCatalogOpsCount} 筆待同步；" +
              $"已捨棄 {_viewModel.DroppedSceneCatalogOperations} 筆最舊變更（容量上限 {EasyControlViewModel.MaxPendingSceneCatalogOps}）";
        _removeCustomScene.Enabled = _scenes.SelectedValue is string removableSceneId &&
                                     _viewModel.CustomSceneCards.Any(item => item.Id == removableSceneId);
        var selectedScene = _viewModel.SelectedScene?.Id;
        if (selectedScene is not null && !string.Equals(_scenes.SelectedValue as string, selectedScene,
                                                         StringComparison.Ordinal))
        {
            _updatingScene = true;
            _scenes.SelectedValue = selectedScene;
            _updatingScene = false;
        }
    }

    private void SyncSessionList()
    {
        var selectedHandle = _viewModel.SelectedSessionHandle;
        var allSessions = _viewModel.SessionCatalog.ToArray();
        var filter = _sessionFilter.Text.Trim();
        var sessions = filter.Length == 0
            ? allSessions
            : Array.FindAll(
                allSessions,
                entry => entry.DisplayName.Contains(filter, StringComparison.OrdinalIgnoreCase) ||
                         entry.AppId.Contains(filter, StringComparison.OrdinalIgnoreCase));
        _updatingSession = true;
        try
        {
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
    }

    private void SyncSceneList()
    {
        var selectedId = _scenes.SelectedValue as string ?? _viewModel.SelectedScene?.Id;
        _updatingScene = true;
        try
        {
            _scenes.DataSource = null;
            _scenes.DataSource = _viewModel.Scenes.ToList();
            _scenes.DisplayMember = nameof(SceneCard.Name);
            _scenes.ValueMember = nameof(SceneCard.Id);
            if (selectedId is null) return;
            var scenes = _viewModel.Scenes.ToArray();
            var selectedIndex = Array.FindIndex(scenes, item => item.Id == selectedId);
            if (selectedIndex >= 0) _scenes.SelectedIndex = selectedIndex;
        }
        finally
        {
            _updatingScene = false;
        }
    }

    private void SyncPhysicalDeviceList()
    {
        var selectedId = _physicalDeviceSelector.SelectedValue as string ?? _viewModel.SelectedPhysicalDeviceId;
        var renderDevices = _viewModel.PhysicalDevices
            .Where(device => device.Flow == PhysicalDeviceFlowV1.Render && device.IsSelectable)
            .ToArray();
        _updatingPhysicalDevices = true;
        try
        {
            _physicalDeviceSelector.DataSource = null;
            _physicalDeviceSelector.DisplayMember = nameof(PhysicalDeviceCard.DisplayName);
            _physicalDeviceSelector.ValueMember = nameof(PhysicalDeviceCard.EndpointId);
            _physicalDeviceSelector.DataSource = renderDevices;
            if (selectedId is null) return;
            var selectedIndex = Array.FindIndex(renderDevices, device => device.EndpointId == selectedId);
            if (selectedIndex >= 0) _physicalDeviceSelector.SelectedIndex = selectedIndex;
        }
        finally
        {
            _updatingPhysicalDevices = false;
        }
    }

    private void SyncSessionControls()
    {
        var selected = _viewModel.SelectedSession;
        var requested = Math.Clamp((int)Math.Round(_viewModel.SessionVolumeDb),
                                   _sessionVolume.Minimum, _sessionVolume.Maximum);
        if (_sessionVolume.Value != requested) _sessionVolume.Value = requested;
        _sessionVolumeReadout.Text = $"{_sessionVolume.Value} dB";
        _sessionMuted.Checked = _viewModel.SessionMuted;
        if (_sessionLane.Text != _viewModel.SessionRouteLaneId)
            _sessionLane.Text = _viewModel.SessionRouteLaneId;
        if (_sessionOutput.Text != _viewModel.SessionRouteOutputGroup)
            _sessionOutput.Text = _viewModel.SessionRouteOutputGroup;
        if (selected is null) _sessionSelector.SelectedIndex = -1;
    }

    private void SyncRouteRuleList()
    {
        var selectedId = _routeRuleList.SelectedValue as string;
        _updatingRouteRules = true;
        try
        {
            var rules = _viewModel.RouteRules.ToArray();
            var hasRules = rules.Length > 0;
            _routeRuleList.DataSource = null;
            _routeRuleList.DisplayMember = nameof(SessionRouteRuleCard.Summary);
            _routeRuleList.ValueMember = nameof(SessionRouteRuleCard.RuleId);
            _routeRuleList.DataSource = rules;
            _routeRuleList.Visible = hasRules;
            _routeRuleEmptyState.Visible = !hasRules;
            _routeRuleEmptyState.Text = "尚未建立 App 路由預設；填寫 App ID 或顯示名稱後，按「新增／更新預設」即可建立。";
            if (selectedId is null) return;
            var selectedIndex = Array.FindIndex(rules, item => item.RuleId == selectedId);
            if (selectedIndex >= 0) _routeRuleList.SelectedIndex = selectedIndex;
        }
        finally
        {
            _updatingRouteRules = false;
        }
    }

    private void ExportCustomScenes()
    {
        using var dialog = new SaveFileDialog
        {
            Filter = "自訂場景 JSON (*.json)|*.json",
            DefaultExt = "json",
            Title = "匯出自訂場景"
        };
        if (dialog.ShowDialog(this) != DialogResult.OK) return;
        _viewModel.ExportCustomScenes(dialog.FileName);
        RefreshView();
    }

    private void ImportCustomScenes()
    {
        using var dialog = new OpenFileDialog
        {
            Filter = "自訂場景 JSON (*.json)|*.json",
            DefaultExt = "json",
            Title = "匯入自訂場景"
        };
        if (dialog.ShowDialog(this) != DialogResult.OK) return;
        if (_viewModel.ImportCustomScenes(dialog.FileName)) SyncSceneList();
        RefreshView();
    }

    private void ExportRouteRules()
    {
        using var dialog = new SaveFileDialog
        {
            Filter = "路由預設 JSON (*.json)|*.json",
            DefaultExt = "json",
            Title = "匯出 App 路由預設"
        };
        if (dialog.ShowDialog(this) != DialogResult.OK) return;
        _viewModel.ExportRouteRules(dialog.FileName);
        RefreshView();
    }

    private void ImportRouteRules()
    {
        using var dialog = new OpenFileDialog
        {
            Filter = "路由預設 JSON (*.json)|*.json",
            DefaultExt = "json",
            Title = "匯入 App 路由預設"
        };
        if (dialog.ShowDialog(this) != DialogResult.OK) return;
        if (_viewModel.ImportRouteRules(dialog.FileName)) SyncRouteRuleList();
        RefreshView();
    }

}
