using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using System.Text;
using System.Windows.Input;

namespace MousemuxOverlay
{
    public static class NativeStatic
    {
        public const int OCR_NORMAL = 32512;
        public const int OCR_IBEAM = 32513;
        public const int OCR_WAIT = 32514;
        public const int OCR_CROSS = 32515;
        public const int OCR_UP = 32516;
        public const int OCR_SIZE = 32640;
        public const int OCR_ICON = 32641;
        public const int OCR_SIZENWSE = 32642;
        public const int OCR_SIZENESW = 32643;
        public const int OCR_SIZEWE = 32644;
        public const int OCR_SIZENS = 32645;
        public const int OCR_SIZEALL = 32646;
        public const int OCR_NO = 32648;
        public const int OCR_HAND = 32649;
        public const int OCR_APPSTARTING = 32650;
       static int[] cursors = {
    OCR_NORMAL,
    OCR_IBEAM,
    OCR_WAIT,
    OCR_CROSS,
    OCR_UP,
    OCR_SIZE,
    OCR_ICON,
    OCR_SIZENWSE,
    OCR_SIZENESW,
    OCR_SIZEWE,
    OCR_SIZENS,
    OCR_SIZEALL,
    OCR_NO,
    OCR_HAND,
    OCR_APPSTARTING
};

        [DllImport("user32.dll", SetLastError = true)]
        private static extern IntPtr CreateCursor(IntPtr hInst, int xHotSpot, int yHotSpot, int nWidth, int nHeight, byte[] pvANDPlane, byte[] pvXORPlane);

        [DllImport("user32.dll", SetLastError = true)]
        private static extern bool SetSystemCursor(IntPtr hcur, uint id);

        

        public static void HideCursor()
        {
            foreach (uint cursor in cursors)
            {
                byte[] andMask = new byte[128];
                byte[] xorMask = new byte[128];

                Array.Fill(andMask, (byte)0xFF);
                Array.Fill(xorMask, (byte)0x00);

                IntPtr invisible = CreateCursor(
                    IntPtr.Zero,
                    0,
                    0,
                    32,
                    32,
                    andMask,
                    xorMask
                );


                if (invisible == IntPtr.Zero)
                    throw new System.ComponentModel.Win32Exception(
                        Marshal.GetLastWin32Error()
                    );


                if (!SetSystemCursor(invisible, cursor))
                {
                    
                }
            }
        }


        [DllImport("user32.dll", SetLastError = true)]
        private static extern bool SystemParametersInfo(uint uiAction, uint uiParam, IntPtr pvParam, uint fWinIni);

        public static void RestoreCursor()
        {
            SystemParametersInfo(0x0057, 0, IntPtr.Zero, 0);
        }

    }
}
