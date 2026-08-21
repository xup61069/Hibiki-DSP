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
    private readonly Label _status = new() { AutoSize = false, Height = 48 };
    private readonly Label _effective = new() { AutoSize = true };
    private readonly TrackBar _volume = new() { Minimum = -60, Maximum = 0, TickFrequency = 5, Width = 460 };
    private readonly Button _enhance = new() { Text = "一鍵改善", AutoSize = true, Margin = new Padding(3, 12, 3, 3) };

    internal PreviewForm(EasyControlViewModel viewModel)
    {
        _viewModel = viewModel;
        Text = "Hibiki DSP — Compatibility Preview";
        ClientSize = new Size(620, 430);
        StartPosition = FormStartPosition.CenterScreen;
        Font = new Font("Segoe UI", 10);

        var panel = new FlowLayoutPanel { Dock = DockStyle.Fill, Padding = new Padding(24), FlowDirection = FlowDirection.TopDown, WrapContents = false, AutoScroll = true };
        panel.Controls.Add(new Label { Text = "Hibiki DSP", AutoSize = true, Font = new Font("Segoe UI", 24, FontStyle.Bold) });
        panel.Controls.Add(new Label { Text = "本機 Compatibility Preview：自帶 .NET runtime，不需要 Windows App Runtime；不含 driver、系統攔截或正式音訊處理。", AutoSize = false, Width = 550, Height = 42 });
        panel.Controls.Add(new Label { Text = "離線預覽模式：先啟動真正的 Hibiki 引擎，才能套用場景或改變音量。", AutoSize = true, ForeColor = Color.DimGray });
        panel.Controls.Add(new Label { Text = "輸出群組", AutoSize = true, Margin = new Padding(3, 12, 3, 0) });
        var groups = new ComboBox { Width = 460, DropDownStyle = ComboBoxStyle.DropDownList, DataSource = _viewModel.OutputGroups.ToList(), DisplayMember = "Name", ValueMember = "Id" };
        groups.SelectedIndexChanged += (_, _) => { if (groups.SelectedValue is string id) _viewModel.SelectedOutputGroup = id; };
        panel.Controls.Add(groups);
        var connect = new Button { Text = "嘗試連接已啟動的 Hibiki 引擎", AutoSize = true, Margin = new Padding(3, 12, 3, 3) };
        connect.Click += async (_, _) => { await _viewModel.ConnectAsync(TimeSpan.FromSeconds(3)); RefreshView(); };
        panel.Controls.Add(connect);
        panel.Controls.Add(_connection);
        _enhance.Click += async (_, _) => { await _viewModel.OneTapEnhanceAsync(); RefreshView(); };
        panel.Controls.Add(_enhance);
        panel.Controls.Add(new Label { Text = "系統音量（dB）", AutoSize = true, Margin = new Padding(3, 12, 3, 0) });
        _volume.Value = (int)_viewModel.RequestedVolumeDb;
        _volume.ValueChanged += async (_, _) => { _viewModel.RequestedVolumeDb = _volume.Value; if (_viewModel.IsConnected) await _viewModel.QueueVolumeAsync(); RefreshView(); };
        panel.Controls.Add(_volume);
        panel.Controls.Add(_effective);
        panel.Controls.Add(_status);
        Controls.Add(panel);
        _viewModel.PropertyChanged += OnViewModelChanged;
        FormClosed += async (_, _) => await _viewModel.DisconnectAsync();
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
        _enhance.Enabled = _viewModel.IsConnected;
        _volume.Enabled = _viewModel.IsConnected;
        _effective.Text = $"實際有效音量：{_viewModel.EffectiveVolumeDb:0.0} dB；{_viewModel.SafetyStatusText}";
        _status.Text = _viewModel.StatusText;
        var requested = Math.Clamp((int)Math.Round(_viewModel.RequestedVolumeDb), _volume.Minimum, _volume.Maximum);
        if (_volume.Value != requested) _volume.Value = requested;
    }
}
