// SPDX-License-Identifier: GPL-3.0-only

using Microsoft.UI.Xaml.Data;

namespace Hibiki.WinUI.Converters;

public sealed class InverseBoolConverter : IValueConverter
{
    public object Convert(object value, Type targetType, object parameter, string language)
        => !(value as bool? ?? false);

    public object ConvertBack(object value, Type targetType, object parameter, string language)
        => !((value as bool?) ?? false);
}
