using MousemuxOverlay.NamedPipes;
using System;
using System.Collections.Generic;
using System.Collections.ObjectModel;
using System.ComponentModel;
using System.Runtime.CompilerServices;
using System.Text;
using System.Windows;
using System.Windows.Media;

namespace MousemuxOverlay
{
    public class MainViewModel : INotifyPropertyChanged
    {
        private string miceCount = "0";
        public string MiceCount
        {
            get
            {
                return miceCount.ToString();
            }
            set
            {
                if (miceCount != value)
                {
                    miceCount = value;
                    OnPropertyChanged(nameof(miceCount));
                }
            }
        }

        private string keyCount = "0";
        public string KeyboardCount
        {
            get
            {
                return keyCount.ToString();
            }
            set
            {
                if (keyCount != value)
                {
                    keyCount = value;
                    OnPropertyChanged(nameof(keyCount));
                }
            }
        }


        private readonly MousePipeClient _pipe = new();

        private readonly CancellationTokenSource _cts = new();
        private Task? _runTask;

        

        public ObservableCollection<MouseDevice> Mice { get; }
            = new();

        public MouseDevice? SelectedMouse { get; set; }
        public MainViewModel()
        {
            _pipe.DataReceived += OnDataReceived;
        }
        public void Start()
        {
            _runTask ??= _pipe.RunAsync(_cts.Token);
        }

        private void OnDataReceived(MouseData[] mice)
        {
            // Dieser Callback kommt vom Hintergrund-Task.
            // ObservableCollection darf nicht direkt hier verändert werden!

            Application.Current.Dispatcher.BeginInvoke(() =>
            {
                UpdateMice(mice);
            });
        }

        private void UpdateMice(MouseData[] incoming)
        {
            var activeDevices = incoming
                .Select(m => m.Device)
                .ToHashSet();

            // Vorhandene Geräte aktualisieren oder neue hinzufügen.
            foreach (var data in incoming)
            {
                var existing = Mice.FirstOrDefault(x => x.Device == data.Device);

                if (existing == null)
                {
                    existing = new MouseDevice
                    {
                        Device = data.Device
                    };

                    Mice.Add(existing);
                    MiceCount = Mice.Count.ToString();
                    int i = 0;
                    foreach (var m in incoming)
                    {
                        if (m.IsLinked)
                        {
                            i++;
                        }
                    }
                    KeyboardCount = i.ToString();
                }

                existing.Update(data);
            }

            // Nicht mehr vorhandene Geräte entfernen.
            for (int i = Mice.Count - 1; i >= 0; i--)
            {
                if (!activeDevices.Contains(Mice[i].Device))
                {
                    Mice.RemoveAt(i);
                }
            }
        }

        public async Task StopAsync()
        {
            _pipe.DataReceived -= OnDataReceived;

            _cts.Cancel();

            if (_runTask != null)
            {
                try
                {
                    await _runTask;
                }
                catch (OperationCanceledException)
                {
                    // Normaler Shutdown
                }
            }

            _cts.Dispose();
        }

        public event PropertyChangedEventHandler PropertyChanged;

        protected void OnPropertyChanged([CallerMemberName] string propertyName = null)
        {
            PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(propertyName));
        }
    }
}