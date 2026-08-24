// SPDX-License-Identifier: GPL-3.0-only

using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Data;

namespace Hibiki.WinUI.Converters;

public sealed class InverseBoolToVisibilityConverter : IValueConverter
{
    public object Convert(object value, Type targetType, object parameter, string language)
        => (value as bool? ?? false) ? Visibility.Collapsed : Visibility.Visible;

    public object ConvertBack(object value, Type targetType, object parameter, string language)
        => value is Visibility.Collapsed;
}
