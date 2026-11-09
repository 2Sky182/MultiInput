using MousemuxOverlay.NamedPipes;
using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.Runtime.CompilerServices;
using System.Text;
using System.Windows.Media;

namespace MousemuxOverlay
{


    public class MouseDevice : INotifyPropertyChanged
    {
        public ulong Device { get; set; }

        private string _deviceText = "";
        private string _positionText = "";
        private string _deltaText = "";
        private string _buttonsText = "";
        private string _wheel = "";

        private string _statusText = "";
        private Brush _colorBrush = Brushes.Gray;
        private Brush _statusBackground = Brushes.Gray;
        private Brush _statusForeground = Brushes.White;

        public bool isLinked = false;

        public string DeviceText
        {
            get => _deviceText;
            set => Set(ref _deviceText, value);
        }

        public string PositionText
        {
            get => _positionText;
            set => Set(ref _positionText, value);
        }

        public string DeltaText
        {
            get => _deltaText;
            set => Set(ref _deltaText, value);
        }

        public string ButtonsText
        {
            get => _buttonsText;
            set => Set(ref _buttonsText, value);
        }

        public string Wheel
        {
            get => _wheel;
            set => Set(ref _wheel, value);
        }

        public string StatusText
        {
            get => _statusText;
            set => Set(ref _statusText, value);
        }

        public Brush ColorBrush
        {
            get => _colorBrush;
            set => Set(ref _colorBrush, value);
        }

        public Brush StatusBackground
        {
            get => _statusBackground;
            set => Set(ref _statusBackground, value);
        }

        public Brush StatusForeground
        {
            get => _statusForeground;
            set => Set(ref _statusForeground, value);
        }

        public event PropertyChangedEventHandler? PropertyChanged;

        private void Set<T>(
            ref T field,
            T value,
            [CallerMemberName] string? name = null)
        {
            if (Equals(field, value))
                return;

            field = value;
            PropertyChanged?.Invoke(
                this,
                new PropertyChangedEventArgs(name));
        }

        public void Update(MouseData m)
        {
            DeviceText = $"0x{m.Device:X}";

            PositionText = $"{m.X}, {m.Y}";
            DeltaText = $"{m.DX}, {m.DY}";

            ButtonsText = $"L:{m.LeftDown} R:{m.RightDown} M:{m.MiddleDown}";

            Wheel = m.Wheel.ToString();

            StatusText = m.HasBlocked
                ? "BLOCKED"
                : m.IsLinked ? "LINKED" : "ACTIVE";

            StatusBackground = m.HasBlocked
                ? Brushes.DarkRed
                : m.IsLinked ? Brushes.DarkGreen : Brushes.DimGray;

            
            StatusForeground = Brushes.White;

            var color = Color.FromScRgb(
                Math.Clamp(m.A, 0f, 1f),
                Math.Clamp(m.R, 0f, 1f),
                Math.Clamp(m.G, 0f, 1f),
                Math.Clamp(m.B, 0f, 1f));

            ColorBrush = new SolidColorBrush(color);
        }
    }

}
