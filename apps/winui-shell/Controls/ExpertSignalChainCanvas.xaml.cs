// SPDX-License-Identifier: GPL-3.0-only

using Hibiki.ControlModel;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;

namespace Hibiki.WinUI.Controls;

public sealed partial class ExpertSignalChainCanvas : UserControl
{
    public ExpertSignalChainCanvas()
    {
#if !HIBIKI_COMPATIBILITY_PREVIEW
        InitializeComponent();
#endif
    }

    public static readonly DependencyProperty ModelProperty = DependencyProperty.Register(
        nameof(Model),
        typeof(ExpertSignalChainModel),
        typeof(ExpertSignalChainCanvas),
        new PropertyMetadata(ExpertSignalChainModel.Reference));

    public ExpertSignalChainModel Model
    {
        get => (ExpertSignalChainModel)GetValue(ModelProperty);
        set => SetValue(ModelProperty, value);
    }
}
