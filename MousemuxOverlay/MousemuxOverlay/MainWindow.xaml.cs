
using MousemuxOverlay;
using System;
using System.Diagnostics;
using System.IO;
using System.Runtime.InteropServices;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Interop;
using System.Windows.Media;
using System.Windows.Threading;
using System.Xml.Linq;

namespace MouseMuxOverlay
{
    public partial class MainWindow : Window
    {
        bool isExeRunning = false;
        const string backExe = "MultiInput.exe";
        private readonly MainViewModel _vm;

        public MainWindow()
        {
            InitializeComponent();
            _vm = new MainViewModel();

            DataContext = _vm;

            Loaded += MainWindow_Loaded;
            Closed += MainWindow_Closed; 
        }

        private async void MainWindow_Closed(object? sender, EventArgs e)
        {
            await _vm.StopAsync();
        }

        private void MainWindow_Loaded(object sender, RoutedEventArgs e)
        {
            try
            {
                if(!StarteExe())
                {
                    MessageBox.Show($"Error {backExe} not found");
                }
                else
                {
                    _vm.Start();
                    isExeRunning = true;
                }

            }
            catch(Exception ex)
            {
                MessageBox.Show(ex.Message);
            }
        }


        bool StarteExe()
        {
            if (!File.Exists(backExe))
            {
                return false;
            }

            var p = GetBackendProcess();
            

            if (p != null)
            {
                return true;
            }
            else
            {
                ProcessStartInfo psi = new ProcessStartInfo();
                psi.Arguments = "mySpecialCode";
                psi.CreateNoWindow = true;
                psi.FileName = backExe;

                Process.Start(psi);
                return true;
            }
            
        }

        private Process GetBackendProcess()
        {
            
            string backendPath = Path.GetFullPath(backExe);
            foreach (var process in Process.GetProcessesByName(Path.GetFileNameWithoutExtension(backExe)))
            {
                try
                {
                    string? processPath = process.MainModule?.FileName;

                    if (processPath == null)
                        continue;

                    processPath = Path.GetFullPath(processPath);

                    if (string.Equals(processPath, backendPath, StringComparison.OrdinalIgnoreCase))
                    {
                        return process;
                    }
                }
                catch
                {
                    // Prozess nicht zugänglich oder bereits beendet
                }
            }
            return null;
        }
        private void KillMulti_Click(object sender, RoutedEventArgs e)
        {
            if (isExeRunning)
            {
                try
                {
                    Process? backend = GetBackendProcess();

                    if (backend != null && !backend.HasExited)
                    {
                        backend.Kill(entireProcessTree: true);
                        backend.WaitForExit();
                        cursorCheck.IsChecked = false;
                    }
                }
                catch (Exception ex)
                {
                    MessageBox.Show($"Fehler beim Beenden: {ex.Message}");
                }
                isExeRunning = false;
            }
            else
            {
                ProcessStartInfo psi = new ProcessStartInfo();
                psi.Arguments = "mySpecialCode";
                psi.CreateNoWindow = true;
                psi.FileName = backExe;

                Process.Start(psi);
                isExeRunning = true;
                cursorCheck.IsChecked = true;
            }
        }
        private void MiceGrid_SelectionChanged(object sender, RoutedEventArgs e)
        {

        }

        private void CursorHide_Checked(object sender, EventArgs e)
        {
            try
            {
                NativeStatic.HideCursor();
            }
            catch (Exception ex)
            {
                MessageBox.Show(ex.Message);
            }
        }

        private void CursorHide_UnChecked(object sender, EventArgs e)
        {
            NativeStatic.RestoreCursor();
        }

        
    }
}
